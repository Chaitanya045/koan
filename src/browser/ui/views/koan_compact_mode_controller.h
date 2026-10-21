// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/events/event_observer.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

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
class KoanSidebarContextMenuController;
class KoanSidebarNavButtons;
class KoanSidebarNavView;

// Zen's compact mode and layouts for one window.
//
// Single toolbar (Zen's default layout; Ctrl+Alt+B, pref koan.single_toolbar):
// there is no top toolbar. The top container is hidden in the overlay
// (below), the page card reaches the top of the window, and
// KoanSidebarNavView at the top of the sidebar has the navigation buttons
// and a URL pill. Like Zen, it needs an expanded sidebar: collapsing the
// sidebar to icons gives Zen's "Collapsed Sidebar" layout (a toolbar above
// the page) until it is expanded again.
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
// In both, focusing the omnibox (the URL pill, Ctrl+L) shows Zen's floating
// URL bar: just the location bar, in a card centred over the window; it goes
// away when focus leaves it.
//
// Tabs on the right (pref koan.tabs.on_right) mirrors the window: see
// IsSidebarOnRight() in koan_compact_mode.h.
//
// All states are profile prefs shared by all windows. Keyboard shortcuts
// live in KoanShortcutController; right-clicking the sidebar opens Zen's
// toolbar context menu (Compact Mode, Tabs on the right).
class KoanCompactModeController : public views::FocusChangeListener,
                                  public views::ViewObserver,
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
  bool hides_toolbar() const {
    return mode_ == Mode::kSingleToolbar ||
           (mode_ == Mode::kCompact && hide_toolbar_);
  }
  bool is_toolbar_revealed() const { return toolbar_revealed_; }
  bool is_url_bar_floating() const { return url_bar_floating_; }
  // The caption-button strip above the page is collapsed (the card starts at
  // the gutter).
  bool caption_strip_collapsed() const {
    return caption_strip_collapsible_ && !caption_strip_revealed_;
  }
  // The "Tabs on the right" setting. Where the sidebar is, is
  // IsSidebarOnRight() (koan_compact_mode.h), which also checks the window.
  bool tabs_on_right() const { return tabs_on_right_; }

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Flip the profile prefs.
  void ToggleCompactMode();
  void ToggleSingleToolbar();
  void ToggleTabsOnRight();
  // Zen's "Hide sidebar" / "Hide toolbar" / "Hide both": what compact mode
  // hides. Like Zen, this doesn't turn compact mode on.
  void SetCompactModeHides(bool sidebar, bool toolbar);

  // Compact mode: slides the hidden sidebar out (Zen's Ctrl+Alt+S); it
  // slides away once the pointer isn't over it. Returns false outside
  // compact mode.
  bool PeekSidebar();

  // A drag of the sidebar's edge collapsed it: like Zen, hide it in compact
  // mode instead, and say once how to bring it back.
  void OnSidebarDragCollapsed();

  // Zen's toolbar context menu, at `screen_point`.
  void ShowContextMenu(const gfx::Point& screen_point,
                       ui::mojom::MenuSourceType source_type);

  // Compact mode hides the toolbar but not the sidebar: the revealed toolbar
  // goes beside the sidebar, as Zen's navbar does.
  bool reveals_toolbar_beside_sidebar() const {
    return can_reveal_toolbar() && !hides_sidebar();
  }

  // views::FocusChangeListener:
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // The original profile's prefs (off the record windows share them).
  PrefService* GetPrefs() const;
  // What the pref says compact mode hides.
  bool PrefHidesSidebar() const;
  bool PrefHidesToolbar() const;
  Mode DesiredMode() const;
  // Whether the user collapsed the sidebar to icons (not compact mode).
  bool IsSidebarCollapsedByUser() const;
  // Single toolbar is on and possible (the sidebar is expanded), as in Zen.
  bool DesiredSingleToolbar() const;
  // What compact mode hides, from the prefs and the layout.
  bool DesiredHideSidebar() const;
  bool DesiredHideToolbar() const;
  // Zen's check for a compact variant that would strand the window buttons:
  // collapsed sidebar, buttons on its side, only the toolbar hidden.
  bool IsHideToolbarOnlyBlocked() const;
  void UpdateActiveSoon();
  void UpdateActive();
  void Activate(Mode mode);
  // `keep_sidebar_hidden`: compact mode goes on hiding the sidebar (only
  // what else it hides changes), so it isn't expanded in between.
  void Deactivate(bool keep_sidebar_hidden);
  void OnTabsOnRightChanged();
  void OnSidebarCollapseChanged();

  // Compact mode with the toolbar hidden in the overlay, revealed at the top
  // edge (not single toolbar, which has no toolbar to reveal).
  bool can_reveal_toolbar() const {
    return mode_ == Mode::kCompact && hide_toolbar_ &&
           !compact_keeps_sidebar_nav_;
  }
  void SetToolbarRevealed(bool revealed, bool animate);
  // The toolbar is slid out of the window (neither revealed nor floating).
  bool is_toolbar_away() const {
    return !toolbar_revealed_ && !url_bar_floating_;
  }
  void ApplyToolbarTransform(bool animate);
  // Whether the top container would be drawn if its layer were visible.
  bool IsTopContainerShown() const;
  void HideAwayToolbarLayer();
  bool ShouldKeepToolbarRevealed() const;
  void CheckToolbarHide();

  // While the toolbar, the caption strip or a peeked sidebar is out, watches
  // the pointer so each hides once it has been away long enough. Nothing
  // runs while they are hidden or while the pointer rests over them.
  void UpdatePointerMonitor();
  void CheckRevealedHides();

  // The floating URL bar: shows only the location bar, centred.
  void SetUrlBarFloating(bool floating);
  void HideToolbarForFloating();
  void LayoutFloatingUrlBar();

  KoanSidebarNavView* sidebar_nav();

  // Single toolbar: adds KoanSidebarNavView to the top of the sidebar.
  void InstallSidebarNav();
  // Right-clicking the sidebar opens ShowContextMenu() (the region view is
  // recreated on tab strip mode changes).
  void InstallContextMenu();
  void OnContextMenuClosed();

  void CheckSidebarPeek();
  // Zen's flash popup: the hidden sidebar pops up for a moment.
  void FlashSidebar();
  void EndFlash();

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
  void OnToolbarHotZoneHovered(bool hovered);
  void LayoutHotZones();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  base::CallbackListSubscription collapse_subscription_;
  base::CallbackListSubscription resizing_subscription_;
  // The sidebar was expanded when the current drag of its edge started.
  bool resizing_from_expanded_ = false;

  Mode mode_ = Mode::kNone;
  // In compact mode: what it hides.
  bool hide_sidebar_ = false;
  bool hide_toolbar_ = false;
  bool tabs_on_right_ = false;
  bool toolbar_revealed_ = false;
  bool url_bar_floating_ = false;
  // Whether compact mode keeps the sidebar header (single toolbar).
  bool compact_keeps_sidebar_nav_ = false;
  // While the URL bar floats: the toolbar's other views, hidden, and whether
  // each is to be visible afterwards. Entries go when a view is deleted.
  base::flat_map<views::View*, bool> floating_restore_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      floating_siblings_observation_{this};
  bool changing_toolbar_visibility_ = false;
  bool laying_out_floating_ = false;
  base::ScopedObservation<views::View, views::ViewObserver>
      top_container_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver>
      toolbar_observation_{this};
  // Compact mode hid the sidebar: whether it was collapsed before.
  std::optional<bool> collapsed_before_;
  // The window was opened while compact mode was on, and hasn't had its
  // sidebar back since: its collapsed state may be compact mode's.
  bool opened_in_compact_mode_ = false;
  // A drag collapsed the sidebar into compact mode: it was expanded before.
  bool collapsed_by_drag_ = false;
  // Koan asked upstream to expand the sidebar (it animates).
  bool expanding_sidebar_ = false;
  std::optional<size_t> top_container_index_;

  raw_ptr<KoanHotZone> sidebar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> toolbar_hot_zone_ = nullptr;
  raw_ptr<KoanHotZone> caption_hot_zone_ = nullptr;
  // Lets the pointer through the revealed toolbar to the caption buttons.
  std::unique_ptr<KoanCaptionButtonsTargeter> caption_buttons_targeter_;
  bool caption_strip_collapsible_ = false;
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
  std::unique_ptr<ExpandOnHoverLock> flash_lock_;

  base::OneShotTimer toolbar_hide_timer_;
  base::OneShotTimer sidebar_peek_timer_;
  base::OneShotTimer flash_timer_;
  std::optional<base::TimeTicks> sidebar_unhovered_since_;
  base::TimeTicks sidebar_peek_started_;
  std::optional<base::TimeTicks> toolbar_unwanted_since_;
  std::unique_ptr<views::EventMonitor> pointer_monitor_;

  // The sidebar's context menu.
  std::unique_ptr<KoanSidebarContextMenuController> context_menu_controller_;
  views::ViewTracker context_menu_region_;
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> compact_menu_model_;
  // Declared after the models it shows, so it goes first.
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // Keeps the sidebar out while its menu is open.
  std::unique_ptr<ExpandOnHoverLock> menu_lock_;

  base::WeakPtrFactory<KoanCompactModeController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_CONTROLLER_H_
