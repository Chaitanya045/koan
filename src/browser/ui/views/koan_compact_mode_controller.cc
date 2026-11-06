// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_compact_mode_controller.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/immersive/immersive_mode_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_sidebar_animation.h"
#include "koan/browser/ui/views/koan_sidebar_nav_view.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_traffic_lights.h"
#include "koan/browser/ui/views/koan_traffic_lights_controller.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"
#include "koan/browser/ui/views/koan_welcome.h"
#include "koan/browser/ui/views/koan_window_controls.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/browser/ui/views/koan_zen_toasts.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace koan {

namespace {

constexpr base::TimeDelta kToolbarRevealDuration = base::Milliseconds(160);
constexpr base::TimeDelta kToolbarHideDuration = base::Milliseconds(200);
// How long the pointer must be away before the toolbar hides again (Zen's
// zen.view.compact.toolbar-hide-after-hover.duration).
constexpr base::TimeDelta kToolbarHideDelay = base::Milliseconds(1000);
// Zen's zen.view.compact.sidebar-keep-hover.duration: how long the sidebar
// stays out after the pointer leaves it.
constexpr base::TimeDelta kSidebarKeepHover = base::Milliseconds(150);

constexpr base::TimeDelta kCaptionButtonFadeDuration = base::Milliseconds(120);

// Native fullscreen: how close to the window's edge the pointer reveals the
// sidebar or toolbar that compact mode hides. Zen's element keeps a sliver
// of 1px (+ the element separation) at the edge, which the pointer, stopped
// by the screen, rests on: 2pt covers it and rounding. The page covers the
// window edge to edge there, so no view of ours could take the pointer.
constexpr int kFullscreenEdgeSize = 2;

// Zen's flash popup (zen.view.compact.toolbar-flash-popup.duration).
constexpr base::TimeDelta kFlashDuration = base::Milliseconds(800);

// Zen's toolbar context menu (zen-vertical-tabs.ftl).
enum ContextMenuCommand {
  kCommandCompactModeToggle = 1,
  kCommandCompactModeHideSidebar,
  kCommandCompactModeHideToolbar,
  kCommandCompactModeHideBoth,
  kCommandTabsOnRight,
};

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

// Opens the controller's menu when the sidebar is right-clicked.
class KoanSidebarContextMenuController : public views::ContextMenuController {
 public:
  explicit KoanSidebarContextMenuController(
      KoanCompactModeController* controller)
      : controller_(controller) {}

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override {
    controller_->ShowContextMenu(point, source_type);
  }

 private:
  const raw_ptr<KoanCompactModeController> controller_;
};

// Where compact mode reveals the toolbar, the frame's caption buttons over it
// stay theirs: the top container (in the overlay, above the frame) doesn't
// take the pointer there.
class KoanCaptionButtonsTargeter : public views::ViewTargeterDelegate {
 public:
  explicit KoanCaptionButtonsTargeter(BrowserView* browser_view)
      : browser_view_(browser_view) {}

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    const BrowserWidget* widget = browser_view_->browser_widget();
    if (const BrowserFrameView* frame =
            widget ? widget->GetFrameView() : nullptr) {
      for (const views::View* child : frame->children()) {
        if (IsCaptionButton(child) && child->GetVisible() &&
            child->GetCanProcessEventsWithinSubtree() &&
            views::View::ConvertRectToTarget(frame, target, child->bounds())
                .Contains(rect.CenterPoint())) {
          return false;
        }
      }
    }
    return ViewTargeterDelegate::DoesIntersectRect(target, rect);
  }

 private:
  const raw_ptr<BrowserView> browser_view_;
};

// Tells the controller when native fullscreen starts and ends (Chromium's
// immersive mode; only macOS has it for normal windows).
class KoanCompactModeController::ImmersiveObserver
    : public ImmersiveModeController::Observer {
 public:
  ImmersiveObserver(ImmersiveModeController* controller,
                    base::RepeatingClosure on_changed)
      : on_changed_(std::move(on_changed)) {
    observation_.Observe(controller);
  }

  // ImmersiveModeController::Observer:
  void OnImmersiveFullscreenEntered() override { on_changed_.Run(); }
  void OnImmersiveFullscreenExited() override { on_changed_.Run(); }
  void OnImmersiveModeControllerDestroyed() override { observation_.Reset(); }

 private:
  const base::RepeatingClosure on_changed_;
  base::ScopedObservation<ImmersiveModeController,
                          ImmersiveModeController::Observer>
      observation_{this};
};

KoanCompactModeController::KoanCompactModeController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  pref_change_registrar_.Init(GetPrefs());
  for (const char* pref :
       {prefs::kCompactModeEnabled, prefs::kSingleToolbarEnabled,
        prefs::kCompactModeHide, prefs::kUrlBarBehavior}) {
    pref_change_registrar_.Add(
        pref, base::BindRepeating(&KoanCompactModeController::UpdateActiveSoon,
                                  base::Unretained(this)));
  }
  pref_change_registrar_.Add(
      prefs::kTabsOnRight,
      base::BindRepeating(&KoanCompactModeController::OnTabsOnRightChanged,
                          base::Unretained(this)));
  tabs_on_right_ =
      pref_change_registrar_.prefs()->GetBoolean(prefs::kTabsOnRight);
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
#if BUILDFLAG(IS_MAC)
  traffic_lights_ = std::make_unique<KoanTrafficLightsController>(
      browser_view_,
      base::BindRepeating(&KoanCompactModeController::UpdateCaptionButtons,
                          base::Unretained(this)));
#endif
}

KoanCompactModeController::~KoanCompactModeController() {
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
  }
}

void KoanCompactModeController::OnAddedToWidget() {
#if BUILDFLAG(IS_MAC)
  // (After BrowserView, which is added to the immersive mode controller
  // first: by the time this runs, it has put the top container where
  // Chromium wants it.)
  if (!immersive_observer_) {
    if (auto* immersive =
            ImmersiveModeController::From(browser_view_->browser())) {
      immersive_observer_ = std::make_unique<ImmersiveObserver>(
          immersive,
          base::BindRepeating(
              &KoanCompactModeController::OnImmersiveFullscreenChanged,
              base::Unretained(this)));
    }
  }
  // The page's own fullscreen leaves Chromium's immersive mode on, so only
  // the fullscreen controller says when it starts and ends.
  if (!fullscreen_subscription_) {
    if (auto* manager =
            ExclusiveAccessManager::From(browser_view_->browser())) {
      fullscreen_subscription_ =
          manager->fullscreen_controller()->RegisterOnFullscreenStateChanged(
              base::BindRepeating(
                  &KoanCompactModeController::OnFullscreenStateChanged,
                  base::Unretained(this)));
    }
  }
#endif
  if (!focus_manager_ && browser_view_->GetFocusManager()) {
    focus_manager_ = browser_view_->GetFocusManager();
    focus_manager_->AddFocusChangeListener(this);
  }
  UpdateActiveSoon();
}

bool KoanCompactModeController::CanCompactMode() const {
  return features::IsCompactModeEnabled() &&
         GetTopContainerOverlayView(browser_view_) &&
         UseContentCard(browser_view_);
}

void KoanCompactModeController::OnBrowserViewLayout() {
  if (traffic_lights_) {
    traffic_lights_->OnBrowserViewLayout();
  }
  // Fullscreen and tab strip mode changes show up as layouts. Re-evaluate
  // outside of layout, since (de)activation reparents views.
  if (mode_ != DesiredMode()) {
    UpdateActiveSoon();
  }
  InstallContextMenu();
  if (mode_ != Mode::kNone) {
    LayoutHotZones();
    // Every fullscreen transition lays out: the pointer monitor follows (this
    // window's or the whole application's, see UpdatePointerMonitor()).
    UpdatePointerMonitor();
    // The top container's height may have changed (e.g. bookmarks bar).
    if (hides_toolbar() && !toolbar_revealed_) {
      ApplyToolbarTransform(/*animate=*/false);
    }
    KoanBrowserViewController::From(browser_view_)
        ->url_bar()
        ->OnBrowserViewLayout();
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
  // From here on the window is going away: no UI work (the widget and the
  // browser's animation controller may be gone), see Deactivate() and
  // UpdateCaptionButtons(). Letting go of a lock makes the sidebar's region
  // decide again and collapse with an animation, so the locks are released
  // through ReleaseLock(), which defers that until the region is gone.
  // (KoanVerticalTabsPolicy::OnBrowserViewDestroying() ran first, so
  // IsClosing() is true from here on.)
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  // Order matters: resetting the menu runner may run OnContextMenuClosed(),
  // which releases menu_lock_ again, so it is released (and null) first.
  ReleaseLock(menu_lock_);
  menu_runner_.reset();
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
  PrefService* prefs = GetPrefs();
  prefs->SetBoolean(prefs::kCompactModeEnabled,
                    !prefs->GetBoolean(prefs::kCompactModeEnabled));
}

void KoanCompactModeController::ToggleSingleToolbar() {
  PrefService* prefs = GetPrefs();
  prefs->SetBoolean(prefs::kSingleToolbarEnabled, !DesiredSingleToolbar());
}

void KoanCompactModeController::ToggleTabsOnRight() {
  PrefService* prefs = GetPrefs();
  prefs->SetBoolean(prefs::kTabsOnRight,
                    !prefs->GetBoolean(prefs::kTabsOnRight));
}

void KoanCompactModeController::SetCompactModeHides(bool sidebar,
                                                    bool toolbar) {
  GetPrefs()->SetString(prefs::kCompactModeHide,
                        !toolbar  ? prefs::kCompactModeHideSidebar
                        : sidebar ? prefs::kCompactModeHideBoth
                                  : prefs::kCompactModeHideToolbar);
}

PrefService* KoanCompactModeController::GetPrefs() const {
  // Off the record windows share the profile's layout.
  return browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs();
}

bool KoanCompactModeController::PrefHidesSidebar() const {
  return GetPrefs()->GetString(prefs::kCompactModeHide) !=
         prefs::kCompactModeHideToolbar;
}

bool KoanCompactModeController::PrefHidesToolbar() const {
  // Anything else (e.g. an edited Preferences file) means the default.
  const std::string& hide = GetPrefs()->GetString(prefs::kCompactModeHide);
  return hide == prefs::kCompactModeHideToolbar ||
         hide == prefs::kCompactModeHideBoth;
}

bool KoanCompactModeController::caption_strip_collapsed() const {
  if (caption_strip_collapsible_ && !caption_strip_revealed_) {
    return true;
  }
  // (Native fullscreen has no lights to make room for.)
  return lights_keep_strip_open_ && IsWindowFullscreen(browser_view_);
}

void KoanCompactModeController::OnTabsOnRightChanged() {
  tabs_on_right_ =
      pref_change_registrar_.prefs()->GetBoolean(prefs::kTabsOnRight);
  // The hot zones and which window buttons hide depend on the side.
  if (mode_ != Mode::kNone) {
    const Mode mode = mode_;
    Deactivate();
    Activate(mode);
  }
  // Everything in the window is laid out again, mirrored or not; the
  // sidebar's own views too.
  if (views::View* region = browser_view_->tab_strip_view()) {
    for (views::View* child : region->children()) {
      child->InvalidateLayout();
    }
    region->InvalidateLayout();
  }
  if (views::View* top_container = browser_view_->top_container()) {
    top_container->InvalidateLayout();
  }
  OnContentCardChanged();
}

void KoanCompactModeController::OnSidebarCollapseSettled() {
  if (IsClosing()) {
    return;
  }
  // The locks that were refused while the sidebar collapsed (see
  // TakeExpandedLock()) are taken now. (Only the sidebar's: this also runs for
  // the width changes of a resize drag and for expanded states, and
  // ReconcileBubbleHold() has the toolbar's logic in it.)
  const auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  if (!hides_sidebar() || !state || !state->IsCollapsed()) {
    return;
  }
  VLOG(1) << "Koan: sidebar settled collapsed (peek requested: "
          << peek_requested_ << ", hover lock: " << !!hover_lock_
          << ", edge hovered: " << edge_sidebar_hovered_ << ")";
  if (!bubble_lock_) {
    ReconcileBubbleHold();
  }
  if (peek_requested_ && !peek_lock_) {
    peek_requested_ = false;
    peek_lock_ = TakeExpandedLock();
  }
  // A flash asked for during the collapse (a tab selected right after compact
  // mode went on) shows now, for what is left of its time.
  if (!flash_lock_ && flash_timer_.IsRunning()) {
    flash_lock_ = TakeExpandedLock();
  }
  // The pointer is at the edge: on the hot zone, or, in fullscreen, where the
  // hot zone has no width and CheckFullscreenEdges() tells (only when it
  // changes, so a refused lock would never be asked for again).
  if (!hover_lock_ &&
      (edge_sidebar_hovered_ ||
       (sidebar_hot_zone_ && sidebar_hot_zone_->IsMouseHovered()))) {
    hover_lock_ = TakeExpandedLock();
  }
}

std::unique_ptr<ExpandOnHoverLock>
KoanCompactModeController::TakeExpandedLock() {
  // Upstream starts its expand-on-hover animation as soon as such a lock is
  // held. While the sidebar is still collapsing, that happens from inside the
  // collapse animation's own ended notification (it is the one that marks the
  // sidebar collapsed: the region view's UpdateExpandOnHoverState() runs from
  // it). BrowserAnimationController::GroupData::OnAnimationEnded() then clears
  // its current motion after notifying, which is the motion the new animation
  // has just started: when that animation steps,
  // UpdateCurrentValues() fails CHECK(current_motion_). So the lock waits until
  // the sidebar is collapsed, and callers retry once it is
  // (OnSidebarCollapseSettled()). Not in a closing window either: the
  // browser's animation controller may be gone.
  const auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  auto* tab_strip = browser_view_->tab_strip_view();
  if (IsClosing() || !state || !state->IsCollapsed() || !tab_strip) {
    VLOG(1) << "Koan: sidebar lock refused (collapsing or closing)";
    return nullptr;
  }
  VLOG(1) << "Koan: sidebar lock taken";
  return tab_strip->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
}

bool KoanCompactModeController::PeekSidebar() {
  if (!hides_sidebar()) {
    return false;
  }
  // Zen's cmd_zenCompactModeShowSidebar toggles `zen-user-show`: the sidebar
  // stays out, wherever the pointer is, until the shortcut is used again.
  if (peek_lock_ || peek_requested_) {
    peek_requested_ = false;
    ReleaseLock(peek_lock_);
    return true;
  }
  peek_lock_ = TakeExpandedLock();
  // Refused while the sidebar collapses: taken once it has (the shortcut was
  // used, so the next use unpins).
  peek_requested_ = !peek_lock_ && !IsClosing();
  return true;
}

void KoanCompactModeController::FlashSidebar() {
  if (!flash_lock_) {
    flash_lock_ = TakeExpandedLock();
  }
  // Once it ends, upstream's hover tracking keeps the sidebar open while the
  // pointer is over it.
  flash_timer_.Start(FROM_HERE, kFlashDuration, this,
                     &KoanCompactModeController::EndFlash);
}

void KoanCompactModeController::EndFlash() {
  flash_timer_.Stop();
  ReleaseLock(flash_lock_);
}

void KoanCompactModeController::ReleaseLock(
    std::unique_ptr<ExpandOnHoverLock>& lock) {
  if (lock) {
    VLOG(1) << "Koan: sidebar lock let go" << (IsClosing() ? " (closing)" : "");
  }
  if (IsClosing()) {
    ReleaseExpandOnHoverLockSoon(lock);
    return;
  }
  lock.reset();
}

void KoanCompactModeController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Zen (tabs.mjs _handleTabSelect, flashSidebarIfNecessary): selecting a
  // tab, also a new one, briefly pops up the hidden sidebar, if the user
  // turned that on; not in split view.
  const MultiContentsView* contents = browser_view_->multi_contents_view();
  if (selection.active_tab_changed() && hides_sidebar() &&
      GetPrefs()->GetBoolean(prefs::kCompactModeToolbarFlashPopup) &&
      !(contents && contents->IsInSplitView())) {
    FlashSidebar();
  }
  if (change.type() == TabStripModelChange::kInserted) {
    MaybeShowBackgroundTabToast(tab_strip_model, *change.GetInsert());
  }
}

void KoanCompactModeController::MaybeShowBackgroundTabToast(
    TabStripModel* tab_strip_model,
    const TabStripModelChange::Insert& insert) {
  // Zen (ZenCompactMode.mjs _onTabOpen, zen-general.ftl): a tab a page opens
  // in the background while the sidebar is hidden gets a toast, with a
  // button that switches to it.
  const views::View* region = browser_view_->tab_strip_view();
  const auto* strip = views::AsViewClass<VerticalTabStripRegionView>(region);
  if (!hides_sidebar() || (strip && strip->is_expanded_on_hover())) {
    return;
  }
  for (const TabStripModelChange::ContentsWithIndex& inserted :
       insert.contents) {
    if (inserted.index == tab_strip_model->active_index() ||
        !tab_strip_model->GetOpenerOfTabAt(inserted.index)) {
      continue;
    }
    KoanBrowserViewController* controller =
        KoanBrowserViewController::From(browser_view_);
    if (!controller) {
      return;
    }
    controller->toast()->ShowWithAction(
        kBackgroundTabOpenedToast, kTabIcon, u"Switch to tab",
        base::BindOnce(
            [](base::WeakPtr<tabs::TabInterface> tab,
               TabStripModel* tab_strip) {
              const int index = tab ? tab_strip->GetIndexOfTab(tab.get())
                                    : TabStripModel::kNoTab;
              if (index != TabStripModel::kNoTab) {
                tab_strip->ActivateTabAt(index);
              }
            },
            inserted.tab->GetWeakPtr(), base::Unretained(tab_strip_model)));
    return;
  }
}

void KoanCompactModeController::InstallContextMenu() {
  views::View* region = browser_view_->tab_strip_view();
  if (!region || region->GetClassName() != "VerticalTabStripRegionView" ||
      context_menu_region_.view() == region) {
    return;
  }
  if (!context_menu_controller_) {
    context_menu_controller_ =
        std::make_unique<KoanSidebarContextMenuController>(this);
  }
  region->set_context_menu_controller(context_menu_controller_.get());
  context_menu_region_.SetView(region);
}

void KoanCompactModeController::ShowContextMenu(
    const gfx::Point& screen_point,
    ui::mojom::MenuSourceType source_type) {
  // Zen's toolbar context menu (ZenCompactMode.mjs addContextMenu(),
  // ZenVerticalTabsManager.mjs initRightSideOrderContextMenu()). With single
  // toolbar Zen shows just the toggle, not the submenu. A runner from an
  // earlier menu goes before the models it shows.
  menu_runner_.reset();
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  compact_menu_model_.reset();
  if (DesiredSingleToolbar()) {
    context_menu_model_->AddCheckItem(kCommandCompactModeToggle,
                                      u"Enable compact mode");
  } else {
    compact_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    compact_menu_model_->AddCheckItem(kCommandCompactModeToggle,
                                      u"Enable compact mode");
    compact_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    compact_menu_model_->AddRadioItem(kCommandCompactModeHideSidebar,
                                      u"Hide sidebar", /*group_id=*/0);
    compact_menu_model_->AddRadioItem(kCommandCompactModeHideToolbar,
                                      u"Hide toolbar", /*group_id=*/0);
    compact_menu_model_->AddRadioItem(kCommandCompactModeHideBoth, u"Hide both",
                                      /*group_id=*/0);
    context_menu_model_->AddSubMenu(0, u"Compact Mode",
                                    compact_menu_model_.get());
  }
  context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  context_menu_model_->AddCheckItem(kCommandTabsOnRight, u"Tabs on the right");
  // The sidebar stays out (it may be compact mode's overlay) while its menu
  // is open, as with upstream's menus on it.
  if (auto* tab_strip = browser_view_->tab_strip_view()) {
    menu_lock_ = tab_strip->GetExpandOnHoverLock(
        ExpandOnHoverLockType::kKeepCurrentState);
  }
  menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&KoanCompactModeController::OnContextMenuClosed,
                          weak_factory_.GetWeakPtr()));
  menu_runner_->RunMenuAt(browser_view_->GetWidget(), nullptr,
                          gfx::Rect(screen_point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
  // No menu (e.g. another one was open), or it ran to its end (macOS).
  if (!menu_runner_->IsRunning()) {
    ReleaseLock(menu_lock_);
  }
}

void KoanCompactModeController::OnContextMenuClosed() {
  ReleaseLock(menu_lock_);
}

bool KoanCompactModeController::IsCommandIdChecked(int command_id) const {
  const PrefService* prefs = GetPrefs();
  const bool hide_sidebar = DesiredHideSidebar();
  const bool hide_toolbar = DesiredHideToolbar();
  switch (command_id) {
    case kCommandCompactModeToggle:
      return prefs->GetBoolean(prefs::kCompactModeEnabled);
    case kCommandCompactModeHideSidebar:
      return hide_sidebar && !hide_toolbar;
    case kCommandCompactModeHideToolbar:
      return hide_toolbar && !hide_sidebar;
    case kCommandCompactModeHideBoth:
      return hide_sidebar && hide_toolbar;
    case kCommandTabsOnRight:
      return tabs_on_right_;
    default:
      return false;
  }
}

void KoanCompactModeController::ExecuteCommand(int command_id,
                                               int event_flags) {
  switch (command_id) {
    case kCommandCompactModeToggle:
      ToggleCompactMode();
      break;
    case kCommandCompactModeHideSidebar:
      SetCompactModeHides(/*sidebar=*/true, /*toolbar=*/false);
      break;
    case kCommandCompactModeHideToolbar:
      SetCompactModeHides(/*sidebar=*/false, /*toolbar=*/true);
      break;
    case kCommandCompactModeHideBoth:
      SetCompactModeHides(/*sidebar=*/true, /*toolbar=*/true);
      break;
    case kCommandTabsOnRight:
      ToggleTabsOnRight();
      break;
  }
}

void KoanCompactModeController::OnSidebarDragCollapsed() {
  PrefService* prefs = GetPrefs();
  if (!features::IsCompactModeEnabled() ||
      prefs->GetBoolean(prefs::kCompactModeEnabled)) {
    return;
  }
  // ZenCustomizableUI.sys.mjs #collapseSidebarIntoCompactMode(): compact
  // mode hides the sidebar; it comes back at the width it had.
  if (!PrefHidesSidebar()) {
    SetCompactModeHides(/*sidebar=*/true, /*toolbar=*/true);
  }
  prefs->SetBoolean(prefs::kCompactModeEnabled, true);
  // zen-general.ftl zen-sidebar-drag-collapsed-toast, once, for 5 s, with
  // the compact mode shortcut as the user set it; none without one (Zen).
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  const std::optional<ui::Accelerator> shortcut =
      controller ? controller->shortcuts()->GetAccelerator(
                       shortcuts::Action::kToggleCompactMode)
                 : std::nullopt;
  if (shortcut && !prefs->GetBoolean(prefs::kCompactModeDragCollapseHintSeen)) {
    prefs->SetBoolean(prefs::kCompactModeDragCollapseHintSeen, true);
    controller->toast()->Show(
        u"Sidebar hidden! Use", shortcut->GetShortcutText(),
        u"to bring the sidebar back.", std::u16string(), base::Seconds(5));
  }
}

void KoanCompactModeController::OnDidChangeFocus(views::View* focused_before,
                                                 views::View* focused_now) {
  // Editing the address (the URL pill, Ctrl+L) opens Zen's URL bar for as
  // long as the omnibox has focus.
  KoanUrlBarController* url_bar =
      KoanBrowserViewController::From(browser_view_)->url_bar();
  url_bar->OnFocusChanged(focused_now);
  if (!hides_toolbar() || url_bar->is_open()) {
    return;
  }
  // Focusing anything else in the toolbar (e.g. Alt+F) reveals it; it stays
  // until focus leaves (checked again on each focus change). Not over the
  // page's own fullscreen (Koan is suspended there, see IsContentFullscreen()).
  if (IsContentFullscreen(browser_view_)) {
    return;
  }
  if (focused_now && browser_view_->top_container()->Contains(focused_now)) {
    SetToolbarRevealed(true, /*animate=*/true);
  }
  CheckToolbarHide();
}

void KoanCompactModeController::OnUrlBarOpenChanged() {
  if (mode_ == Mode::kNone) {
    return;
  }
  views::View* top_container = browser_view_->top_container();
  if (IsUrlBarOpen()) {
    // The URL bar card replaces the toolbar row; its layer may have been
    // hidden while the toolbar was slid out.
    toolbar_revealed_ = false;
    toolbar_unwanted_since_.reset();
    toolbar_hide_timer_.Stop();
    UpdatePointerMonitor();
    if (ui::Layer* layer = top_container->layer()) {
      layer->SetVisible(IsTopContainerShown());
    }
    return;
  }
  top_container->SetCanProcessEventsWithinSubtree(toolbar_revealed_);
  ApplyToolbarTransform(/*animate=*/false);
}

bool KoanCompactModeController::IsUrlBarOpen() const {
  auto* controller = KoanBrowserViewController::From(browser_view_);
  return controller && controller->url_bar()->is_open();
}

bool KoanCompactModeController::is_toolbar_away() const {
  return !toolbar_revealed_ && !IsUrlBarOpen();
}

void KoanCompactModeController::OnEvent(const ui::Event& event) {
  // (The application-wide monitor hears the other windows too, and a window
  // that is closing is torn down by the browser's own tasks.)
  if (IsClosing()) {
    return;
  }
  CheckRevealedHides();
  CheckFullscreenEdges();
}

bool KoanCompactModeController::WantsFullscreenEdges() const {
  return mode_ != Mode::kNone && IsWindowFullscreen(browser_view_) &&
         !IsContentFullscreen(browser_view_) &&
         (sidebar_hot_zone_ || toolbar_hot_zone_);
}

void KoanCompactModeController::CheckFullscreenEdges() {
  if (!WantsFullscreenEdges()) {
    if (std::exchange(edge_sidebar_hovered_, false)) {
      OnSidebarHotZoneHovered(false);
    }
    return;
  }
  // The monitor hears the whole application (see UpdatePointerMonitor()):
  // only a pointer over this window counts, not one on another display or
  // over another window of ours.
  const gfx::Point screen_pointer =
      display::Screen::Get()->GetCursorScreenPoint();
  if (!browser_view_->GetBoundsInScreen().Contains(screen_pointer)) {
    // (The hover lock is let go of, not just forgotten: the pointer can leave
    // for another display straight from the edge.)
    if (std::exchange(edge_sidebar_hovered_, false)) {
      OnSidebarHotZoneHovered(false);
    }
    return;
  }
  gfx::Point pointer = screen_pointer;
  views::View::ConvertPointFromScreen(browser_view_, &pointer);
  const gfx::Rect bounds = browser_view_->GetLocalBounds();
  // Nothing slides in under a menu or a tab drag in progress.
  const bool busy = TabDragController::IsActive() ||
                    views::MenuController::GetForOwnerWidget(
                        browser_view_->GetWidget()) != nullptr;
  if (sidebar_hot_zone_) {
    const bool at_edge =
        IsSidebarOnRight(browser_view_)
            ? pointer.x() >= bounds.right() - kFullscreenEdgeSize
            : pointer.x() < bounds.x() + kFullscreenEdgeSize;
    // (Only when it would change: the window under the pointer is looked up
    // for that.)
    const bool hovered = at_edge && !busy && pointer.y() >= bounds.y() &&
                         pointer.y() < bounds.bottom() &&
                         IsPointerOverWindow(browser_view_);
    if (hovered != edge_sidebar_hovered_) {
      VLOG(1) << "Koan: fullscreen sidebar edge "
              << (hovered ? "hovered" : "left") << " at " << pointer.x() << ","
              << pointer.y();
      edge_sidebar_hovered_ = hovered;
      OnSidebarHotZoneHovered(hovered);
    }
  }
  if (toolbar_hot_zone_ && !busy && pointer.y() >= bounds.y() &&
      pointer.y() < bounds.y() + kFullscreenEdgeSize &&
      pointer.x() >= bounds.x() && pointer.x() < bounds.right() &&
      IsPointerOverWindow(browser_view_)) {
    OnToolbarHotZoneHovered(true);
  }
}

KoanCompactModeController::Mode KoanCompactModeController::DesiredMode() const {
  // (Also in macOS' native fullscreen, where the content card stays, see
  // UseContentCard(): Chromium moves the top container to its own overlay
  // widget then, and Koan takes it back, see OnImmersiveFullscreenChanged().
  // Elsewhere fullscreen has no content card.)
  if (!CanCompactMode()) {
    return Mode::kNone;
  }
  const PrefService* prefs = GetPrefs();
  if (prefs->GetBoolean(prefs::kCompactModeEnabled)) {
    return Mode::kCompact;
  }
  if (DesiredSingleToolbar()) {
    return Mode::kSingleToolbar;
  }
  return Mode::kNone;
}

bool KoanCompactModeController::DesiredSingleToolbar() const {
  return GetPrefs()->GetBoolean(prefs::kSingleToolbarEnabled);
}

bool KoanCompactModeController::DesiredHideSidebar() const {
  return DesiredSingleToolbar() || PrefHidesSidebar() || !DesiredHideToolbar();
}

bool KoanCompactModeController::DesiredHideToolbar() const {
  // Single toolbar has no toolbar to show.
  return DesiredSingleToolbar() || PrefHidesToolbar();
}

void KoanCompactModeController::CancelSpaceSwipe() {
  // A swipe between Spaces drags the sidebar's content: the sidebar going
  // away or coming back under the fingers ends it.
  if (auto* controller = KoanBrowserViewController::From(browser_view_);
      controller && controller->workspaces()) {
    controller->workspaces()->CancelSwipe(/*animate=*/false);
  }
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
       (DesiredSingleToolbar() == compact_keeps_sidebar_nav_ &&
        DesiredHideSidebar() == hide_sidebar_ &&
        DesiredHideToolbar() == hide_toolbar_))) {
    // The URL bar behaviour pref may have changed.
    KoanBrowserViewController::From(browser_view_)->url_bar()->OnModeChanged();
    SyncSidebar();
    return;
  }
  if (mode_ != Mode::kNone) {
    Deactivate();
  }
  if (desired != Mode::kNone) {
    Activate(desired);
  }
  UpdateFullscreenTopChrome();
  OnContentCardChanged();
  KoanBrowserViewController::From(browser_view_)->url_bar()->OnModeChanged();
  SyncSidebar();
}

void KoanCompactModeController::SyncSidebar() {
  // The sidebar is collapsed while compact mode hides it, and only then: the
  // one rule behind which the strip's state is Koan's (see
  // KoanVerticalTabsPolicy). A hidden sidebar that stays hidden while the
  // variant changes is not expanded in between.
  KoanVerticalTabsPolicy* policy =
      KoanBrowserViewController::From(browser_view_)->vertical_tabs();
  policy->SetSidebarHidden(hides_sidebar());
  policy->UpdateSidebarButtonText();
}

void KoanCompactModeController::OnContentCardChanged() {
  // The contents area may keep its bounds while the card inside it moves, so
  // lay it out explicitly; Glance sits over the card, the welcome screen
  // below the window buttons' strip.
  if (MultiContentsView* contents = browser_view_->multi_contents_view()) {
    contents->InvalidateLayout();
  }
  browser_view_->InvalidateLayout();
  LayoutHotZones();
  if (auto* controller = KoanBrowserViewController::From(browser_view_)) {
    controller->glance()->OnBrowserViewLayout();
    controller->welcome()->OnBrowserViewLayout();
  }
}

void KoanCompactModeController::Activate(Mode mode) {
  CancelSpaceSwipe();
  // What compact mode hides.
  const bool single_toolbar = DesiredSingleToolbar();
  hide_sidebar_ = mode == Mode::kCompact && DesiredHideSidebar();
  hide_toolbar_ = mode == Mode::kCompact && DesiredHideToolbar();
  mode_ = mode;
  compact_keeps_sidebar_nav_ = mode == Mode::kCompact && single_toolbar;
  VLOG(1) << (mode == Mode::kCompact ? "Koan: compact mode on"
                                     : "Koan: single toolbar on")
          << (hide_sidebar_ ? ", sidebar hidden" : "")
          << (hide_toolbar_ && !compact_keeps_sidebar_nav_ ? ", toolbar hidden"
                                                           : "");

  // Toolbar: move the top container into the overlay view, above the page.
  // Mirrors BrowserView::ReparentTopContainerForStartOfImmersive(); the
  // layout positions a top container that isn't parented to the BrowserView.
  views::View* top_container = browser_view_->top_container();
  ParkTopContainer();
  if (can_reveal_toolbar()) {
    caption_buttons_targeter_ =
        std::make_unique<KoanCaptionButtonsTargeter>(browser_view_);
    top_container->SetEventTargeter(
        std::make_unique<views::ViewTargeter>(caption_buttons_targeter_.get()));
  }
  if (hides_toolbar()) {
    SetToolbarRevealed(false, /*animate=*/false);
    // Zen: no strip for the window buttons; they appear at the top edge.
    // Zen's macOS lights are always there, though: with the sidebar on the
    // right (not in its top row) they are over the page's corner, so the
    // strip with them stays open.
    lights_keep_strip_open_ =
        traffic_lights_ && IsSidebarOnRight(browser_view_) && !hides_sidebar();
    caption_strip_collapsible_ = !lights_keep_strip_open_;
    caption_strip_revealed_ = false;
  }
  if (mode == Mode::kSingleToolbar || compact_keeps_sidebar_nav_) {
    InstallSidebarNav();
  }

  // Edge hot zones, below the tab strip so the expanded strip gets the
  // pointer, above the page.
  std::optional<size_t> index;
  if (auto* tab_strip = browser_view_->tab_strip_view()) {
    index = browser_view_->GetIndexOf(tab_strip);
  }
  const size_t insert_at = index.value_or(browser_view_->children().size());
  const auto add_hot_zone =
      [&](void (KoanCompactModeController::*on_hover)(bool)) {
        return browser_view_->AddChildViewAt(
            std::make_unique<KoanHotZone>(
                base::BindRepeating(on_hover, base::Unretained(this))),
            insert_at);
      };
  if (hide_sidebar_) {
    sidebar_hot_zone_ =
        add_hot_zone(&KoanCompactModeController::OnSidebarHotZoneHovered);
  }
  // The top edge reveals the toolbar (with the window buttons), or, with
  // single toolbar, just the window buttons.
  if (can_reveal_toolbar()) {
    toolbar_hot_zone_ =
        add_hot_zone(&KoanCompactModeController::OnToolbarHotZoneHovered);
  } else if (caption_strip_collapsible_) {
    caption_hot_zone_ =
        add_hot_zone(&KoanCompactModeController::OnCaptionHotZoneHovered);
  }
  UpdateCaptionButtons();
  UpdatePointerMonitor();
  if (hides_toolbar() && !bubble_fallback_anchor_) {
    auto anchor = std::make_unique<views::View>();
    anchor->SetCanProcessEventsWithinSubtree(false);
    bubble_fallback_anchor_ = browser_view_->AddChildView(std::move(anchor));
  }

  browser_view_->InvalidateLayout();
  LayoutHotZones();
}

void KoanCompactModeController::ParkTopContainer() {
  views::View* top_container = browser_view_->top_container();
  views::View* overlay = GetTopContainerOverlayView(browser_view_);
  if (!hides_toolbar() || !overlay || top_container->parent() == overlay) {
    return;
  }
  // Where it goes back to (not from Chromium's overlay widget, whose
  // fullscreen ends with it back in the BrowserView, and so another park).
  if (top_container->parent() == browser_view_) {
    top_container_index_ = browser_view_->GetIndexOf(top_container);
  }
  top_container->SetPaintToLayer();
  top_container->layer()->SetFillsBoundsOpaquely(false);
  top_container->SetProperty(views::kViewDoesNotLayOutChildren, false);
  overlay->AddChildView(top_container);
  overlay->SetVisible(true);
}

bool KoanCompactModeController::IsClosing() const {
  // One answer for the window: KoanVerticalTabsPolicy::IsClosing().
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return !controller || controller->vertical_tabs()->IsClosing();
}

void KoanCompactModeController::ClearHoverStates() {
  // A closing window is torn down by the browser's own tasks, and
  // BrowserDidClose (which reaches here through the immersive mode controller)
  // comes first: nothing to clear, and releasing a lock now starts a collapse
  // animation just before the animation controller is destroyed.
  if (IsClosing()) {
    return;
  }
  // Nothing stays revealed from before the change (Zen's sizemodechange).
  edge_sidebar_hovered_ = false;
  hover_release_timer_.Stop();
  if (toolbar_revealed_) {
    SetToolbarRevealed(false, /*animate=*/false);
  }
  // The locks are let go of after this call, and not at all if the window is
  // closing by then (it can start closing in between): letting a lock go
  // collapses the sidebar, an animation.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanCompactModeController> self) {
                       if (self && !self->IsClosing()) {
                         self->ReleaseLock(self->hover_lock_);
                         self->ReleaseLock(self->peek_lock_);
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void KoanCompactModeController::OnFullscreenStateChanged() {
  // (A posted notification: it can arrive after the window started closing.)
  if (IsClosing() || mode_ == Mode::kNone) {
    return;
  }
  ClearHoverStates();
  UpdatePointerMonitor();
  // The page's own fullscreen drops the card (see IsContentFullscreen()): lay
  // the contents out again (its corners follow the layout).
  OnContentCardChanged();
}

void KoanCompactModeController::OnImmersiveFullscreenChanged() {
  if (IsClosing() || mode_ == Mode::kNone) {
    return;
  }
  ClearHoverStates();
  // The URL bar lets go of the top container before it moves: Chromium has
  // just destroyed the layer it styled (leaving fullscreen), or the bar's
  // card would end up in the wrong window state.
  if (auto* controller = KoanBrowserViewController::From(browser_view_)) {
    controller->url_bar()->EndEditing();
  }
  // (Native fullscreen has no strip for window buttons, and no lights over
  // the page: AppKit's come with the menu bar.)
  SetCaptionStripRevealed(false);
  UpdatePointerMonitor();
  if (!hides_toolbar()) {
    return;
  }
  // Chromium has just put the top container where it reveals it from the top
  // edge (or taken it out of fullscreen, destroying its layer). Hide it
  // again, as when the mode was activated.
  ParkTopContainer();
  browser_view_->top_container()->SetCanProcessEventsWithinSubtree(
      toolbar_revealed_);
  ApplyToolbarTransform(/*animate=*/false);
  browser_view_->InvalidateLayout();
}

void KoanCompactModeController::UpdateFullscreenTopChrome() {
#if BUILDFLAG(IS_MAC)
  if (!IsWindowFullscreen(browser_view_)) {
    return;
  }
  // Dialogs and bubbles anchor to the window where Koan's toolbar is, not to
  // Chromium's overlay widget (which holds its toolbar, and is out of sight
  // while nothing is revealed).
  if (MultiContentsView* contents = browser_view_->multi_contents_view()) {
    views::Widget* anchor = HidesFullscreenTopChrome(browser_view_)
                                ? nullptr
                                : browser_view_->overlay_widget();
    contents->SetProperty(views::kWidgetForAnchoringKey, anchor);
  }
  // AppKit's own title bar: nothing of it at the top edge but the menu bar
  // while Koan hides its toolbar, else it has the lights, as usual.
  if (HidesFullscreenTopChrome(browser_view_)) {
    HideFullscreenTitlebar(browser_view_);
  } else {
    RestoreFullscreenTitlebar(browser_view_);
    SetTrafficLightsShown(browser_view_, true);
  }
  // The toolbar style: "none" while Koan hides its toolbar, else the user's.
  if (BrowserWidget* widget = browser_view_->browser_widget()) {
    if (BrowserFrameView* frame = widget->GetFrameView()) {
      frame->UpdateFullscreenTopUI();
    }
  }
#endif
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

views::View* KoanCompactModeController::GetBubbleAnchor() {
  if (IsClosing() || !hides_toolbar()) {
    return nullptr;
  }
  // (Called from layout, and for anchors that never get a bubble: it only
  // asks for a look at what is open a moment later.)
  ScheduleBubbleCheck();
  // Compact mode hides just the toolbar: it comes in under the bubble (see
  // ReconcileBubbleHold()) and the anchor is upstream's.
  if (can_reveal_toolbar()) {
    return nullptr;
  }
  if (KoanSidebarNavView* nav = sidebar_nav()) {
    if (views::View* anchor = nav->bubble_anchor()) {
      return anchor;
    }
  }
  // The sidebar is hidden (compact mode keeps its header): the buttons.
  views::View* buttons = sidebar_nav_buttons_.view();
  if (buttons && buttons->IsDrawn() && !buttons->GetVisibleBounds().IsEmpty()) {
    return buttons;
  }
  // Neither is on screen: a point that is, never the slid-away toolbar.
  return bubble_fallback_anchor_;
}

namespace {

// The open bubbles of `browser_view`'s window, with their anchors.
std::vector<std::pair<views::Widget*, const views::View*>> OpenBubbles(
    BrowserView* browser_view) {
  std::vector<std::pair<views::Widget*, const views::View*>> bubbles;
  views::Widget* widget = browser_view->GetWidget();
  if (!widget) {
    return bubbles;
  }
  for (views::Widget* owned :
       views::Widget::GetAllOwnedWidgets(widget->GetNativeView())) {
    const views::BubbleDialogDelegate* bubble =
        owned->IsVisible() && owned->widget_delegate()
            ? owned->widget_delegate()->AsBubbleDialogDelegate()
            : nullptr;
    if (const views::View* anchor =
            bubble ? bubble->GetAnchorView() : nullptr) {
      bubbles.emplace_back(owned, anchor);
    }
  }
  return bubbles;
}

}  // namespace

bool KoanCompactModeController::HasBubbleAnchoredToToolbar() const {
  const views::View* top_container = browser_view_->top_container();
  for (const auto& [widget, anchor] : OpenBubbles(browser_view_)) {
    if (top_container && top_container->Contains(anchor)) {
      return true;
    }
  }
  return false;
}

void KoanCompactModeController::HoldSidebarForBubble() {
  if (IsClosing() || !hides_sidebar()) {
    return;
  }
  // (Not while the sidebar collapses: OnSidebarCollapseSettled() reconciles
  // again when it has.)
  if (!bubble_lock_) {
    bubble_lock_ = TakeExpandedLock();
  }
  // The bubble is open when the checks run, and the lock goes when it closes.
  ScheduleBubbleCheck();
}

void KoanCompactModeController::ScheduleBubbleCheck() {
  // The bubble may be shown a moment after its anchor was asked for (a
  // permission chip waits for its anchor). Anchors are asked on every layout
  // while a prompt is pending (a live resize): one run of checks, not one per
  // ask. A first check not yet run covers this ask too; a run further along
  // starts again.
  if (bubble_check_timer_.IsRunning() && bubble_check_step_ == 0) {
    return;
  }
  bubble_check_step_ = 0;
  bubble_check_timer_.Start(FROM_HERE, base::TimeDelta(), this,
                            &KoanCompactModeController::RunBubbleCheck);
}

void KoanCompactModeController::RunBubbleCheck() {
  ReconcileBubbleHold();
  // 150 ms after the first check, 600 ms after the start.
  if (bubble_check_step_ < 2) {
    bubble_check_timer_.Start(
        FROM_HERE, base::Milliseconds(bubble_check_step_ == 0 ? 150 : 450),
        this, &KoanCompactModeController::RunBubbleCheck);
    ++bubble_check_step_;
  }
}

void KoanCompactModeController::ReconcileBubbleHold() {
  if (IsClosing()) {
    return;
  }
  views::View* top_container = browser_view_->top_container();
  const KoanSidebarNavView* nav = sidebar_nav();
  bool on_toolbar = false;
  bool on_peeked_sidebar = false;
  std::vector<views::Widget*> bubbles;
  for (const auto& [widget, anchor] : OpenBubbles(browser_view_)) {
    const bool toolbar =
        can_reveal_toolbar() && top_container->Contains(anchor);
    const bool sidebar = hides_sidebar() && nav && nav->Contains(anchor);
    if (!toolbar && !sidebar) {
      continue;
    }
    on_toolbar |= toolbar;
    on_peeked_sidebar |= sidebar;
    bubbles.push_back(widget);
    // To be told when it goes away (or is hidden), to let go again.
    if (!bubble_widgets_.IsObservingSource(widget)) {
      bubble_widgets_.AddObservation(widget);
    }
  }
  ui::Layer* toolbar_layer = top_container->layer();
  if (on_toolbar &&
      (!toolbar_revealed_ ||
       (toolbar_layer && toolbar_layer->GetAnimator()->is_animating()))) {
    // The bubble was placed against the toolbar's place off the window (or
    // against its place part way through sliding in), and an anchor follows
    // its view's bounds, not a compositor animation of its layer: bring the
    // toolbar in at once and place the bubbles again.
    if (toolbar_layer) {
      toolbar_layer->GetAnimator()->AbortAllAnimations();
    }
    SetToolbarRevealed(true, /*animate=*/false);
    for (views::Widget* widget : bubbles) {
      if (views::BubbleDialogDelegate* bubble =
              widget->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->OnAnchorBoundsChanged();
      }
    }
  }
  // The peeked sidebar stays out as long as a bubble points at its pill (Zen
  // holds it with the bubble's has-popup-menu).
  if (on_peeked_sidebar) {
    if (!bubble_lock_) {
      bubble_lock_ = TakeExpandedLock();
    }
  } else if (bubble_lock_) {
    ReleaseLock(bubble_lock_);
  }
  // None open: the toolbar goes after its usual delay.
  if (!on_toolbar) {
    CheckToolbarHide();
  }
}

void KoanCompactModeController::OnWidgetDestroying(views::Widget* widget) {
  if (bubble_widgets_.IsObservingSource(widget)) {
    bubble_widgets_.RemoveObservation(widget);
  }
  // (A moment later: it is still there, and open, right now.)
  ScheduleBubbleCheck();
}

void KoanCompactModeController::OnWidgetVisibilityChanged(views::Widget* widget,
                                                          bool visible) {
  if (!visible) {
    ScheduleBubbleCheck();
  }
}

KoanSidebarNavView* KoanCompactModeController::sidebar_nav() {
  return static_cast<KoanSidebarNavView*>(sidebar_nav_.view());
}

void KoanCompactModeController::Deactivate() {
  CancelSpaceSwipe();
  VLOG(1) << (mode_ == Mode::kCompact ? "Koan: compact mode off"
                                      : "Koan: single toolbar off");
  // The URL bar lets go of the top container before it moves back.
  KoanBrowserViewController::From(browser_view_)->url_bar()->EndEditing();
  mode_ = Mode::kNone;
  hide_sidebar_ = false;
  hide_toolbar_ = false;
  compact_keeps_sidebar_nav_ = false;
  caption_strip_collapsible_ = false;
  lights_keep_strip_open_ = false;
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
  EndFlash();
  hover_release_timer_.Stop();
  ReleaseLock(hover_lock_);
  ReleaseLock(peek_lock_);
  // (Zen drops `zen-user-show` when compact mode turns off.)
  peek_requested_ = false;
  pointer_monitor_.reset();
  edge_sidebar_hovered_ = false;
  bubble_widgets_.RemoveAllObservations();
  ReleaseLock(bubble_lock_);
  if (bubble_fallback_anchor_) {
    browser_view_->RemoveChildViewT(
        std::exchange(bubble_fallback_anchor_, nullptr));
  }
  if (sidebar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(sidebar_hot_zone_, nullptr));
  }
  if (toolbar_hot_zone_) {
    browser_view_->RemoveChildViewT(std::exchange(toolbar_hot_zone_, nullptr));
  }

  // Mirrors BrowserView::ReparentTopContainerForEndOfImmersive().
  views::View* top_container = browser_view_->top_container();
  if (caption_buttons_targeter_) {
    top_container->SetEventTargeter(nullptr);
    caption_buttons_targeter_.reset();
  }
  bool restore_top_container =
      top_container->parent() != browser_view_ && top_container_index_;
#if BUILDFLAG(IS_MAC)
  // Immersive fullscreen has the top container in its own overlay widget
  // (Koan's overlay holds it in between, see
  // OnImmersiveFullscreenChanged()): leave it there until the fullscreen
  // ends, hide Koan's (now empty) overlay, and undo Koan's own state on the
  // top container: the hidden toolbar's slide out of view and its ignoring
  // the pointer. The layer stays; the overlay widget paints the top
  // container with it.
  const ImmersiveModeController* immersive_controller =
      ImmersiveModeController::From(browser_view_->browser());
  if (views::View* immersive = browser_view_->overlay_widget()
                                   ? browser_view_->overlay_view()
                                   : nullptr;
      immersive && immersive_controller && immersive_controller->IsEnabled()) {
    restore_top_container = false;
    if (top_container->parent() != immersive) {
      immersive->AddChildView(top_container);
      immersive->SetVisible(true);
    }
    if (views::View* overlay = GetTopContainerOverlayView(browser_view_)) {
      overlay->SetVisible(false);
    }
    if (ui::Layer* layer = top_container->layer()) {
      layer->GetAnimator()->AbortAllAnimations();
      layer->SetTransform(gfx::Transform());
      // A hidden toolbar's layer is hidden (HideAwayToolbarLayer()): without
      // this the toolbar stays invisible in the fullscreen toolbar, a blank
      // grey band above the sidebar.
      layer->SetVisible(true);
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

  // A closing window has no layout to do.
  if (IsClosing()) {
    return;
  }

  browser_view_->InvalidateLayout();
}

void KoanCompactModeController::SetToolbarRevealed(bool revealed,
                                                   bool animate) {
  // The open URL bar owns the top container. Over the page's own fullscreen
  // nothing is revealed (Koan is suspended there).
  if (!hides_toolbar() || IsUrlBarOpen() ||
      (revealed && IsContentFullscreen(browser_view_))) {
    return;
  }
  const bool changed = toolbar_revealed_ != revealed;
  toolbar_revealed_ = revealed;
  browser_view_->top_container()->SetCanProcessEventsWithinSubtree(revealed);
  if (changed || !animate) {
    ApplyToolbarTransform(animate);
  }
  if (changed) {
    // The window buttons over the page come and go with the toolbar.
    UpdateCaptionButtons();
  }
  toolbar_unwanted_since_.reset();
  toolbar_hide_timer_.Stop();
  UpdatePointerMonitor();
  CheckToolbarHide();
}

void KoanCompactModeController::ApplyToolbarTransform(bool animate) {
  views::View* top_container = browser_view_->top_container();
  ui::Layer* layer = top_container->layer();
  if (!layer) {
    return;
  }
  gfx::Transform target;
  if (is_toolbar_away()) {
    // Slide fully above the window, including any drop shadow.
    target.Translate(0, -top_container->height());
  } else {
    layer->SetVisible(IsTopContainerShown());
  }
  if (!animate) {
    if (layer->GetAnimator()->is_animating()) {
      return;
    }
    layer->SetTransform(target);
    HideAwayToolbarLayer();
    return;
  }
  // Compositor-driven, so the slide stays smooth even if the UI thread is
  // busy.
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&KoanCompactModeController::HideAwayToolbarLayer,
                              weak_factory_.GetWeakPtr()))
      .SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
      .Once()
      .SetDuration(toolbar_revealed_ ? kToolbarRevealDuration
                                     : kToolbarHideDuration)
      .SetTransform(
          top_container, target,
          toolbar_revealed_ ? gfx::Tween::EASE_OUT : gfx::Tween::EASE_IN);
}

bool KoanCompactModeController::IsTopContainerShown() const {
  // Like views' own layer visibility: the view and its ancestors up to the
  // overlay view, which owns the next layer up.
  const views::View* top_container = browser_view_->top_container();
  return top_container->GetVisible() &&
         (!top_container->parent() || top_container->parent()->GetVisible());
}

void KoanCompactModeController::HideAwayToolbarLayer() {
  // A toolbar slid out of the window still repaints on every URL, title and
  // load change unless its layer is hidden; the views stay visible, so the
  // omnibox can still take focus (Ctrl+L).
  ui::Layer* layer = browser_view_->top_container()->layer();
  if (mode_ != Mode::kNone && is_toolbar_away() && layer) {
    layer->SetVisible(false);
  }
}

bool KoanCompactModeController::ShouldKeepToolbarRevealed() const {
  views::View* top_container = browser_view_->top_container();
  if (top_container->IsMouseHovered() ||
      (toolbar_hot_zone_ && toolbar_hot_zone_->IsMouseHovered())) {
    return true;
  }
  // The window buttons came with it. macOS' are AppKit's, over the window:
  // the pointer on them leaves its views.
  if (traffic_lights_ && IsPointerOverTrafficLights(browser_view_)) {
    return true;
  }
  if (const BrowserFrameView* frame =
          browser_view_->browser_widget()
              ? browser_view_->browser_widget()->GetFrameView()
              : nullptr) {
    for (const views::View* child : frame->children()) {
      if (IsCaptionButton(child) && child->IsMouseHovered()) {
        return true;
      }
    }
  }
  // Or a bubble anchored to it is open (a permission prompt, page info ...).
  if (HasBubbleAnchoredToToolbar()) {
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
    // A menu closing sends the window no event: look again later. Nor does a
    // bubble that a toolbar button opened (not watched, see
    // ReconcileBubbleHold()) closing by keyboard.
    if (views::MenuController::GetForOwnerWidget(browser_view_->GetWidget()) ||
        HasBubbleAnchoredToToolbar()) {
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
      (toolbar_revealed_ || caption_strip_revealed_ || WantsFullscreenEdges());
  views::Widget* widget = browser_view_->GetWidget();
  if (!wanted || !widget) {
    if (pointer_monitor_) {
      VLOG(1) << "Koan: pointer monitor off";
    }
    pointer_monitor_.reset();
    return;
  }
  // At the screen's edge in native fullscreen the event goes to the window
  // that is under the pointer, which is not always the browser window's
  // (another window of the application can be there, as an invisible bubble's
  // or the immersive overlay's), and a monitor of one window then never sees
  // it: watch the whole application there, and decide by the pointer's place
  // (CheckFullscreenEdges()).
  const bool application_wide = WantsFullscreenEdges();
  if (pointer_monitor_ &&
      pointer_monitor_is_application_wide_ == application_wide) {
    return;
  }
  const std::set<ui::EventType> types = {
      ui::EventType::kMouseMoved, ui::EventType::kMouseDragged,
      ui::EventType::kMouseEntered, ui::EventType::kMouseExited};
  pointer_monitor_is_application_wide_ = application_wide;
  pointer_monitor_ = application_wide
                         ? views::EventMonitor::CreateApplicationMonitor(
                               this, widget->GetNativeWindow(), types)
                         : views::EventMonitor::CreateWindowMonitor(
                               this, widget->GetNativeWindow(), types);
  VLOG(1) << "Koan: pointer monitor on ("
          << (application_wide ? "application wide" : "this window") << ")";
}

void KoanCompactModeController::CheckRevealedHides() {
  CheckToolbarHide();
  CheckCaptionStripHide();
}

void KoanCompactModeController::OnSidebarHotZoneHovered(bool hovered) {
  if (mode_ == Mode::kNone || IsContentFullscreen(browser_view_)) {
    VLOG(1) << "Koan: sidebar hot zone ignored (mode "
            << static_cast<int>(mode_) << ")";
    return;
  }
  VLOG(1) << "Koan: sidebar hot zone " << (hovered ? "hovered" : "left")
          << (hover_lock_ ? ", holding" : "");
  if (hovered) {
    hover_release_timer_.Stop();
    if (!hover_lock_) {
      hover_lock_ = TakeExpandedLock();
    }
    return;
  }
  // Once the strip has slid over the hot zone the pointer is inside the
  // strip, and upstream's hover tracking keeps it open until it leaves; the
  // lock is kept for Zen's keep-hover time, so the hand-over between the two
  // doesn't close it for a moment.
  hover_release_timer_.Start(FROM_HERE, kSidebarKeepHover,
                             base::BindOnce(
                                 [](KoanCompactModeController* self) {
                                   self->ReleaseLock(self->hover_lock_);
                                 },
                                 base::Unretained(this)));
}

void KoanCompactModeController::OnToolbarHotZoneHovered(bool hovered) {
  if (hovered && !IsContentFullscreen(browser_view_)) {
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
  if (traffic_lights_ && IsPointerOverTrafficLights(browser_view_)) {
    wanted = true;
  }
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

bool KoanCompactModeController::IsCaptionButtonBesideSidebar(
    const views::View* button) const {
  // Zen (ZenVerticalTabsManager.mjs _updateEvent) keeps the window buttons in
  // the sidebar's top row when they are on its side; the row keeps room for
  // them (see the browser_view_tabbed_layout_impl.cc hook). A hidden sidebar
  // has no row.
  if (hides_sidebar() || !button->parent()) {
    return false;
  }
  const bool button_on_right =
      button->bounds().CenterPoint().x() > button->parent()->width() / 2;
  return button_on_right == IsSidebarOnRight(browser_view_);
}

bool KoanCompactModeController::HasPageSideCaptionButtons() const {
  // (Native fullscreen on macOS: no buttons over the page.)
  if (IsWindowFullscreen(browser_view_)) {
    return false;
  }
  // The traffic lights are at the window's leading edge: over the page where
  // the sidebar is hidden or on the right.
  if (traffic_lights_) {
    return hides_sidebar() || IsSidebarOnRight(browser_view_);
  }
  const BrowserWidget* widget = browser_view_->browser_widget();
  const BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return false;
  }
  for (const views::View* child : frame->children()) {
    if (IsCaptionButton(child) && child->GetVisible() &&
        !IsCaptionButtonBesideSidebar(child)) {
      return true;
    }
  }
  return false;
}

void KoanCompactModeController::UpdateCaptionButtons() {
  if (IsClosing()) {
    // The window is going away: nothing to show or hide.
    return;
  }
#if BUILDFLAG(IS_MAC)
  if (traffic_lights_) {
    // AppKit shows the lights by itself in fullscreen, with the menu bar;
    // they are laid out again when it ends.
    if (IsWindowFullscreen(browser_view_)) {
      return;
    }
    // The traffic lights are AppKit's: shown or hidden, as the other
    // platforms' buttons fade, and on the sidebar's side (the leading one)
    // for as long as the sidebar is there, as in Zen, where they are part of
    // it.
    const bool page_side_shown = !caption_strip_collapsible_ ||
                                 caption_strip_revealed_ ||
                                 (can_reveal_toolbar() && toolbar_revealed_);
    // (With single toolbar the lights are in the sidebar's header, so they go
    // and come with the sidebar: hidden with it, not drawn over the page.)
    SetTrafficLightsShown(
        browser_view_, page_side_shown || (!IsSidebarOnRight(browser_view_) &&
                                           traffic_lights_->IsSidebarShown()));
    return;
  }
#endif
  BrowserWidget* widget = browser_view_->browser_widget();
  BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return;
  }
  // The strip above the page is open, or (compact mode) the revealed toolbar
  // has room for the buttons.
  const bool page_side_shown = !caption_strip_collapsible_ ||
                               caption_strip_revealed_ ||
                               (can_reveal_toolbar() && toolbar_revealed_);
  // The frame sets the caption buttons' visibility in its layout, so fade
  // them on a layer instead and let a hidden one ignore the pointer.
  for (views::View* child : frame->children()) {
    if (!IsCaptionButton(child)) {
      continue;
    }
    const bool shown = page_side_shown || IsCaptionButtonBesideSidebar(child);
    child->SetCanProcessEventsWithinSubtree(shown);
    if (!child->layer()) {
      if (shown) {
        continue;
      }
      child->SetPaintToLayer();
      child->layer()->SetFillsBoundsOpaquely(false);
      child->layer()->SetOpacity(1.0f);
    }
    // The revealed toolbar is in the overlay, above the frame: keep the
    // buttons above it.
    if (ui::Layer* toolbar_layer = browser_view_->top_container()->layer();
        can_reveal_toolbar() && toolbar_layer &&
        toolbar_layer->parent() == child->layer()->parent()) {
      toolbar_layer->parent()->StackAbove(child->layer(), toolbar_layer);
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
  // The page's own fullscreen covers the window: no zone takes the pointer.
  if (IsContentFullscreen(browser_view_)) {
    for (KoanHotZone* zone : {sidebar_hot_zone_.get(), toolbar_hot_zone_.get(),
                              caption_hot_zone_.get()}) {
      if (zone) {
        zone->SetBoundsRect(gfx::Rect());
      }
    }
    return;
  }
  const gfx::Rect bounds = browser_view_->GetLocalBounds();
  const gfx::Insets gutter = ContentCardInsets(browser_view_);
  // The fixed point for bubbles: at the end of the top row, off the lights.
  if (bubble_fallback_anchor_) {
    const int row = GetHiddenToolbarRowHeight(
        GetCaptionButtonAreaSize(browser_view_).height());
    constexpr int kWidth = 24;
    constexpr int kInset = 72;
    bubble_fallback_anchor_->SetBoundsRect(gfx::Rect(
        IsSidebarOnRight(browser_view_) ? bounds.right() - kInset - kWidth
                                        : bounds.x() + kInset,
        row - 2, kWidth, 2));
  }
  // Along the hidden sidebar's edge, where it slides in.
  if (sidebar_hot_zone_) {
    sidebar_hot_zone_->SetBoundsRect(
        IsSidebarOnRight(browser_view_)
            ? gfx::Rect(bounds.right() - gutter.right(), bounds.y(),
                        gutter.right(), bounds.height())
            : gfx::Rect(bounds.x(), bounds.y(), gutter.left(),
                        bounds.height()));
  }
  // Along the top edge, above the page (beside a visible sidebar, whose top
  // row has its own buttons).
  const views::View* page = browser_view_->multi_contents_view();
  const gfx::Rect top_edge =
      page ? gfx::Rect(page->x(), bounds.y(), page->width(), gutter.top())
           : gfx::Rect(bounds.x(), bounds.y(), bounds.width(), gutter.top());
  for (KoanHotZone* zone : {toolbar_hot_zone_.get(), caption_hot_zone_.get()}) {
    if (zone) {
      zone->SetBoundsRect(top_edge);
    }
  }
}

}  // namespace koan
