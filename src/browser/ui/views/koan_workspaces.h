// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_

#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/views/context_menu_controller.h"

class BrowserView;
class TabStripModel;

namespace koan {

class KoanWorkspaceSwitcherView;

// Zen-style workspaces for one window, built on upstream tab group focus
// mode (features::kTabGroupsFocusing): a workspace is a tab group, and the
// active workspace is the focused group, so the vertical tab strip shows
// only its tabs (plus pinned tabs, which act as Essentials shared by all
// workspaces). With no focused group every tab is shown ("All tabs").
//
// Owns the switcher at the bottom of the sidebar and keeps new tabs in the
// active workspace. Keyboard shortcuts live in KoanShortcutController.
class KoanWorkspaceController : public TabStripModelObserver,
                                public views::ContextMenuController {
 public:
  explicit KoanWorkspaceController(BrowserView* browser_view);
  KoanWorkspaceController(const KoanWorkspaceController&) = delete;
  KoanWorkspaceController& operator=(const KoanWorkspaceController&) = delete;
  ~KoanWorkspaceController() override;

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Whether workspaces apply to this window (feature on, normal window).
  bool IsEnabled() const;

  // Workspaces in sidebar order (by their first tab).
  std::vector<tab_groups::TabGroupId> GetWorkspaces() const;
  std::optional<tab_groups::TabGroupId> GetActiveWorkspace() const;

  void SwitchTo(const tab_groups::TabGroupId& workspace);
  void ShowAllTabs();
  void Cycle(bool forward);
  void CreateWorkspace();

  TabStripModel* tab_strip_model() const;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

  // views::ContextMenuController (the Space buttons):
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void InstallSwitcher();
  // Rebuilds the switcher soon (coalesced). Never synchronously: model
  // changes are often triggered by a switcher button, which a synchronous
  // rebuild would destroy inside its own click handler.
  void UpdateSwitcher();
  void RebuildSwitcher();
  void AdoptUngroupedTabs();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<KoanWorkspaceSwitcherView> switcher_ = nullptr;
  bool adopt_pending_ = false;
  bool rebuild_pending_ = false;

  base::WeakPtrFactory<KoanWorkspaceController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_
