// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/menus/simple_menu_model.h"

class BrowserView;
class BrowserWindowInterface;
class TabStripModel;

namespace gfx {
class Point;
}  // namespace gfx

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace koan {

class KoanFolderSearch;

// The folder menus' items, by name for UI DevTools (scripts/e2e/folders.mjs).
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderRenameItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderUnloadItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderNewSubfolderItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderChangeSpaceItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderSpaceItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderConvertItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderUnpackItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderDeleteItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateSpaceItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateFolderItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateSplitItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateTabItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanMoveToFolderItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanTabFolderItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanTabNewFolderItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanTabRemoveFromFolderItem);

// A Zen folder: a named, collapsible list of tabs (and subfolders) at the top
// of a Space. Tabs in a folder stay in their Space's tab group.
struct KoanFolder {
  KoanFolder();
  KoanFolder(KoanFolder&&);
  KoanFolder& operator=(KoanFolder&&);
  ~KoanFolder();

  int id = 0;
  std::u16string name;
  bool collapsed = false;
  // The folder this one is a subfolder of; 0 at the top level.
  int parent = 0;
  // The Space (tab group) the folder belongs to; none for ungrouped tabs.
  std::optional<tab_groups::TabGroupId> space;
  // The folder's own tabs (not its subfolders'), in no particular order.
  std::vector<tabs::TabHandle> tabs;
};

// Zen-style tab folders for one window (ZenFolders.mjs).
//
// Chromium's tab groups don't nest and can't be pinned, and Koan's Spaces
// already are tab groups, so folders are Koan's own model: a tree of folders
// per Space, each holding tab handles. The tabs stay ordinary tabs of their
// Space; Koan keeps each folder's tabs together at the top of the Space in
// the tab strip model (folders first, in tree order, then the loose tabs),
// and the vertical tab strip's layouts call FolderRows (koan_folders.h) to
// put a row before each folder's tabs, indent them and hide them while the
// folder is collapsed. Upstream draws, drags and closes the tabs as usual.
class KoanFolderController : public TabStripModelObserver,
                             public ui::SimpleMenuModel::Delegate {
 public:
  // zen.folders.max-subfolders: folders nest at most this deep.
  static constexpr int kMaxDepth = 5;
  // The tab menu's "Move to Folder" (koan_tab_menu_hooks.h), next to
  // kSplitOutTabCommandId.
  static constexpr int kMoveToFolderCommandId = 3002;

  struct Entry {
    int id;
    int depth;
  };

  explicit KoanFolderController(BrowserView* browser_view);
  KoanFolderController(const KoanFolderController&) = delete;
  KoanFolderController& operator=(const KoanFolderController&) = delete;
  ~KoanFolderController() override;

  // The controller of the window `view` (or `browser`) is in, if folders
  // apply to it.
  static KoanFolderController* FromView(const views::View* view);
  static KoanFolderController* FromBrowser(BrowserWindowInterface* browser);
  // Whether `tab` is in a folder (of a window where folders apply).
  static bool IsInFolder(const tabs::TabInterface* tab);

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Whether folders apply to this window (feature on, normal window with
  // vertical tabs).
  bool IsEnabled() const;

  BrowserView* browser_view() const { return browser_view_; }
  TabStripModel* tab_strip_model() const;

  const KoanFolder* GetFolder(int id) const;
  // The folders of `space` in display order: each folder, then its tabs,
  // then its subfolders.
  std::vector<Entry> GetFolders(
      const std::optional<tab_groups::TabGroupId>& space) const;
  // The folder holding `tab`, or 0.
  int GetFolderOf(const tabs::TabInterface* tab) const;
  int GetDepth(int id) const;
  // The tabs of `id` (and of its subfolders, if `recursive`), in tab strip
  // order.
  std::vector<tabs::TabInterface*> GetTabs(int id, bool recursive) const;
  // Whether the active tab is in `id` or one of its subfolders.
  bool HasActiveTab(int id) const;
  // The outermost collapsed folder among `id` and its parents, or 0.
  int GetRootMostCollapsed(int id) const;
  // Whether a collapse or expand is animating (hidden tabs stay visible
  // meanwhile, so they can shrink away).
  bool animating() const { return base::TimeTicks::Now() < animate_until_; }
  // The Space new folders go to: the active one, or none in "All tabs".
  std::optional<tab_groups::TabGroupId> GetActiveSpace() const;

  // Zen's folder actions.
  int CreateFolder(const std::vector<tabs::TabInterface*>& tab_list,
                   int parent,
                   std::optional<tab_groups::TabGroupId> space,
                   std::u16string name,
                   bool rename);
  void Rename(int id, std::u16string name);
  void SetCollapsed(int id, bool collapsed);
  // Zen's Collapse / Expand All Folders, for the active Space's folders
  // (the ungrouped tabs' in "All tabs"), and whether one would change.
  void SetAllCollapsed(bool collapsed);
  bool HasFoldersInActiveSpace(bool collapsed) const;
  void CreateSubfolder(int id);
  void Unpack(int id);
  void Delete(int id);
  void ChangeSpace(int id, const tab_groups::TabGroupId& space);
  void ConvertToSpace(int id);
  void UnloadAllTabs(int id);
  void MoveTabsToFolder(const std::vector<tabs::TabInterface*>& tab_list,
                        int id);
  void RemoveTabsFromFolder(const std::vector<tabs::TabInterface*>& tab_list);
  void ActivateTab(tabs::TabInterface* tab);

  // The folder's context menu (Zen's zenFolderActions).
  void ShowFolderMenu(int id,
                      views::View* source,
                      const gfx::Point& point,
                      ui::mojom::MenuSourceType source_type);
  // Zen's "Create New" menu of the sidebar's "+" button.
  void ShowCreateMenu(views::View* anchor);
  // "Move to Folder" in the tab context menu.
  void AddTabMenuItems(ui::SimpleMenuModel& menu, int tab_index);
  // Zen's folder search: the tabs of a collapsed folder, filtered as you
  // type. Opens on hovering the folder.
  void ShowSearch(int id, views::View* anchor);

  // Returns true once for a folder created with `rename`: its row starts
  // editing the name when it appears.
  bool TakePendingRename(int id);

  // Adds rows for new folders, removes stale ones and relayouts, soon.
  void ScheduleSync();

  // Session data: folders by tab index, as JSON.
  std::string Serialize() const;
  void Restore(int first_tab_index, const std::string& data);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void TabGroupedStateChanged(TabStripModel* tab_strip_model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  base::WeakPtr<KoanFolderController> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  KoanFolder* GetMutableFolder(int id);
  std::vector<int> GetSubfolders(int id) const;
  // Adds `tab_list` to `id`, taking them out of other folders, the Essentials
  // and other Spaces.
  void AddTabs(int id, const std::vector<tabs::TabInterface*>& tab_list);
  void RemoveFolder(int id);
  // Before `leaving` (a folder's tabs) are closed or moved out of `space`:
  // if they are all its tabs, adds a fresh tab to it first, so the Space (a
  // tab group, which goes away with its last tab) survives, like Zen's and
  // like Clear (KoanShortcutController::CloseUnpinnedTabs). Without a Space,
  // keeps the window open that way when closing all its tabs.
  void KeepSpace(const std::optional<tab_groups::TabGroupId>& space,
                 const std::vector<tabs::TabInterface*>& leaving,
                 bool closing);
  // Applies Zen's drag rule after a user move: a tab dropped before a
  // folder's tab joins that folder; a tab dropped among loose tabs leaves.
  void OnTabMoved(tabs::TabInterface* tab, int to_index);
  // Drops closed and pinned tabs, tabs that left the folder's Space and
  // folders whose Space is gone.
  void Prune();
  // Moves each Space's folder tabs, in folder order, to the top of the
  // Space in the tab strip model. Waits while a tab is being dragged.
  void Normalize();
  void SyncRows();
  void Changed();
  void Save();
  void StartAnimation();

  const raw_ptr<BrowserView> browser_view_;
  std::vector<KoanFolder> folders_;
  int next_id_ = 1;
  int pending_rename_ = 0;
  base::TimeTicks animate_until_;
  bool normalizing_ = false;
  bool sync_pending_ = false;
  bool restored_ = false;
  bool normalize_pending_ = false;
  std::string saved_;
  base::OneShotTimer animation_end_timer_;
  base::OneShotTimer drag_retry_timer_;

  // Context of the open menu.
  int menu_folder_ = 0;
  std::vector<tabs::TabHandle> menu_tabs_;
  std::vector<int> menu_targets_;
  std::vector<tab_groups::TabGroupId> menu_spaces_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> submenu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<ui::SimpleMenuModel> tab_submenu_model_;
  std::unique_ptr<KoanFolderSearch> search_;
  base::CallbackListSubscription mode_subscription_;
  // Counts popups, so a closed one's cleanup leaves a newer one alone.
  int search_generation_ = 0;

  base::WeakPtrFactory<KoanFolderController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_CONTROLLER_H_
