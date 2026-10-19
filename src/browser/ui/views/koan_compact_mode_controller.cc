// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_compact_mode_controller.h"

#include <algorithm>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_sidebar_nav_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace koan {

namespace {

constexpr base::TimeDelta kToolbarRevealDuration = base::Milliseconds(160);
constexpr base::TimeDelta kToolbarHideDuration = base::Milliseconds(200);
// How long the pointer must be away before the toolbar hides again.
constexpr base::TimeDelta kToolbarHideDelay = base::Milliseconds(400);
// How long a keyboard-opened sidebar stays before hover decides.
constexpr base::TimeDelta kSidebarPeekGrace = base::Seconds(3);

// Zen's floating URL bar (ZenUIManager.mjs, zen-omnibox.css): as wide as
// two thirds of the window, at most 750 DIP, centred; the open bar with its
// results (about this tall) is centred vertically.
constexpr int kFloatingUrlBarMaxWidth = 750;
constexpr int kFloatingUrlBarOpenHeight = 330;
constexpr int kFloatingUrlBarRadius = 12;
constexpr base::TimeDelta kFloatingUrlBarFadeDuration = base::Milliseconds(120);
constexpr base::TimeDelta kCaptionButtonFadeDuration = base::Milliseconds(120);

// Returns the BrowserView's top-container overlay view: a child of the
// widget's NonClientView, drawn above the client view (and so above the
// page). BrowserView only exposes it on macOS, and its class is private to
// browser_view.cc, so find it by its metadata class name.
views::View* GetTopContainerOverlayView(BrowserView* browser_view) {
  views::Widget* widget = browser_view->GetWidget();
  if (!widget || !widget->non_client_view()) {
    return nullptr;
  }
  for (views::View* child : widget->non_client_view()->children()) {
    if (child->GetClassName() == "TopContainerOverlayView") {
      return child;
    }
  }
  return nullptr;
}

// Whether `view` is one of the frame's caption buttons (minimize, maximize,
// restore, close): image buttons in the GTK-native frame, Material buttons
// in Chrome's own (e.g. themed) frame. The macOS frame (BrowserFrameViewMac)
// has none: AppKit draws the window buttons (traffic lights) at the leading
// edge, so hiding caption buttons is a no-op there.
bool IsCaptionButton(const views::View* view) {
#if BUILDFLAG(IS_MAC)
  return false;
#else
  return view->GetClassName() == "ImageButton" ||
         view->GetClassName() == "FrameCaptionButton";
#endif
}

}  // namespace

// An invisible strip along a window edge that reports pointer enter/exit.
// It sits in the content card's gutter, where no native web contents window
// can swallow the events.
class KoanHotZone : public views::View {
  METADATA_HEADER(KoanHotZone, views::View)

 public:
  explicit KoanHotZone(base::RepeatingCallback<void(bool)> on_hover)
      : on_hover_(std::move(on_hover)) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    on_hover_.Run(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    on_hover_.Run(false);
  }

 private:
  base::RepeatingCallback<void(bool)> on_hover_;
};

BEGIN_METADATA(KoanHotZone)
END_METADATA

KoanCompactModeController::KoanCompactModeController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  pref_change_registrar_.Init(
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs());
  for (const char* pref :
       {prefs::kCompactModeEnabled, prefs::kSingleToolbarEnabled}) {
    pref_change_registrar_.Add(
        pref, base::BindRepeating(&KoanCompactModeController::UpdateActiveSoon,
                                  base::Unretained(this)));
  }
}

KoanCompactModeController::~KoanCompactModeController() {
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
  }
}

void KoanCompactModeController::OnAddedToWidget() {
  if (!focus_manager_ && browser_view_->GetFocusManager()) {
    focus_manager_ = browser_view_->GetFocusManager();
    focus_manager_->AddFocusChangeListener(this);
  }
  UpdateActiveSoon();
}

void KoanCompactModeController::OnBrowserViewLayout() {
  // Fullscreen and tab strip mode changes show up as layouts. Re-evaluate
  // outside of layout, since (de)activation reparents views.
  if (mode_ != DesiredMode()) {
    UpdateActiveSoon();
  }
  if (mode_ != Mode::kNone) {
    LayoutHotZones();
    // The top container's height may have changed (e.g. bookmarks bar).
    if (!toolbar_revealed_) {
      ApplyToolbarTransform(/*animate=*/false);
    }
    LayoutFloatingUrlBar();
    UpdateCaptionButtons();
    // The tab strip may not have existed yet when the mode was activated;
    // add the header outside of layout.
    if ((mode_ == Mode::kSingleToolbar || compact_keeps_sidebar_nav_) &&
        (!sidebar_nav_.view() || !sidebar_nav_buttons_.view())) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&KoanCompactModeController::InstallSidebarNav,
                         weak_factory_.GetWeakPtr()));
    }
  }
}

void KoanCompactModeController::OnBrowserViewDestroying() {
  if (mode_ != Mode::kNone) {
    Deactivate();
  }
  pointer_monitor_.reset();
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_ = nullptr;
  }
}

void KoanCompactModeController::ToggleCompactMode() {
  PrefService* prefs =
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kCompactModeEnabled,
                    !prefs->GetBoolean(prefs::kCompactModeEnabled));
}

void KoanCompactModeController::ToggleSingleToolbar() {
  PrefService* prefs =
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kSingleToolbarEnabled,
                    !prefs->GetBoolean(prefs::kSingleToolbarEnabled));
}

bool KoanCompactModeController::PeekSidebar() {
  if (!hides_sidebar()) {
    return false;
  }
  if (!peek_lock_) {
    if (auto* tab_strip = browser_view_->tab_strip_view()) {
      peek_lock_ =
          tab_strip->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
    }
  }
  sidebar_unhovered_since_.reset();
  sidebar_peek_started_ = base::TimeTicks::Now();
  sidebar_peek_timer_.Stop();
  CheckSidebarPeek();
  UpdatePointerMonitor();
  return true;
}

void KoanCompactModeController::CheckSidebarPeek() {
  if (!peek_lock_) {
    return;
  }
  auto* tab_strip = browser_view_->tab_strip_view();
  if (tab_strip && tab_strip->IsMouseHovered()) {
    sidebar_unhovered_since_.reset();
    sidebar_peek_timer_.Stop();
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!sidebar_unhovered_since_) {
    sidebar_unhovered_since_ = now;
  }
  // Give the pointer time to reach the sidebar after a keyboard peek.
  const base::TimeTicks hide_at =
      std::max(sidebar_peek_started_ + kSidebarPeekGrace,
               *sidebar_unhovered_since_ + kToolbarHideDelay);
  if (!tab_strip || now >= hide_at) {
    sidebar_peek_timer_.Stop();
    peek_lock_.reset();
    UpdatePointerMonitor();
    return;
  }
  if (!sidebar_peek_timer_.IsRunning()) {
    sidebar_peek_timer_.Start(FROM_HERE, hide_at - now, this,
                              &KoanCompactModeController::CheckSidebarPeek);
  }
}

void KoanCompactModeController::OnDidChangeFocus(views::View* focused_before,
                                                 views::View* focused_now) {
  if (!hides_toolbar()) {
    return;
  }
  // Editing the address (the URL pill, Ctrl+L) floats the URL bar until
  // focus leaves it.
  LocationBarView* location_bar = browser_view_->GetLocationBarView();
  const bool editing =
      focused_now && location_bar && location_bar->Contains(focused_now);
  SetUrlBarFloating(editing);
  // Focusing anything else in the toolbar (e.g. Alt+F) reveals it; the hide
  // timer keeps it revealed until focus leaves.
  if (!editing && focused_now &&
      browser_view_->top_container()->Contains(focused_now)) {
    SetToolbarRevealed(true, /*animate=*/true);
  }
  CheckToolbarHide();
}

void KoanCompactModeController::OnViewBoundsChanged(
    views::View* observed_view) {
  // BrowserView's layout puts the top container back at full width.
  if (observed_view == browser_view_->top_container() ||
      observed_view == browser_view_->toolbar()) {
    LayoutFloatingUrlBar();
  }
}

void KoanCompactModeController::OnViewVisibilityChanged(
    views::View* observed_view,
    views::View* starting_view,
    bool visible) {
  // A toolbar view that other code shows while the URL bar floats: it
  // should be visible afterwards, but not inside the floating card.
  auto it = floating_restore_.find(observed_view);
  if (observed_view != starting_view || it == floating_restore_.end() ||
      changing_toolbar_visibility_ || !visible) {
    return;
  }
  it->second = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanCompactModeController::HideToolbarForFloating,
                     weak_factory_.GetWeakPtr()));
}

void KoanCompactModeController::OnViewIsDeleting(views::View* observed_view) {
  if (floating_restore_.erase(observed_view)) {
    floating_siblings_observation_.RemoveObservation(observed_view);
  }
}

void KoanCompactModeController::OnEvent(const ui::Event& event) {
  CheckRevealedHides();
}

void KoanCompactModeController::HideToolbarForFloating() {
  if (!url_bar_floating_) {
    return;
  }
  base::AutoReset<bool> changing(&changing_toolbar_visibility_, true);
  for (const auto& [view, restore] : floating_restore_) {
    view->SetVisible(false);
  }
}

void KoanCompactModeController::SetUrlBarFloating(bool floating) {
  if (floating == url_bar_floating_) {
    return;
  }
  views::View* top_container = browser_view_->top_container();
  LocationBarView* location_bar = browser_view_->GetLocationBarView();
  if (floating) {
    if (mode_ == Mode::kNone || !location_bar || !location_bar->parent() ||
        !top_container->layer() || !browser_view_->toolbar()) {
      return;
    }
    url_bar_floating_ = true;
    toolbar_revealed_ = false;
    if (sidebar_nav()) {
      sidebar_nav()->SetEditing(true);
    }
    toolbar_hide_timer_.Stop();
    UpdatePointerMonitor();
    // Just the location bar: hide the rest of the toolbar, remembering what
    // to restore (and watching for other code showing things meanwhile).
    for (views::View* sibling : location_bar->parent()->children()) {
      if (sibling != location_bar) {
        floating_restore_[sibling] = sibling->GetVisible();
        floating_siblings_observation_.AddObservation(sibling);
      }
    }
    HideToolbarForFloating();
    top_container->SetCanProcessEventsWithinSubtree(true);
    ui::Layer* layer = top_container->layer();
    layer->GetAnimator()->AbortAllAnimations();
    layer->SetTransform(gfx::Transform());
    layer->SetRoundedCornerRadius(gfx::RoundedCornersF(kFloatingUrlBarRadius));
    layer->SetIsFastRoundedCorner(true);
    layer->SetMasksToBounds(true);
    top_container_observation_.Observe(top_container);
    toolbar_observation_.Observe(browser_view_->toolbar());
    LayoutFloatingUrlBar();
    layer->SetOpacity(0.0f);
    views::AnimationBuilder()
        .Once()
        .SetDuration(kFloatingUrlBarFadeDuration)
        .SetOpacity(top_container, 1.0f, gfx::Tween::EASE_OUT);
    return;
  }

  url_bar_floating_ = false;
  if (sidebar_nav()) {
    sidebar_nav()->SetEditing(false);
  }
  top_container_observation_.Reset();
  toolbar_observation_.Reset();
  floating_siblings_observation_.RemoveAllObservations();
  {
    base::AutoReset<bool> changing(&changing_toolbar_visibility_, true);
    for (const auto& [view, restore] : floating_restore_) {
      view->SetVisible(restore);
    }
  }
  floating_restore_.clear();
  if (ui::Layer* layer = top_container->layer()) {
    layer->GetAnimator()->AbortAllAnimations();
    layer->SetOpacity(1.0f);
    layer->SetRoundedCornerRadius(gfx::RoundedCornersF());
    layer->SetMasksToBounds(false);
  }
  top_container->SetCanProcessEventsWithinSubtree(toolbar_revealed_);
  ApplyToolbarTransform(/*animate=*/false);
  browser_view_->InvalidateLayout();
}

void KoanCompactModeController::LayoutFloatingUrlBar() {
  views::View* top_container = browser_view_->top_container();
  ToolbarView* toolbar = browser_view_->toolbar();
  // Fullscreen deactivates the mode (which ends floating) right after.
  if (!url_bar_floating_ || laying_out_floating_ || !toolbar ||
      !top_container->parent() || browser_view_->IsFullscreen()) {
    return;
  }
  base::AutoReset<bool> laying_out(&laying_out_floating_, true);
  const gfx::Rect area = views::View::ConvertRectToTarget(
      browser_view_, top_container->parent(), browser_view_->GetLocalBounds());
  const int width = std::min(area.width() * 2 / 3, kFloatingUrlBarMaxWidth);
  const int height = toolbar->GetPreferredSize().height();
  // The card is the toolbar row only (a bookmarks bar is clipped), like
  // Zen's floating URL bar.
  const int top = std::max(0, (area.height() - kFloatingUrlBarOpenHeight) / 2);
  top_container->SetBoundsRect(gfx::Rect(area.x() + (area.width() - width) / 2,
                                         area.y() + top, width, height));
  toolbar->SetBoundsRect(gfx::Rect(0, 0, width, height));
}

KoanCompactModeController::Mode KoanCompactModeController::DesiredMode() const {
  if (!features::IsCompactModeEnabled() ||
      !GetTopContainerOverlayView(browser_view_) ||
      !UseContentCard(browser_view_)) {
    return Mode::kNone;
  }
  const PrefService* prefs =
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs();
  if (prefs->GetBoolean(prefs::kCompactModeEnabled)) {
    return Mode::kCompact;
  }
  if (prefs->GetBoolean(prefs::kSingleToolbarEnabled)) {
    return Mode::kSingleToolbar;
  }
  return Mode::kNone;
}

bool KoanCompactModeController::DesiredCompactKeepsSidebarNav() const {
  return browser_view_->GetProfile()
      ->GetOriginalProfile()
      ->GetPrefs()
      ->GetBoolean(prefs::kSingleToolbarEnabled);
}

void KoanCompactModeController::UpdateActiveSoon() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanCompactModeController::UpdateActive,
                                weak_factory_.GetWeakPtr()));
}

void KoanCompactModeController::UpdateActive() {
  const Mode desired = DesiredMode();
  if (desired == mode_ &&
      (mode_ != Mode::kCompact ||
       DesiredCompactKeepsSidebarNav() == compact_keeps_sidebar_nav_)) {
    return;
  }
  if (mode_ != Mode::kNone) {
    Deactivate();
  }
  if (desired != Mode::kNone) {
    Activate(desired);
  }
  OnContentCardChanged();
}

void KoanCompactModeController::OnContentCardChanged() {
  // The contents area may keep its bounds while the card inside it moves, so
  // lay it out explicitly; Glance sits over the card.
  if (MultiContentsView* contents = browser_view_->multi_contents_view()) {
    contents->InvalidateLayout();
  }
  browser_view_->InvalidateLayout();
  LayoutHotZones();
  if (auto* controller = KoanBrowserViewController::From(browser_view_)) {
    controller->glance()->OnBrowserViewLayout();
  }
}

void KoanCompactModeController::Activate(Mode mode) {
  mode_ = mode;
  VLOG(1) << (mode == Mode::kCompact ? "Koan: compact mode on"
                                     : "Koan: single toolbar on");

  // Compact: collapse the sidebar; the layout then gives it zero width and
  // upstream's expand-on-hover draws it over the page.
  if (auto* state =
          tabs::VerticalTabStripStateController::From(browser_view_->browser());
      state && mode == Mode::kCompact) {
    collapsed_before_ = state->IsCollapsed();
    if (!state->IsCollapsed()) {
      state->RequestCollapse(true);
    }
  }

  // Toolbar: move the top container into the overlay view, above the page.
  // Mirrors BrowserView::ReparentTopContainerForStartOfImmersive(); the
  // layout positions a top container that isn't parented to the BrowserView.
  views::View* top_container = browser_view_->top_container();
  views::View* overlay = GetTopContainerOverlayView(browser_view_);
  if (top_container->parent() == browser_view_) {
    top_container_index_ = browser_view_->GetIndexOf(top_container);
    top_container->SetPaintToLayer();
    top_container->layer()->SetFillsBoundsOpaquely(false);
    top_container->SetProperty(views::kViewDoesNotLayOutChildren, false);
    overlay->AddChildView(top_container);
    overlay->SetVisible(true);
  }
  SetToolbarRevealed(false, /*animate=*/false);

  compact_keeps_sidebar_nav_ =
      mode == Mode::kCompact && DesiredCompactKeepsSidebarNav();
  if (mode == Mode::kSingleToolbar || compact_keeps_sidebar_nav_) {
    InstallSidebarNav();
    // Zen: no strip for the window buttons; they appear at the top edge.
    caption_strip_collapsible_ = true;
    caption_strip_revealed_ = false;
    caption_hot_zone_ = browser_view_->AddChildView(
        std::make_unique<KoanHotZone>(base::BindRepeating(
            &KoanCompactModeController::OnCaptionHotZoneHovered,
            base::Unretained(this))));
    UpdateCaptionButtons();
  }
  if (mode == Mode::kSingleToolbar) {
    browser_view_->InvalidateLayout();
    return;
  }

  // Edge hot zones, below the tab strip so the expanded strip gets the
  // pointer, above the page.
  std::optional<size_t> index;
  if (auto* tab_strip = browser_view_->tab_strip_view()) {
    index = browser_view_->GetIndexOf(tab_strip);
  }
  const size_t insert_at = index.value_or(browser_view_->children().size());
  sidebar_hot_zone_ = browser_view_->AddChildViewAt(
      std::make_unique<KoanHotZone>(base::BindRepeating(
          &KoanCompactModeController::OnSidebarHotZoneHovered,
          base::Unretained(this))),
      insert_at);
  // With single toolbar there is no toolbar to reveal (like Zen).
  if (!compact_keeps_sidebar_nav_) {
    toolbar_hot_zone_ = browser_view_->AddChildViewAt(
        std::make_unique<KoanHotZone>(base::BindRepeating(
            &KoanCompactModeController::OnToolbarHotZoneHovered,
            base::Unretained(this))),
        insert_at);
  }

  browser_view_->InvalidateLayout();
  LayoutHotZones();
}

void KoanCompactModeController::InstallSidebarNav() {
  views::View* region = browser_view_->tab_strip_view();
  if ((mode_ != Mode::kSingleToolbar && !compact_keeps_sidebar_nav_) ||
      (sidebar_nav_.view() && sidebar_nav_buttons_.view()) || !region ||
      region->GetClassName() != "VerticalTabStripRegionView") {
    return;
  }
  for (views::View* child : region->children()) {
    // The navigation buttons go in the top row, next to the sidebar toggle
    // (laid out by the koan_sidebar_header.h hooks).
    if (child->GetClassName() == "VerticalTabStripTopContainer" &&
        !sidebar_nav_buttons_.view()) {
      sidebar_nav_buttons_.SetView(child->AddChildView(
          std::make_unique<KoanSidebarNavButtons>(browser_view_)));
      child->InvalidateLayout();
    }
    // The URL pill at the top of the content area (a FlexLayoutView), above
    // the separator and the tabs.
    if (child->GetClassName() == "FlexLayoutView" && !sidebar_nav_.view()) {
      sidebar_nav_.SetView(child->AddChildViewAt(
          std::make_unique<KoanSidebarNavView>(browser_view_), 0));
    }
  }
}

KoanSidebarNavView* KoanCompactModeController::sidebar_nav() {
  return static_cast<KoanSidebarNavView*>(sidebar_nav_.view());
}

void KoanCompactModeController::Deactivate() {
  VLOG(1) << (mode_ == Mode::kCompact ? "Koan: compact mode off"
                                      : "Koan: single toolbar off");
  SetUrlBarFloating(false);
  mode_ = Mode::kNone;
  compact_keeps_sidebar_nav_ = false;
  caption_strip_collapsible_ = false;
  caption_strip_revealed_ = false;
  caption_hide_timer_.Stop();
  caption_unwanted_since_.reset();
  if (caption_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(caption_hot_zone_, nullptr));
  }
  UpdateCaptionButtons();
  // The trackers clear themselves if upstream destroyed the views (e.g. a
  // tab strip mode change).
  for (views::ViewTracker* tracker : {&sidebar_nav_, &sidebar_nav_buttons_}) {
    views::View* view = tracker->view();
    tracker->SetView(nullptr);
    if (view && view->parent()) {
      views::View* parent = view->parent();
      parent->RemoveChildViewT(view);
      parent->InvalidateLayout();
    }
  }

  toolbar_hide_timer_.Stop();
  toolbar_unwanted_since_.reset();
  sidebar_peek_timer_.Stop();
  hover_lock_.reset();
  peek_lock_.reset();
  pointer_monitor_.reset();
  if (sidebar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(sidebar_hot_zone_, nullptr));
  }
  if (toolbar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(toolbar_hot_zone_, nullptr));
  }

  // Mirrors BrowserView::ReparentTopContainerForEndOfImmersive().
  views::View* top_container = browser_view_->top_container();
  bool restore_top_container =
      top_container->parent() != browser_view_ && top_container_index_;
#if BUILDFLAG(IS_MAC)
  // Immersive fullscreen moves the top container into its own overlay
  // widget, and keeps it there until it ends: leave it there, hide Koan's
  // (now empty) overlay, and undo Koan's own state on the top container:
  // the hidden toolbar's slide out of view and its ignoring the pointer.
  // The layer stays; the overlay widget paints the top container with it.
  if (const views::Widget* immersive = browser_view_->overlay_widget();
      immersive && top_container->GetWidget() == immersive) {
    restore_top_container = false;
    if (views::View* overlay = GetTopContainerOverlayView(browser_view_)) {
      overlay->SetVisible(false);
    }
    if (ui::Layer* layer = top_container->layer()) {
      layer->GetAnimator()->AbortAllAnimations();
      layer->SetTransform(gfx::Transform());
    }
    top_container->SetCanProcessEventsWithinSubtree(true);
  }
#endif
  if (restore_top_container) {
    if (top_container->layer()) {
      top_container->layer()->GetAnimator()->AbortAllAnimations();
    }
    if (views::View* overlay = GetTopContainerOverlayView(browser_view_)) {
      overlay->SetVisible(false);
    }
    top_container->SetCanProcessEventsWithinSubtree(true);
    top_container->DestroyLayer();
    browser_view_->AddChildViewAt(
        top_container,
        std::min(*top_container_index_, browser_view_->children().size()));
    browser_view_->infobar_container()->InsertAfterInFocusList(top_container);
    top_container->SetProperty(views::kViewDoesNotLayOutChildren, true);
  }
  top_container_index_.reset();
  toolbar_revealed_ = false;

  if (auto* state = tabs::VerticalTabStripStateController::From(
          browser_view_->browser())) {
    if (collapsed_before_.has_value() && !*collapsed_before_ &&
        state->IsCollapsed()) {
      state->RequestCollapse(false);
    }
  }
  collapsed_before_.reset();

  browser_view_->InvalidateLayout();
}

void KoanCompactModeController::SetToolbarRevealed(bool revealed,
                                                   bool animate) {
  if (mode_ == Mode::kNone) {
    return;
  }
  const bool changed = toolbar_revealed_ != revealed;
  toolbar_revealed_ = revealed;
  browser_view_->top_container()->SetCanProcessEventsWithinSubtree(revealed);
  if (changed || !animate) {
    ApplyToolbarTransform(animate);
  }
  toolbar_unwanted_since_.reset();
  toolbar_hide_timer_.Stop();
  UpdatePointerMonitor();
  CheckToolbarHide();
}

void KoanCompactModeController::ApplyToolbarTransform(bool animate) {
  views::View* top_container = browser_view_->top_container();
  if (!top_container->layer()) {
    return;
  }
  gfx::Transform target;
  if (!toolbar_revealed_ && !url_bar_floating_) {
    // Slide fully above the window, including any drop shadow.
    target.Translate(0, -top_container->height());
  }
  if (!animate) {
    if (top_container->layer()->GetAnimator()->is_animating()) {
      return;
    }
    top_container->layer()->SetTransform(target);
    return;
  }
  // Compositor-driven, so the slide stays smooth even if the UI thread is
  // busy.
  views::AnimationBuilder()
      .SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
      .Once()
      .SetDuration(toolbar_revealed_ ? kToolbarRevealDuration
                                     : kToolbarHideDuration)
      .SetTransform(
          top_container, target,
          toolbar_revealed_ ? gfx::Tween::EASE_OUT : gfx::Tween::EASE_IN);
}

bool KoanCompactModeController::ShouldKeepToolbarRevealed() const {
  views::View* top_container = browser_view_->top_container();
  if (top_container->IsMouseHovered() ||
      (toolbar_hot_zone_ && toolbar_hot_zone_->IsMouseHovered())) {
    return true;
  }
  // Keep it while something in it has focus (omnibox editing) or a menu
  // (app menu, omnibox context menu, ...) is open.
  if (focus_manager_ && focus_manager_->GetFocusedView() &&
      top_container->Contains(focus_manager_->GetFocusedView())) {
    return true;
  }
  return views::MenuController::GetForOwnerWidget(browser_view_->GetWidget()) !=
         nullptr;
}

void KoanCompactModeController::CheckToolbarHide() {
  if (!toolbar_revealed_) {
    return;
  }
  if (ShouldKeepToolbarRevealed()) {
    toolbar_unwanted_since_.reset();
    toolbar_hide_timer_.Stop();
    // A menu closing sends the window no event: look again later.
    if (views::MenuController::GetForOwnerWidget(browser_view_->GetWidget())) {
      toolbar_hide_timer_.Start(FROM_HERE, kToolbarHideDelay, this,
                                &KoanCompactModeController::CheckToolbarHide);
    }
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!toolbar_unwanted_since_) {
    toolbar_unwanted_since_ = now;
  }
  const base::TimeTicks hide_at = *toolbar_unwanted_since_ + kToolbarHideDelay;
  if (now >= hide_at) {
    SetToolbarRevealed(false, /*animate=*/true);
    return;
  }
  if (!toolbar_hide_timer_.IsRunning()) {
    toolbar_hide_timer_.Start(FROM_HERE, hide_at - now, this,
                              &KoanCompactModeController::CheckToolbarHide);
  }
}

void KoanCompactModeController::UpdatePointerMonitor() {
  const bool wanted =
      mode_ != Mode::kNone &&
      (toolbar_revealed_ || caption_strip_revealed_ || peek_lock_ != nullptr);
  views::Widget* widget = browser_view_->GetWidget();
  if (!wanted || !widget) {
    pointer_monitor_.reset();
    return;
  }
  if (!pointer_monitor_) {
    pointer_monitor_ = views::EventMonitor::CreateWindowMonitor(
        this, widget->GetNativeWindow(),
        {ui::EventType::kMouseMoved, ui::EventType::kMouseDragged,
         ui::EventType::kMouseEntered, ui::EventType::kMouseExited});
  }
}

void KoanCompactModeController::CheckRevealedHides() {
  CheckToolbarHide();
  CheckCaptionStripHide();
  CheckSidebarPeek();
}

void KoanCompactModeController::OnSidebarHotZoneHovered(bool hovered) {
  if (mode_ == Mode::kNone) {
    return;
  }
  if (hovered) {
    if (!hover_lock_) {
      if (auto* tab_strip = browser_view_->tab_strip_view()) {
        hover_lock_ = tab_strip->GetExpandOnHoverLock(
            ExpandOnHoverLockType::kKeepExpanded);
      }
    }
    return;
  }
  // Once the strip has slid over the hot zone the pointer is inside the
  // strip, and upstream's hover tracking keeps it open until it leaves.
  hover_lock_.reset();
}

void KoanCompactModeController::OnToolbarHotZoneHovered(bool hovered) {
  if (hovered) {
    SetToolbarRevealed(true, /*animate=*/true);
  }
}

void KoanCompactModeController::OnCaptionHotZoneHovered(bool hovered) {
  if (hovered && caption_strip_collapsible_ && HasPageSideCaptionButtons()) {
    SetCaptionStripRevealed(true);
  }
}

void KoanCompactModeController::SetCaptionStripRevealed(bool revealed) {
  if (!caption_strip_collapsible_ || caption_strip_revealed_ == revealed) {
    return;
  }
  caption_strip_revealed_ = revealed;
  caption_unwanted_since_.reset();
  caption_hide_timer_.Stop();
  UpdatePointerMonitor();
  UpdateCaptionButtons();
  OnContentCardChanged();
  CheckCaptionStripHide();
}

void KoanCompactModeController::CheckCaptionStripHide() {
  if (!caption_strip_revealed_) {
    return;
  }
  bool wanted = caption_hot_zone_ && caption_hot_zone_->IsMouseHovered();
  if (const BrowserFrameView* frame =
          browser_view_->browser_widget()
              ? browser_view_->browser_widget()->GetFrameView()
              : nullptr) {
    for (const views::View* child : frame->children()) {
      if (IsCaptionButton(child) && child->IsMouseHovered()) {
        wanted = true;
      }
    }
  }
  if (wanted) {
    caption_unwanted_since_.reset();
    caption_hide_timer_.Stop();
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!caption_unwanted_since_) {
    caption_unwanted_since_ = now;
  }
  const base::TimeTicks hide_at = *caption_unwanted_since_ + kToolbarHideDelay;
  if (now >= hide_at) {
    SetCaptionStripRevealed(false);
    return;
  }
  if (!caption_hide_timer_.IsRunning()) {
    caption_hide_timer_.Start(
        FROM_HERE, hide_at - now, this,
        &KoanCompactModeController::CheckCaptionStripHide);
  }
}

int KoanCompactModeController::PageLeftInFrame(const views::View* frame) const {
  const views::View* page = browser_view_->multi_contents_view();
  return page ? views::View::ConvertPointToTarget(page, frame, gfx::Point()).x()
              : 0;
}

bool KoanCompactModeController::HasPageSideCaptionButtons() const {
  const BrowserWidget* widget = browser_view_->browser_widget();
  const BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return false;
  }
  const int page_left = PageLeftInFrame(frame);
  for (const views::View* child : frame->children()) {
    if (IsCaptionButton(child) && child->GetVisible() &&
        child->bounds().x() >= page_left) {
      return true;
    }
  }
  return false;
}

void KoanCompactModeController::UpdateCaptionButtons() {
  BrowserWidget* widget = browser_view_->browser_widget();
  BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return;
  }
  const bool collapsed = caption_strip_collapsed();
  const int page_left = PageLeftInFrame(frame);
  // The frame sets the caption buttons' visibility in its layout, so fade
  // them on a layer instead and let a hidden one ignore the pointer.
  for (views::View* child : frame->children()) {
    if (!IsCaptionButton(child)) {
      continue;
    }
    // Only buttons over the page side hide: buttons on the leading edge sit
    // above the sidebar's top row, which upstream keeps clear for them.
    const bool shown = !collapsed || child->bounds().x() < page_left;
    child->SetCanProcessEventsWithinSubtree(shown);
    if (!child->layer()) {
      if (shown) {
        continue;
      }
      child->SetPaintToLayer();
      child->layer()->SetFillsBoundsOpaquely(false);
      child->layer()->SetOpacity(1.0f);
    }
    const float opacity = shown ? 1.0f : 0.0f;
    if (child->layer()->GetTargetOpacity() != opacity) {
      views::AnimationBuilder()
          .SetPreemptionStrategy(
              ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
          .Once()
          .SetDuration(kCaptionButtonFadeDuration)
          .SetOpacity(child, opacity, gfx::Tween::EASE_OUT);
    }
  }
}

void KoanCompactModeController::LayoutHotZones() {
  const gfx::Rect bounds = browser_view_->GetLocalBounds();
  const gfx::Insets gutter = ContentCardInsets(browser_view_);
  if (sidebar_hot_zone_) {
    sidebar_hot_zone_->SetBoundsRect(
        gfx::Rect(bounds.x(), bounds.y(), gutter.left(), bounds.height()));
  }
  if (toolbar_hot_zone_) {
    toolbar_hot_zone_->SetBoundsRect(
        gfx::Rect(bounds.x(), bounds.y(), bounds.width(), gutter.top()));
  }
  // Above the card, beside the sidebar (whose top row has its own buttons).
  if (caption_hot_zone_) {
    const int x = browser_view_->multi_contents_view()
                      ? browser_view_->multi_contents_view()->x()
                      : bounds.x();
    caption_hot_zone_->SetBoundsRect(
        gfx::Rect(x, bounds.y(), bounds.right() - x, gutter.top()));
  }
}

}  // namespace koan
