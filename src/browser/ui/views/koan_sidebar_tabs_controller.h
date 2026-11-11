// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_CONTROLLER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/events/event_handler.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;
class TabStripView;

namespace koan {

class KoanSidebarTabRowsView;

// Zen's sidebar tab list extras for one window: the "+ New Tab" row above the
// unpinned tabs, the separator above them with a "Clear" button (closes the
// unpinned tabs) and the "Tabs Closed!" toast. The rows live in
// koan_sidebar_tabs.cc, sit between the active Space's pinned and normal
// tabs, are laid out by the hooks in koan_sidebar_tabs.h, and take the place
// of Chromium's "+" button at the bottom of the tab strip, which Koan hides.
// Only with Koan's sidebar on.
class KoanSidebarTabsController : public TabStripModelObserver,
                                  public ui::EventHandler,
                                  public views::ViewObserver {
 public:
  explicit KoanSidebarTabsController(BrowserView* browser_view);
  KoanSidebarTabsController(const KoanSidebarTabsController&) = delete;
  KoanSidebarTabsController& operator=(const KoanSidebarTabsController&) =
      delete;
  ~KoanSidebarTabsController() override;

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Zen's "Tabs Closed! Use Ctrl+Shift+T to undo." toast at the top right of
  // the window. Shown by KoanShortcutController::CloseUnpinnedTabs (the Clear
  // button and Ctrl+Shift+K both go through it). Only with Koan's sidebar on.
  void ShowTabsClosedToast();

  // Draws the "+ New Tab" row as selected while the URL bar is open instead
  // of a new tab, as Zen does.
  void SetNewTabSelected(bool selected);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;

  // ui::EventHandler (the pointer entering and leaving the sidebar; presses
  // on its empty space and its edge):
  void OnMouseEvent(ui::MouseEvent* event) override;

  // views::ViewObserver (the rows going away with their tab strip):
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  // The window's tab strip if it is vertical, or null.
  TabStripView* GetVerticalTabStrip() const;
  // Adds the rows to the vertical tab strip, if there is one and it has none.
  void InstallRows();
  // Where the rows go in the tab strip's children.
  static size_t RowsIndexInStrip(const TabStripView* strip);
  // Moves the rows into the active Space's tab group view, after its pinned
  // tabs (Zen's pinned section), or into the tab strip without a Space.
  // Both also put the rows after the pinned tabs in the focus order.
  void ScheduleUpdateRowsHost();
  void UpdateRowsHost();
  // The same for the Space `space` (the active one).
  void MoveRowsTo(std::optional<tab_groups::TabGroupId> space);
  // The window switched between vertical and horizontal tabs.
  void OnModeChanged(tabs::VerticalTabStripStateController* state);
  void OnCollapseChanged(tabs::VerticalTabStripCollapseState state);
  // Zen shows the separator only while the workspace has unpinned tabs.
  void UpdateClearRow();
  // Zen's zen.tabs.show-newtab-vertical and zen.view.show-newtab-button-top
  // (koan_prefs.h kShowNewTabVertical, kShowNewTabButtonTop).
  void UpdateNewTabRow();
  void OpenNewTab();
  // Zen's clicks in the sidebar (ZenCustomizableUI, tabs-mjs.patch,
  // navigator-toolbox-js.patch): a double-click on its edge resets its width;
  // a middle-click or double-click on the empty space below the tabs opens a
  // new tab (the URL bar with replace-newtab), and on macOS a press there
  // moves the window (zen.view.draggable-sidebar). Marks what it takes as
  // handled.
  void OnSidebarPressed(ui::MouseEvent* event);
  void ClearUnpinnedTabs();
  // "Close all unpinned tabs" with the user's shortcut for it.
  std::u16string GetClearTooltip() const;

  const raw_ptr<BrowserView> browser_view_;
  bool started_ = false;
  // The vertical tab strip region; its pointer enter/exit reveals Clear.
  raw_ptr<views::View> region_ = nullptr;
  // Whether each tab (by handle) was the empty tab at its last change: what
  // decides whether a change needs a layout (OnTabChangedAt()). Dropped with
  // the tab.
  std::map<int32_t, bool> empty_tab_by_id_;
  // Z50: the moves of the tab drag started by the last press in the sidebar.
  // Each one after the first is a haptic tick, as Zen's drop target changes
  // are (ZenDragAndDrop: the first is skipped, "too annoying").
  int drag_moves_ = 0;
  raw_ptr<KoanSidebarTabRowsView> rows_ = nullptr;
  // The New Tab row after the tabs, with Zen's
  // zen.view.show-newtab-button-top off. Always with `rows_`, in their parent.
  raw_ptr<KoanSidebarTabRowsView> end_rows_ = nullptr;
  bool rows_host_update_pending_ = false;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      rows_observation_{this};
  base::CallbackListSubscription mode_subscription_;
  base::CallbackListSubscription collapse_subscription_;
  PrefChangeRegistrar pref_change_registrar_;

  base::WeakPtrFactory<KoanSidebarTabsController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_CONTROLLER_H_
