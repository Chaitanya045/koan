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
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_content_card.h"
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
constexpr base::TimeDelta kToolbarHideCheckInterval = base::Milliseconds(100);
// How long a keyboard-opened sidebar stays before hover decides.
constexpr base::TimeDelta kSidebarPeekGrace = base::Seconds(3);

// Zen's floating URL bar (ZenUIManager.mjs, zen-omnibox.css): as wide as
// two thirds of the window, at most 750 DIP, centred; the open bar with its
// results (about this tall) is centred vertically.
constexpr int kFloatingUrlBarMaxWidth = 750;
constexpr int kFloatingUrlBarOpenHeight = 330;
constexpr int kFloatingUrlBarRadius = 12;
constexpr base::TimeDelta kFloatingUrlBarFadeDuration = base::Milliseconds(120);

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
  pref_change_registrar_.Init(browser_view_->GetProfile()->GetPrefs());
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
  }
}

void KoanCompactModeController::OnBrowserViewDestroying() {
  if (mode_ != Mode::kNone) {
    Deactivate();
  }
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_ = nullptr;
  }
}

void KoanCompactModeController::ToggleCompactMode() {
  PrefService* prefs = browser_view_->GetProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kCompactModeEnabled,
                    !prefs->GetBoolean(prefs::kCompactModeEnabled));
}

void KoanCompactModeController::ToggleSingleToolbar() {
  PrefService* prefs = browser_view_->GetProfile()->GetPrefs();
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
  sidebar_peek_timer_.Start(FROM_HERE, kToolbarHideCheckInterval, this,
                            &KoanCompactModeController::CheckSidebarPeek);
  return true;
}

void KoanCompactModeController::CheckSidebarPeek() {
  auto* tab_strip = browser_view_->tab_strip_view();
  if (!peek_lock_ || !tab_strip) {
    sidebar_peek_timer_.Stop();
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  // Give the pointer time to reach the sidebar after a keyboard peek.
  if (tab_strip->IsMouseHovered() ||
      now - sidebar_peek_started_ < kSidebarPeekGrace) {
    sidebar_unhovered_since_.reset();
    return;
  }
  if (!sidebar_unhovered_since_) {
    sidebar_unhovered_since_ = now;
    return;
  }
  if (now - *sidebar_unhovered_since_ >= kToolbarHideDelay) {
    sidebar_peek_timer_.Stop();
    peek_lock_.reset();
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
}

void KoanCompactModeController::OnViewBoundsChanged(
    views::View* observed_view) {
  // BrowserView's layout puts the top container back at full width.
  LayoutFloatingUrlBar();
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
    if (sidebar_nav_) {
      sidebar_nav_->SetEditing(true);
    }
    toolbar_hide_timer_.Stop();
    // Just the location bar: hide the rest of the toolbar.
    for (views::View* sibling : location_bar->parent()->children()) {
      if (sibling != location_bar && sibling->GetVisible()) {
        hidden_for_floating_.push_back(
            std::make_unique<views::ViewTracker>(sibling));
      }
    }
    for (const auto& tracker : hidden_for_floating_) {
      tracker->view()->SetVisible(false);
    }
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
  if (sidebar_nav_) {
    sidebar_nav_->SetEditing(false);
  }
  top_container_observation_.Reset();
  toolbar_observation_.Reset();
  for (const auto& tracker : hidden_for_floating_) {
    if (tracker->view()) {
      tracker->view()->SetVisible(true);
    }
  }
  hidden_for_floating_.clear();
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
  if (!url_bar_floating_ || laying_out_floating_ || !toolbar ||
      !top_container->parent()) {
    return;
  }
  base::AutoReset<bool> laying_out(&laying_out_floating_, true);
  const gfx::Rect area = views::View::ConvertRectToTarget(
      browser_view_, top_container->parent(), browser_view_->GetLocalBounds());
  const int width = std::min(area.width() * 2 / 3, kFloatingUrlBarMaxWidth);
  const int height = toolbar->GetPreferredSize().height();
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
  const PrefService* prefs = browser_view_->GetProfile()->GetPrefs();
  if (prefs->GetBoolean(prefs::kCompactModeEnabled)) {
    return Mode::kCompact;
  }
  if (prefs->GetBoolean(prefs::kSingleToolbarEnabled)) {
    return Mode::kSingleToolbar;
  }
  return Mode::kNone;
}

bool KoanCompactModeController::DesiredCompactKeepsSidebarNav() const {
  return browser_view_->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSingleToolbarEnabled);
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
  if (sidebar_nav_ || !region ||
      region->GetClassName() != "VerticalTabStripRegionView") {
    return;
  }
  for (views::View* child : region->children()) {
    // The navigation buttons go in the top row, next to the sidebar toggle
    // (laid out by the koan_sidebar_header.h hooks).
    if (child->GetClassName() == "VerticalTabStripTopContainer" &&
        !sidebar_nav_buttons_) {
      sidebar_nav_buttons_ = child->AddChildView(
          std::make_unique<KoanSidebarNavButtons>(browser_view_));
      child->InvalidateLayout();
    }
    // The URL pill at the top of the content area (a FlexLayoutView), above
    // the separator and the tabs.
    if (child->GetClassName() == "FlexLayoutView" && !sidebar_nav_) {
      sidebar_nav_ = child->AddChildViewAt(
          std::make_unique<KoanSidebarNavView>(browser_view_), 0);
    }
  }
}

void KoanCompactModeController::Deactivate() {
  VLOG(1) << (mode_ == Mode::kCompact ? "Koan: compact mode off"
                                      : "Koan: single toolbar off");
  SetUrlBarFloating(false);
  mode_ = Mode::kNone;
  compact_keeps_sidebar_nav_ = false;
  if (sidebar_nav_) {
    sidebar_nav_->parent()->RemoveChildViewT(
        std::exchange(sidebar_nav_, nullptr));
  }
  if (sidebar_nav_buttons_) {
    views::View* row = sidebar_nav_buttons_->parent();
    row->RemoveChildViewT(std::exchange(sidebar_nav_buttons_, nullptr));
    row->InvalidateLayout();
  }

  toolbar_hide_timer_.Stop();
  toolbar_unwanted_since_.reset();
  sidebar_peek_timer_.Stop();
  hover_lock_.reset();
  peek_lock_.reset();
  if (sidebar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(sidebar_hot_zone_, nullptr));
  }
  if (toolbar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(toolbar_hot_zone_, nullptr));
  }

  // Mirrors BrowserView::ReparentTopContainerForEndOfImmersive().
  views::View* top_container = browser_view_->top_container();
  if (top_container->parent() != browser_view_ && top_container_index_) {
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
  if (revealed) {
    toolbar_unwanted_since_.reset();
    if (!toolbar_hide_timer_.IsRunning()) {
      toolbar_hide_timer_.Start(FROM_HERE, kToolbarHideCheckInterval, this,
                                &KoanCompactModeController::CheckToolbarHide);
    }
  } else {
    toolbar_hide_timer_.Stop();
  }
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
  if (ShouldKeepToolbarRevealed()) {
    toolbar_unwanted_since_.reset();
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!toolbar_unwanted_since_) {
    toolbar_unwanted_since_ = now;
    return;
  }
  if (now - *toolbar_unwanted_since_ >= kToolbarHideDelay) {
    SetToolbarRevealed(false, /*animate=*/true);
  }
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
}

}  // namespace koan
