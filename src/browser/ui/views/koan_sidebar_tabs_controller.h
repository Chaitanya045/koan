// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/events/event_handler.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;

namespace views {
class Widget;
}  // namespace views

namespace koan {

class KoanSidebarTabRowsView;
class KoanToastView;

// Zen's sidebar tab list extras for one window: the "+ New Tab" row above the
// unpinned tabs, the separator above them with a "Clear" button (closes the
// unpinned tabs) and the "Tabs Closed!" toast. The rows live in
// koan_sidebar_tabs.cc, are laid out by the hooks in koan_sidebar_tabs.h, and
// take the place of Chromium's "+" button at the bottom of the tab strip,
// which Koan hides. Only with Koan's sidebar on.
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

  // ui::EventHandler (the pointer entering and leaving the sidebar):
  void OnMouseEvent(ui::MouseEvent* event) override;

  // views::ViewObserver (the rows going away with their tab strip):
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  // Adds the rows to the vertical tab strip, if there is one and it has none.
  void InstallRows();
  // The window switched between vertical and horizontal tabs.
  void OnModeChanged(tabs::VerticalTabStripStateController* state);
  void OnCollapseChanged(tabs::VerticalTabStripCollapseState state);
  // Zen shows the separator only while the workspace has unpinned tabs.
  void UpdateClearRow();
  void OpenNewTab();
  void ClearUnpinnedTabs();
  // "Close all unpinned tabs" with the user's shortcut for it.
  std::u16string GetClearTooltip() const;
  void OnToastHovered(bool hovered);
  void HideToast();
  void CloseToast();

  const raw_ptr<BrowserView> browser_view_;
  bool started_ = false;
  // The vertical tab strip region; its pointer enter/exit reveals Clear.
  raw_ptr<views::View> region_ = nullptr;
  raw_ptr<KoanSidebarTabRowsView> rows_ = nullptr;
  base::ScopedObservation<views::View, views::ViewObserver> rows_observation_{
      this};
  base::CallbackListSubscription mode_subscription_;
  base::CallbackListSubscription collapse_subscription_;

  std::unique_ptr<views::Widget> toast_widget_;
  raw_ptr<KoanToastView> toast_view_ = nullptr;
  // Hides the toast, then closes it once it has faded out.
  base::OneShotTimer toast_timer_;
  bool toast_hiding_ = false;

  base::WeakPtrFactory<KoanSidebarTabsController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_CONTROLLER_H_
