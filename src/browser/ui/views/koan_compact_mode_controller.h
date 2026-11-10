// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "koan/browser/ui/views/koan_sidebar_animation.h"
#include "koan/browser/ui/views/koan_window_edges.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/events/event_observer.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class PrefService;
class ExpandOnHoverLock;

namespace gfx {
class Point;
}  // namespace gfx

namespace views {
class EventMonitor;
class MenuRunner;
}  // namespace views

namespace koan {

class KoanCaptionButtonsTargeter;
class KoanHotZone;
class KoanOutsideWindowTracker;
class KoanSidebarContextMenuController;
class KoanSidebarNavButtons;
class KoanSidebarNavView;
class KoanTrafficLightsController;

// Zen's compact mode and layouts for one window.
//
// Single toolbar (Zen's default layout; Ctrl+Alt+B, pref koan.single_toolbar):
// there is no top toolbar. The top container is hidden in the overlay
// (below), the page card reaches the top of the window, and
// KoanSidebarNavView at the top of the sidebar has the navigation buttons
// and a URL pill. The sidebar is always expanded: Zen's "Collapsed Sidebar"
// layout (icons only, a toolbar above the page) is not offered for now, and
// Chromium's icon rail never shows (KoanVerticalTabsPolicy).
//
// Compact mode (Ctrl+S like Zen, pref koan.compact_mode.enabled) hides the
// sidebar, the toolbar or both (Zen's "Compact Mode" submenu; with single
// toolbar there is no toolbar, so just the sidebar):
// - a hidden sidebar (vertical tab strip) is collapsed to zero width and
//   slides over the page (upstream's expand-on-hover overlay) when the
//   pointer reaches its edge;
// - a hidden toolbar: the top container (toolbar, bookmarks bar) moves into
//   the BrowserView's overlay view, above the page, and slides down with a
//   compositor transform when the pointer reaches the top edge;
// - the page is a card with a gutter on every side something is hidden; the
//   edge hot zones live in that gutter, outside the page's native window.
//
// Wherever the toolbar is hidden, so is the strip with the frame's caption
// buttons (Zen's window buttons). Buttons above the sidebar's top row stay
// there; the others show while the pointer is at the top edge: the strip
// opens above the page, or, where compact mode hid the toolbar, they come
// with the revealed toolbar.
//
// In both, the omnibox opens out of the hidden toolbar as Zen's URL bar
// (KoanUrlBarController) while it has focus.
//
// Both go on in macOS' native fullscreen, as in Zen: Chromium's immersive
// mode would move the top container to its own overlay widget and reveal it
// at the top edge (with a title bar strip), so Koan takes it back and the
// fullscreen toolbar style is "none" (HidesFullscreenTopChrome()); only the
// menu bar appears there.
//
// Tabs on the right (pref koan.tabs.on_right) mirrors the window: see
// IsSidebarOnRight() in koan_compact_mode.h.
//
// All states are profile prefs shared by all windows. Keyboard shortcuts
// live in KoanShortcutController; right-clicking the sidebar opens Zen's
// toolbar context menu (Compact Mode, Tabs on the right).
class KoanCompactModeController : public views::FocusChangeListener,
                                  public views::WidgetObserver,
                                  public ui::EventObserver,
                                  public TabStripModelObserver,
                                  public ui::SimpleMenuModel::Delegate {
 public:
  explicit KoanCompactModeController(BrowserView* browser_view);
  KoanCompactModeController(const KoanCompactModeController&) = delete;
  KoanCompactModeController& operator=(const KoanCompactModeController&) =
      delete;
  ~KoanCompactModeController() override;

  enum class Mode { kNone, kCompact, kSingleToolbar };

  Mode mode() const { return mode_; }
  // Compact mode hides the sidebar: it takes no layout space.
  bool hides_sidebar() const {
    return mode_ == Mode::kCompact && hide_sidebar_;
  }
  // Compact mode or single toolbar: the top toolbar takes no layout space.
  // Compact mode with single toolbar: the sidebar is hidden but its header
  // (toggle, navigation buttons) stays, in the window's top row.
  bool keeps_sidebar_nav() const { return compact_keeps_sidebar_nav_; }
  bool hides_toolbar() const {
    return mode_ == Mode::kSingleToolbar ||
           (mode_ == Mode::kCompact && hide_toolbar_);
  }
  bool is_toolbar_revealed() const { return toolbar_revealed_; }
  // The caption-button strip above the page is collapsed (the card starts at
  // the gutter).
  bool caption_strip_collapsed() const;
  // The "Tabs on the right" setting. Where the sidebar is, is
  // IsSidebarOnRight() (koan_compact_mode.h), which also checks the window.
  bool tabs_on_right() const { return tabs_on_right_; }

  // The sidebar's URL pill, when it is mounted (single toolbar / compact
  // mode with the header kept). KoanUrlBarController edits over it.
  KoanSidebarNavView* sidebar_nav();

  // Where a bubble about the page points while Koan hides the toolbar (see
  // GetBubbleAnchorView() in koan_url_bar.h): the pill's site icon, else the
  // header's buttons, else a fixed point at the window's top; null when the
  // toolbar is there (or, with compact mode hiding just the toolbar, when it
  // is to slide in under the bubble). Changes nothing but for scheduling a
  // check of which bubbles are open (ReconcileBubbleHold()): it is called
  // from layout and for bubbles that never open.
  views::View* GetBubbleAnchor();

  // A bubble is about to open at the peeked sidebar's pill (Koan's own page
  // info button, which doesn't ask GetBubbleAnchor()): hold the sidebar out
  // from now, before it opens, so the pill the bubble points at stays, and
  // let go again when the bubble is gone (ReconcileBubbleHold()). Does
  // nothing unless compact mode hides the sidebar.
  void HoldSidebarForBubble();

  // Called by KoanUrlBarController when the omnibox is lifted out of the
  // toolbar or put back.
  void OnUrlBarOpenChanged();

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();
  // The sidebar finished collapsing or expanding (from the vertical tabs
  // policy, in a task of its own): locks that had to wait for it are taken.
  void OnSidebarCollapseSettled();

  // Flip the profile prefs.
  void ToggleCompactMode();
  void ToggleSingleToolbar();
  void ToggleTabsOnRight();
  // Zen's "Hide sidebar" / "Hide toolbar" / "Hide both": what compact mode
  // hides. Like Zen, this doesn't turn compact mode on.
  void SetCompactModeHides(bool sidebar, bool toolbar);

  // Compact mode: Zen's Ctrl+Alt+S slides the hidden sidebar out and keeps it
  // there until it is used again. Returns false outside compact mode.
  bool PeekSidebar();

  // A drag of the sidebar's edge collapsed it: like Zen, hide it in compact
  // mode instead, and say once how to bring it back.
  // Z50: a tab drag runs from the sidebar: it stays out until the drag ends
  // (Zen's _isTabBeingDragged ignores the hover leave), wherever the pointer
  // goes. Called on each of the drag's pointer events in the sidebar.
  void HoldSidebarForTabDrag();
  void OnSidebarDragCollapsed();

  // Zen's toolbar context menu, at `screen_point`.
  void ShowContextMenu(const gfx::Point& screen_point,
                       ui::mojom::MenuSourceType source_type);

  // Compact mode hides the toolbar but not the sidebar: the revealed toolbar
  // goes beside the sidebar, as Zen's navbar does.
  bool reveals_toolbar_beside_sidebar() const {
    return can_reveal_toolbar() && !hides_sidebar();
  }

  // views::WidgetObserver (a bubble that holds the toolbar or the sidebar
  // out):
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

  // views::FocusChangeListener:
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // The original profile's prefs (off the record windows share them).
  PrefService* GetPrefs() const;
  // What the pref says compact mode hides.
  bool PrefHidesSidebar() const;
  bool PrefHidesToolbar() const;
  Mode DesiredMode() const;
  // Single toolbar is on (the pref; the sidebar is always expanded unless
  // compact mode hides it).
  bool DesiredSingleToolbar() const;
  // What compact mode hides, from the prefs and the layout.
  bool DesiredHideSidebar() const;
  bool DesiredHideToolbar() const;
  // Whether compact mode can apply in this window (a normal window with
  // Koan's sidebar, and a content card: not in fullscreen except on macOS).
  bool CanCompactMode() const;
  // The sidebar is collapsed while compact mode hides it, and only then.
  void SyncSidebar();
  void UpdateActiveSoon();
  void CancelSpaceSwipe();
  void UpdateActive();
  void Activate(Mode mode);
  void Deactivate();
  void OnTabsOnRightChanged();

  // Compact mode with the toolbar hidden in the overlay, revealed at the top
  // edge (not single toolbar, which has no toolbar to reveal).
  bool can_reveal_toolbar() const {
    return mode_ == Mode::kCompact && hide_toolbar_ &&
           !compact_keeps_sidebar_nav_;
  }
  void SetToolbarRevealed(bool revealed, bool animate);
  // The toolbar is slid out of the window (neither revealed nor open as the
  // URL bar).
  bool is_toolbar_away() const;
  bool IsUrlBarOpen() const;
  void ApplyToolbarTransform(bool animate);
  // Whether the top container would be drawn if its layer were visible.
  bool IsTopContainerShown() const;
  void HideAwayToolbarLayer();
  bool ShouldKeepToolbarRevealed() const;
  // A bubble (permission prompt, page info, bookmark, download ...) anchored
  // to the revealed toolbar is open.
  bool HasBubbleAnchoredToToolbar() const;
  // Looks (a moment after an anchor was asked for, and when a bubble goes
  // away) at which bubbles are open: with compact mode hiding the toolbar,
  // the toolbar comes in without animation and the bubbles are placed again;
  // anchored to a peeked sidebar's pill, they hold the sidebar out; with
  // none, both go as they would. Not from inside layout.
  void ScheduleBubbleCheck();
  void ReconcileBubbleHold();
  // One check now, one after 150 ms and one after 600 ms (the bubble may open
  // a moment after its anchor was asked for), whatever the number of asks.
  void RunBubbleCheck();
  void CheckToolbarHide();

  // While the toolbar, the caption strip or a peeked sidebar is out, watches
  // the pointer so each hides once it has been away long enough. Nothing
  // runs while they are hidden or while the pointer rests over them.
  void UpdatePointerMonitor();
  void CheckRevealedHides();
  // Zen's native mouse tracker (KoanOutsideWindowTracker): wanted where
  // compact mode hides the sidebar or the toolbar, outside fullscreen.
  bool WantsOutsideTracking() const;
  // The pointer left the window through `edge`: reveals and holds what is on
  // that edge and returns how far outside to track the pointer, or nullopt.
  std::optional<int> OnPointerLeftWindow(WindowEdge edge,
                                         const gfx::Point& point);
  void OnOutsideTrackingEnded(bool back_in_window);
  // Stops tracking and lets go of its hold.
  void EndOutsideTracking();
  // The page's own fullscreen (video) starts or ends, or the window's:
  // Koan's hover state is cleared (Zen's sizemodechange ->
  // _clearAllHoverStates) and the card is laid out again.
  void OnFullscreenStateChanged();
  void ClearHoverStates();
  // The window is closing or gone (its widget is closed, the browser is going
  // to be deleted, or OnBrowserViewDestroying() ran): no UI work, and no
  // letting go of a lock, which starts an animation.
  bool IsClosing() const;
  // Native fullscreen (macOS): the page covers the window, so the pointer at
  // the screen's edge reveals the hidden sidebar and toolbar, watched here
  // instead of by the hot zones.
  bool WantsFullscreenEdges() const;
  void CheckFullscreenEdges();
  // macOS: the traffic lights are shown in the revealed sidebar's top row
  // (tabs on the left), so the pointer on them, which leaves the sidebar's
  // views for AppKit's buttons, keeps it out, as Zen's onLeave does
  // (ZenCompactMode.mjs). Let go of after the keep-hover time once the
  // pointer is off them.
  bool WantsLightsHold() const;
  void CheckLightsHold();
  // Z50: lets go of the tab drag's hold once no tab drag runs.
  void CheckTabDragHold();
  // The sidebar came or went, or the lights were laid out again.
  void OnTrafficLightsChanged();

  // Hides the toolbar (single toolbar, compact mode): moves the top
  // container, if it is not there already, into the overlay view of this
  // window, above the page. Where the BrowserView or macOS' immersive
  // fullscreen has it (see below), takes it from there.
  void ParkTopContainer();
  // macOS: native fullscreen moves the top container into Chromium's overlay
  // widget, which reveals it from the top edge (with a title bar strip).
  // Where Koan hides the toolbar, the top container goes back to Koan's
  // overlay as the fullscreen starts and ends, and Chromium is told to
  // reveal nothing (HidesFullscreenTopChrome()).
  void OnImmersiveFullscreenChanged();
  // Tells Chromium that what Koan hides in fullscreen changed.
  void UpdateFullscreenTopChrome();

  // Single toolbar: adds KoanSidebarNavView to the top of the sidebar.
  void InstallSidebarNav();
  // Right-clicking the sidebar opens ShowContextMenu() (the region view is
  // recreated on tab strip mode changes).
  void InstallContextMenu();
  void OnContextMenuClosed();

  // Zen's flash popup: the hidden sidebar pops up for a moment.
  void FlashSidebar();
  // Zen's `zen-has-empty-tab`: while the selected tab is Zen's empty tab
  // (the one Koan marked, IsEmptyTab() in koan_spaces.h; not any blank page,
  // as Zen keys on `zen-empty-tab`), the sidebar is out and can't hide.
  void UpdateEmptyTabLock(bool snap_reveal = false);
  void MaybeShowBackgroundTabToast(TabStripModel* tab_strip_model,
                                   const TabStripModelChange::Insert& insert);
  void EndFlash();
  // Lets go of `lock`; deferred while the window is being destroyed.
  void ReleaseLock(std::unique_ptr<ExpandOnHoverLock>& lock);
  // A keep-expanded lock on the sidebar, or null: not while the sidebar is
  // still collapsing (it would start upstream's expand animation from inside
  // the collapse's ended notification, which clears it: a CHECK), and not in a
  // closing window.
  std::unique_ptr<ExpandOnHoverLock> TakeExpandedLock();

  // The caption-button strip.
  void OnCaptionHotZoneHovered(bool hovered);
  void SetCaptionStripRevealed(bool revealed);
  void CheckCaptionStripHide();
  // Fades the frame's caption buttons in or out with the strip.
  void UpdateCaptionButtons();
  // Whether `button` (a frame child) sits above the sidebar's top row, which
  // keeps room for it.
  bool IsCaptionButtonBesideSidebar(const views::View* button) const;
  // Whether the frame has caption buttons that hide with the strip.
  bool HasPageSideCaptionButtons() const;
  // The content card's insets changed: lay out the contents area and Glance.
  void OnContentCardChanged();

  void OnSidebarHotZoneHovered(bool hovered);
  void CheckHoverLock();
  bool IsPointerAtSidebarEdge() const;
  void OnToolbarHotZoneHovered(bool hovered);
  void LayoutHotZones();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;

  Mode mode_ = Mode::kNone;
  // The prefs were applied once (the window has opened): later changes are
  // the user's, and animate.
  bool applied_ = false;
  // In compact mode: what it hides.
  bool hide_sidebar_ = false;
  bool hide_toolbar_ = false;
  bool tabs_on_right_ = false;
  bool toolbar_revealed_ = false;
  // Whether compact mode keeps the sidebar header (single toolbar).
  bool compact_keeps_sidebar_nav_ = false;
  std::optional<size_t> top_container_index_;
  // macOS: told when native fullscreen starts and ends.
  class ImmersiveObserver;
  std::unique_ptr<ImmersiveObserver> immersive_observer_;

  raw_ptr<KoanHotZone> sidebar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> toolbar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> caption_hot_zone_ = nullptr;
  // Lets the pointer through the revealed toolbar to the caption buttons.
  std::unique_ptr<KoanCaptionButtonsTargeter> caption_buttons_targeter_;
  // macOS: the traffic lights, which the window buttons below stand for.
  std::unique_ptr<KoanTrafficLightsController> traffic_lights_;
  bool caption_strip_collapsible_ = false;
  // macOS, tabs on the right: the traffic lights stay at the window's left
  // as in Zen, so the strip above the page that holds them stays open (no
  // gutter there) when windowed.
  bool lights_keep_strip_open_ = false;
  bool caption_strip_revealed_ = false;
  base::OneShotTimer caption_hide_timer_;
  std::optional<base::TimeTicks> caption_unwanted_since_;
  // Koan views inside upstream's tab strip region (KoanSidebarNavView,
  // KoanSidebarNavButtons).
  views::ViewTracker sidebar_nav_;
  views::ViewTracker sidebar_nav_buttons_;
  // Separate locks so the hot zone, a keyboard peek and a flash can't
  // release each other's hold on the sidebar.
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  std::unique_ptr<ExpandOnHoverLock> peek_lock_;
  // The peek shortcut was used while the sidebar collapsed, which refuses the
  // lock: taken once it has (OnSidebarCollapseSettled()).
  bool peek_requested_ = false;
  std::unique_ptr<ExpandOnHoverLock> flash_lock_;
  // Held while the pointer is on the traffic lights (CheckLightsHold()).
  std::unique_ptr<ExpandOnHoverLock> lights_lock_;
  base::OneShotTimer lights_release_timer_;
  std::unique_ptr<ExpandOnHoverLock> empty_tab_lock_;
  std::unique_ptr<ExpandOnHoverLock> tab_drag_lock_;
  base::RepeatingTimer tab_drag_timer_;

  base::OneShotTimer toolbar_hide_timer_;
  base::OneShotTimer flash_timer_;
  // Keeps the sidebar out for a moment after the pointer leaves its hot zone.
  base::OneShotTimer hover_release_timer_;
  // Watches the pointer while a hover lock was taken for a hover the hot zone
  // never reported (CheckHoverLock()).
  base::RepeatingTimer hover_poll_timer_;
  bool edge_sidebar_hovered_ = false;
  // Bubbles that hold the toolbar or the sidebar out (see
  // ReconcileBubbleHold()).
  base::ScopedMultiSourceObservation<views::Widget, views::WidgetObserver>
      bubble_widgets_{this};
  std::unique_ptr<ExpandOnHoverLock> bubble_lock_;
  base::OneShotTimer bubble_check_timer_;
  int bubble_check_step_ = 0;
  // A point at the window's top that is always on screen while Koan hides the
  // toolbar: where bubbles point when neither the pill nor the header is.
  raw_ptr<views::View> bubble_fallback_anchor_ = nullptr;
  // macOS: told when fullscreen of the window or of the page starts or ends.
  base::CallbackListSubscription fullscreen_subscription_;
  std::unique_ptr<views::EventMonitor> pointer_monitor_;
  // The monitor sees all the application's windows (fullscreen), not just the
  // browser window's.
  bool pointer_monitor_is_application_wide_ = false;
  // The monitor hears only the pointer leaving the window (for the outside
  // tracker): nothing is revealed that it would hide.
  bool pointer_monitor_exits_only_ = false;
  // Follows the pointer outside the window after it left through the hidden
  // sidebar's or toolbar's edge; the sidebar's hold meanwhile.
  std::unique_ptr<KoanOutsideWindowTracker> outside_tracker_;
  std::unique_ptr<ExpandOnHoverLock> outside_lock_;

  // The sidebar's context menu.
  std::unique_ptr<KoanSidebarContextMenuController> context_menu_controller_;
  views::ViewTracker context_menu_region_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> compact_menu_model_;
  // Declared after the models it shows, so it goes first.
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // Keeps the sidebar out while its menu is open.
  std::unique_ptr<SidebarHold> menu_hold_;

  base::WeakPtrFactory<KoanCompactModeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
