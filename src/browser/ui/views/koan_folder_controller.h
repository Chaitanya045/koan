// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_CONTROLLER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class BrowserWindowInterface;
class TabStripModel;

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace koan {

class KoanFolderSearch;
class SidebarHold;
struct TabMenuContext;

// The folder menus' items, by name for UI DevTools (scripts/e2e/folders.mjs).
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderRenameItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderUnloadItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderNewSubfolderItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderChangeSpaceItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderSpaceItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderConvertItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderUnpackItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderDeleteItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateFolderItem);
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
  // Names the folder in the session data, where its tabs refer to it.
  // Unique, so a tab moved to another window doesn't join a folder there.
  std::string token;
  std::u16string name;
  bool collapsed = false;
  // The folder this one is a subfolder of; 0 at the top level.
  int parent = 0;
  // The Space (tab group) the folder belongs to.
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
                             public ui::SimpleMenuModel::Delegate,
                             public views::WidgetObserver {
 public:
  // zen.folders.max-subfolders: folders nest at most this deep.
  static constexpr int kMaxDepth = 5;
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

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Whether folders apply to this window (feature on, normal window with
  // vertical tabs, Spaces and pinned tabs).
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
  // The Space new folders go to: the active one.
  std::optional<tab_groups::TabGroupId> GetActiveSpace() const;

  // Zen's folder actions.
  int CreateFolder(const std::vector<tabs::TabInterface*>& tab_list,
                   int parent,
                   std::optional<tab_groups::TabGroupId> space,
                   std::u16string name,
                   bool rename);
  void Rename(int id, std::u16string name);
  void SetCollapsed(int id, bool collapsed);
  // Zen's Collapse / Expand All Folders, for the active Space's folders,
  // and whether one would change.
  void SetAllCollapsed(bool collapsed);
  bool HasFoldersInActiveSpace(bool collapsed) const;
  // "Create Folder" in the "+" menu: an empty folder in the active Space,
  // being renamed.
  void CreateEmptyFolder();
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
  // "Move to Folder" in the tab context menu (koan_folders.h), and its
  // commands.
  void AddTabMenuItems(TabMenuContext& context);
  void ExecuteTabMenuCommand(int command_id);
  // Zen's folder search: the tabs of a collapsed folder, filtered as you
  // type. Opens on hovering the folder.
  void ShowSearch(int id, views::View* anchor);

  // Returns true once for a folder created with `rename`: its row starts
  // editing the name when it appears.
  bool TakePendingRename(int id);

  // Adds rows for new folders, removes stale ones and relayouts, soon.
  void ScheduleSync();

  // Keeps each Space's folder tabs together, in folder order, right after
  // its other pinned tabs at its top (in the pinned section, like Zen's). Run
  // after Koan's own folder changes, and by Koan's pinned tabs at the end of
  // their Normalize() for moves Koan didn't make (an extension, a keyboard
  // move, Spaces putting a tab back): one ordering pass with one wait for
  // drags and session restore. While a drop is still to be decided or an
  // update is due, it leaves the arranging to that update (which decides the
  // drop and prunes first); nothing while the window closes.
  void ArrangeFolders();

  // The window's folders as session data (JSON), and bringing them back
  // before the window's tabs are restored: each tab joins its folder by its
  // own session data as it comes back.
  std::string Serialize() const;
  void Restore(const std::string& data);

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
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  base::WeakPtr<KoanFolderController> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  KoanFolder* GetMutableFolder(int id);
  std::vector<int> GetSubfolders(int id) const;
  // The tabs the open tab menu is for.
  std::vector<tabs::TabInterface*> GetMenuTabs() const;
  // Adds `tab_list` to `id`, taking them out of other folders, the Essentials,
  // the pinned tabs and other Spaces.
  void AddTabs(int id, const std::vector<tabs::TabInterface*>& tab_list);
  void RemoveFolder(int id);
  // Puts `id` and its subfolders in `space`.
  void SetSpace(int id, const std::optional<tab_groups::TabGroupId>& space);
  // Before `leaving` (a folder's tabs) are closed or moved out of `space`:
  // if they are all its tabs, adds a fresh tab to it first, so the Space (a
  // tab group, which goes away with its last tab) survives, like Zen's and
  // like Clear (KoanShortcutController::CloseUnpinnedTabs).
  void KeepSpace(const std::optional<tab_groups::TabGroupId>& space,
                 const std::vector<tabs::TabInterface*>& leaving,
                 bool closing);
  // Remembers where a tab being dragged started (`from_index`).
  void OnTabMoved(tabs::TabInterface* tab, int from_index);
  // Once a drag is over, for each dragged tab that moved (with its split): a
  // drop on a folder's row puts it into the folder or beside it; otherwise
  // Zen's drag rule.
  void FinishDrag();
  // A drop on a folder's row: the folder, and whether the tab goes into it
  // (else beside it, in the folder holding it).
  struct RowDrop {
    int folder = 0;
    bool into = false;
  };
  RowDrop RowDropAt(const gfx::Point& screen_point) const;
  // Adds `unit` (a tab, or a split's tabs) to `id`, before its tabs.
  void MoveToFolderStart(const std::vector<tabs::TabInterface*>& unit, int id);
  // `tab`, or its split's tabs, in tab strip order.
  std::vector<tabs::TabInterface*> WithSplit(tabs::TabInterface* tab) const;
  // Puts `unit` in folder `target` (0: none), pinned where it is (in the
  // pinned section).
  void SetFolder(const std::vector<tabs::TabInterface*>& unit, int target);
  // Zen's drag rule for `unit` where it was dropped: before a folder's tab
  // it joins that folder; among tabs outside folders it leaves its folder.
  void ApplyDragRule(const std::vector<tabs::TabInterface*>& unit);
  // Added tabs whose session data names a folder of this window join it (a
  // restored tab, or a reopened one whose folder is still there).
  void OnTabsAdded(const std::vector<tabs::TabInterface*>& added);
  // Once the restored tabs are in: the restored folders' Spaces.
  void FinishRestore();
  // Drops tabs that left their folder for good: closed, in another window,
  // made an Essential or unpinned. With `spaces_settled` (after Spaces' own
  // update, which puts back tabs and groups that went for a moment), also
  // tabs in another Space and the folders whose Space is gone.
  void Prune(bool spaces_settled);
  // Spaces changed (and settled): see Prune(). The first time, a window that
  // opened without a session restore gets the stored folders back.
  void OnSpacesChanged();
  // The folders without the session (prefs::kFolderList): the folders of
  // the window the user is in, each with its Space's place among the
  // window's Spaces and its tabs (URL, title, pinned state); and bringing
  // them back, with their tabs unloaded, in the profile's only window when
  // it opened without a session restore (Zen restores the pinned tabs and
  // folders then).
  base::ListValue StoredFolders() const;
  void StoreFolders();
  // The window's Spaces in the order the stored folders count them by.
  std::vector<tab_groups::TabGroupId> StoredSpaceOrder() const;
  void RecreateStoredFolders();
  // Whether the window is closing (its tabs go; the session keeps what it
  // had).
  bool IsClosing() const;
  void SyncRows();
  // Schedules Update().
  void Changed();
  // A drag's end, Prune(), ArrangeFolders() after Koan's own changes, the
  // rows and the session data.
  void Update();
  void Save();
  void StartAnimation();

  const raw_ptr<BrowserView> browser_view_;
  std::vector<KoanFolder> folders_;
  int next_id_ = 1;
  int pending_rename_ = 0;
  base::TimeTicks animate_until_;
  bool normalizing_ = false;
  bool sync_pending_ = false;
  // The tabs of the current drag and where each started.
  struct DraggedTab {
    tabs::TabHandle tab;
    int from_index;
  };
  std::vector<DraggedTab> dragged_;
  // Where the pointer was last seen during the drag.
  gfx::Point drop_point_;
  // While a session restores the window's tabs: the restored folders'
  // Spaces, by their place among the window's Spaces (-1: none), counted
  // after the `spaces_before_restore_` it had already.
  bool restoring_ = false;
  std::map<int, int> restored_spaces_;
  int spaces_before_restore_ = 0;
  bool update_pending_ = false;
  // The window got its folders from the session; whether the stored ones
  // were looked at (and so may be written); and whether this window may
  // write them (not right after a crash, while the user may still restore
  // the session, until its folders change).
  bool restored_from_session_ = false;
  bool recreate_checked_ = false;
  bool stores_folders_ = false;
  // The window has had a folder (only then may it store an empty list).
  bool had_folders_ = false;
  // After a crash (the session may still be restored), the stored folders
  // this window didn't bring back: kept in what it writes.
  base::ListValue carried_;
  std::optional<base::ListValue> first_stored_;
  bool may_store_ = true;
  // Koan changed folders: arrange their tabs in the next Update().
  bool arrange_ = false;
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
  // Holds the sidebar out while the folder menu is open (before the runner:
  // its close callback lets go).
  std::unique_ptr<SidebarHold> menu_hold_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<KoanFolderSearch> search_;
  base::CallbackListSubscription spaces_subscription_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  // Counts popups, so a closed one's cleanup leaves a newer one alone; and
  // the folder the open one is for.
  int search_generation_ = 0;
  int search_folder_ = 0;

  base::WeakPtrFactory<KoanFolderController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_FOLDER_CONTROLLER_H_
