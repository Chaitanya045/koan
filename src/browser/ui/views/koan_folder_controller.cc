// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_folder_controller.h"

#include <algorithm>
#include <map>
#include <utility>
#include <variant>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_views.h"
#include "koan/browser/ui/views/koan_folders.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/range/range.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderRenameItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderUnloadItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderNewSubfolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderChangeSpaceItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderSpaceItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderConvertItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderUnpackItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanFolderDeleteItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateSpaceItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateSplitItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateTabItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanMoveToFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanTabFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanTabNewFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanTabRemoveFromFolderItem);

namespace {

// Window extra data key of the session file.
constexpr char kSessionKey[] = "koan.folders";

// Gap between rows in the vertical tab strip (kTabVerticalPadding).
constexpr int kRowSpacing = 2;

// How long hidden tabs stay visible while they shrink away (upstream's tab
// layout animation takes 200 ms).
constexpr base::TimeDelta kCollapseAnimation = base::Milliseconds(250);

// Menu commands of Koan's own (sub)menus. The tab menu's "Move to Folder"
// is KoanFolderController::kMoveToFolderCommandId.
enum Command : int {
  kFolderRename = 19400,
  kFolderUnloadAll,
  kFolderNewSubfolder,
  kFolderChangeSpace,
  kFolderConvertToSpace,
  kFolderUnpack,
  kFolderDelete,
  kCreateSpace,
  kCreateFolder,
  kCreateSplit,
  kCreateTab,
  kTabNewFolder,
  kTabRemoveFromFolder,
  // + index in menu_spaces_.
  kSpaceBase = 19500,
  // + index in menu_targets_.
  kTargetBase = 19600,
  kCommandEnd = 19700,
};

void AddItem(ui::SimpleMenuModel& model,
             int command_id,
             const std::u16string& label,
             ui::ElementIdentifier id) {
  model.AddItem(command_id, label);
  model.SetElementIdentifierAt(model.GetItemCount() - 1, id);
}

// The first tab of a tab strip child (a tab, or a split's first tab).
const tabs::TabInterface* GetFirstTab(const views::View& child) {
  if (const auto* tab_view = views::AsViewClass<TabView>(&child)) {
    return tab_view->GetTabInterface();
  }
  if (const auto* split = views::AsViewClass<SplitTabView>(&child)) {
    const TabCollectionNode* node = split->collection_node();
    if (node && !node->children().empty()) {
      tabs::ConstChildPtr data = node->children().front()->GetNodeData();
      if (std::holds_alternative<tabs::ConstDanglingUntriagedTabInterface>(
              data)) {
        return std::get<tabs::ConstDanglingUntriagedTabInterface>(data);
      }
    }
  }
  return nullptr;
}

// Whether `child` shows the active tab (a split shows two tabs).
bool ShowsActiveTab(const views::View& child, const TabStripModel* model) {
  const tabs::TabInterface* tab = GetFirstTab(child);
  if (!tab) {
    return false;
  }
  const tabs::TabInterface* active = model->GetActiveTab();
  return tab == active || (active && tab->GetSplit().has_value() &&
                           tab->GetSplit() == active->GetSplit());
}

std::vector<int> SortedIndices(
    TabStripModel* model,
    const std::vector<tabs::TabInterface*>& tab_list) {
  std::vector<int> indices;
  for (tabs::TabInterface* tab : tab_list) {
    const int index = model->GetIndexOfTab(tab);
    if (index != TabStripModel::kNoTab) {
      indices.push_back(index);
    }
  }
  std::ranges::sort(indices);
  return indices;
}

}  // namespace

KoanFolder::KoanFolder() = default;
KoanFolder::KoanFolder(KoanFolder&&) = default;
KoanFolder& KoanFolder::operator=(KoanFolder&&) = default;
KoanFolder::~KoanFolder() = default;

KoanFolderController::KoanFolderController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // Observed even while the tabs are horizontal (no folders then), to keep
  // the folders' tabs and the session data right.
  if (features::IsFoldersEnabled() && browser_view_->GetIsNormalType()) {
    tab_strip_model()->AddObserver(this);
  }
}

KoanFolderController::~KoanFolderController() = default;

// static
KoanFolderController* KoanFolderController::FromView(const views::View* view) {
  const views::Widget* widget = view ? view->GetWidget() : nullptr;
  if (!widget) {
    return nullptr;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForNativeWindow(
      widget->GetTopLevelWidget()->GetNativeWindow());
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (!controller || !controller->folders()->IsEnabled()) {
    return nullptr;
  }
  return controller->folders();
}

// static
KoanFolderController* KoanFolderController::FromBrowser(
    BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (!controller || !controller->folders()->IsEnabled()) {
    return nullptr;
  }
  return controller->folders();
}

// static
bool KoanFolderController::IsInFolder(const tabs::TabInterface* tab) {
  const BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  const BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->folders()->IsEnabled() &&
         controller->folders()->GetFolderOf(tab) != 0;
}

bool KoanFolderController::IsEnabled() const {
  if (!features::IsFoldersEnabled() || !browser_view_->GetIsNormalType()) {
    return false;
  }
  const auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  return state && state->ShouldDisplayVerticalTabs();
}

void KoanFolderController::OnAddedToWidget() {
  if (auto* state = tabs::VerticalTabStripStateController::From(
          browser_view_->browser())) {
    // Back to vertical tabs: the folders' rows and order return.
    mode_subscription_ = state->RegisterOnModeChanged(
        base::IgnoreArgs<tabs::VerticalTabStripStateController*>(
            base::BindRepeating(&KoanFolderController::Changed,
                                base::Unretained(this))));
  }
  ScheduleSync();
}

void KoanFolderController::OnBrowserViewDestroying() {
  mode_subscription_ = {};
  menu_runner_.reset();
  search_.reset();
  // Unconditional: removing an observer that was never added is a no-op.
  tab_strip_model()->RemoveObserver(this);
}

TabStripModel* KoanFolderController::tab_strip_model() const {
  return browser_view_->browser()->GetTabStripModel();
}

const KoanFolder* KoanFolderController::GetFolder(int id) const {
  const auto it = std::ranges::find(folders_, id, &KoanFolder::id);
  return it == folders_.end() ? nullptr : &*it;
}

KoanFolder* KoanFolderController::GetMutableFolder(int id) {
  const auto it = std::ranges::find(folders_, id, &KoanFolder::id);
  return it == folders_.end() ? nullptr : &*it;
}

std::vector<KoanFolderController::Entry> KoanFolderController::GetFolders(
    const std::optional<tab_groups::TabGroupId>& space) const {
  std::vector<Entry> entries;
  const auto add = [&](const auto& self, int parent, int depth) -> void {
    for (const KoanFolder& folder : folders_) {
      if (folder.parent == parent && folder.space == space) {
        entries.push_back({folder.id, depth});
        self(self, folder.id, depth + 1);
      }
    }
  };
  add(add, 0, 0);
  return entries;
}

std::vector<int> KoanFolderController::GetSubfolders(int id) const {
  std::vector<int> ids;
  for (const KoanFolder& folder : folders_) {
    if (folder.parent == id) {
      ids.push_back(folder.id);
    }
  }
  return ids;
}

int KoanFolderController::GetFolderOf(const tabs::TabInterface* tab) const {
  if (!tab) {
    return 0;
  }
  for (const KoanFolder& folder : folders_) {
    for (const tabs::TabHandle& handle : folder.tabs) {
      if (handle.Get() == tab) {
        return folder.id;
      }
    }
  }
  return 0;
}

int KoanFolderController::GetDepth(int id) const {
  int depth = 0;
  for (const KoanFolder* folder = GetFolder(id); folder && folder->parent;
       folder = GetFolder(folder->parent)) {
    ++depth;
  }
  return depth;
}

std::vector<tabs::TabInterface*> KoanFolderController::GetTabs(
    int id,
    bool recursive) const {
  TabStripModel* model = tab_strip_model();
  std::vector<tabs::TabInterface*> tab_list;
  const auto collect = [&](const auto& self, int folder_id) -> void {
    const KoanFolder* folder = GetFolder(folder_id);
    if (!folder) {
      return;
    }
    for (const tabs::TabHandle& handle : folder->tabs) {
      tabs::TabInterface* tab = handle.Get();
      if (tab && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
        tab_list.push_back(tab);
      }
    }
    if (recursive) {
      for (int child : GetSubfolders(folder_id)) {
        self(self, child);
      }
    }
  };
  collect(collect, id);
  std::ranges::sort(tab_list, {}, [model](tabs::TabInterface* tab) {
    return model->GetIndexOfTab(tab);
  });
  return tab_list;
}

bool KoanFolderController::HasActiveTab(int id) const {
  for (int folder = GetFolderOf(tab_strip_model()->GetActiveTab()); folder;
       folder = GetFolder(folder)->parent) {
    if (folder == id) {
      return true;
    }
  }
  return false;
}

int KoanFolderController::GetRootMostCollapsed(int id) const {
  int root = 0;
  for (const KoanFolder* folder = GetFolder(id); folder;
       folder = GetFolder(folder->parent)) {
    if (folder->collapsed) {
      root = folder->id;
    }
  }
  return root;
}

std::optional<tab_groups::TabGroupId> KoanFolderController::GetActiveSpace()
    const {
  return tab_strip_model()->GetFocusedGroup();
}

int KoanFolderController::CreateFolder(
    const std::vector<tabs::TabInterface*>& tab_list,
    int parent,
    std::optional<tab_groups::TabGroupId> space,
    std::u16string name,
    bool rename) {
  // Nest at most kMaxDepth deep; deeper requests go next to the parent.
  while (parent && GetDepth(parent) + 1 >= kMaxDepth) {
    parent = GetFolder(parent)->parent;
  }
  KoanFolder folder;
  folder.id = next_id_++;
  folder.name = std::move(name);
  folder.parent = parent;
  folder.space = parent ? GetFolder(parent)->space : space;
  const int id = folder.id;
  folders_.push_back(std::move(folder));
  // Like Zen, expand the parents so the new folder shows.
  for (KoanFolder* p = GetMutableFolder(parent); p;
       p = GetMutableFolder(p->parent)) {
    p->collapsed = false;
  }
  AddTabs(id, tab_list);
  if (rename) {
    pending_rename_ = id;
  }
  Changed();
  return id;
}

void KoanFolderController::AddTabs(
    int id,
    const std::vector<tabs::TabInterface*>& tab_list) {
  KoanFolder* folder = GetMutableFolder(id);
  TabStripModel* model = tab_strip_model();
  if (!folder || tab_list.empty()) {
    return;
  }
  base::AutoReset<bool> busy(&normalizing_, true);
  // Split tabs move as a pair, like Zen's split-view groups.
  std::vector<tabs::TabInterface*> all;
  for (tabs::TabInterface* tab : tab_list) {
    if (std::ranges::contains(all, tab)) {
      continue;
    }
    if (const std::optional<split_tabs::SplitTabId> split = tab->GetSplit()) {
      for (tabs::TabInterface* other : *model) {
        if (other->GetSplit() == split) {
          all.push_back(other);
        }
      }
    } else {
      all.push_back(tab);
    }
  }
  // Like Zen, a tab moved into a folder stops being an Essential. Joining a
  // Space's group below unpins it (keeping splits and the Space's focus);
  // ungrouped folders unpin it here.
  if (!folder->space) {
    for (tabs::TabInterface* tab : all) {
      if (tab->IsPinned()) {
        model->SetTabPinned(model->GetIndexOfTab(tab), false);
      }
    }
  }
  // And joins the folder's Space.
  std::vector<tabs::TabInterface*> regroup;
  for (tabs::TabInterface* tab : all) {
    if (tab->GetGroup() != folder->space || tab->IsPinned()) {
      regroup.push_back(tab);
    }
  }
  if (!regroup.empty()) {
    const std::vector<int> indices = SortedIndices(model, regroup);
    if (folder->space) {
      model->AddToExistingGroup(indices, *folder->space);
    } else {
      model->RemoveFromGroup(indices);
    }
  }
  for (KoanFolder& other : folders_) {
    std::erase_if(other.tabs, [&all](const tabs::TabHandle& handle) {
      return std::ranges::contains(all, handle.Get());
    });
  }
  folder = GetMutableFolder(id);
  for (tabs::TabInterface* tab : all) {
    folder->tabs.push_back(tab->GetHandle());
  }
}

void KoanFolderController::RemoveFolder(int id) {
  for (int child : GetSubfolders(id)) {
    RemoveFolder(child);
  }
  std::erase_if(folders_,
                [id](const KoanFolder& folder) { return folder.id == id; });
}

void KoanFolderController::Rename(int id, std::u16string name) {
  KoanFolder* folder = GetMutableFolder(id);
  if (!folder) {
    return;
  }
  base::TrimWhitespace(name, base::TRIM_ALL, &name);
  folder->name = name.empty() ? u"Folder" : std::move(name);
  Changed();
}

void KoanFolderController::SetCollapsed(int id, bool collapsed) {
  KoanFolder* folder = GetMutableFolder(id);
  if (!folder || folder->collapsed == collapsed) {
    return;
  }
  folder->collapsed = collapsed;
  StartAnimation();
  Changed();
}

void KoanFolderController::SetAllCollapsed(bool collapsed) {
  bool changed = false;
  for (const Entry& entry : GetFolders(GetActiveSpace())) {
    KoanFolder* folder = GetMutableFolder(entry.id);
    changed |= folder->collapsed != collapsed;
    folder->collapsed = collapsed;
  }
  if (changed) {
    StartAnimation();
    Changed();
  }
}

bool KoanFolderController::HasFoldersInActiveSpace(bool collapsed) const {
  return std::ranges::any_of(
      GetFolders(GetActiveSpace()), [&](const Entry& entry) {
        return GetFolder(entry.id)->collapsed == collapsed;
      });
}

void KoanFolderController::CreateSubfolder(int id) {
  if (!GetFolder(id) || GetDepth(id) + 1 >= kMaxDepth) {
    return;
  }
  CreateFolder({}, id, std::nullopt, u"Subfolder", /*rename=*/true);
}

void KoanFolderController::Unpack(int id) {
  KoanFolder* folder = GetMutableFolder(id);
  if (!folder) {
    return;
  }
  // Tabs and subfolders move up a level.
  const int parent = folder->parent;
  std::vector<tabs::TabHandle> tab_list = std::move(folder->tabs);
  for (KoanFolder& child : folders_) {
    if (child.parent == id) {
      child.parent = parent;
    }
  }
  if (KoanFolder* up = GetMutableFolder(parent)) {
    up->tabs.insert(up->tabs.end(), tab_list.begin(), tab_list.end());
  }
  std::erase_if(folders_,
                [id](const KoanFolder& folder) { return folder.id == id; });
  Changed();
}

void KoanFolderController::KeepSpace(
    const std::optional<tab_groups::TabGroupId>& space,
    const std::vector<tabs::TabInterface*>& leaving,
    bool closing) {
  TabStripModel* model = tab_strip_model();
  if (!space && !closing) {
    // Ungrouped tabs moving into a Space keep the window open.
    return;
  }
  int remaining = model->count();
  int at = model->count();
  if (space) {
    if (!model->group_model() ||
        !model->group_model()->ContainsTabGroup(*space)) {
      return;
    }
    const gfx::Range range =
        model->group_model()->GetTabGroup(*space)->ListTabs();
    remaining = static_cast<int>(range.length());
    at = static_cast<int>(range.end());
  }
  for (tabs::TabInterface* tab : leaving) {
    if (tab->GetGroup() == space) {
      --remaining;
    }
  }
  if (remaining > 0) {
    return;
  }
  // The active tab is going (it's one of them): the new one takes its place.
  const bool foreground =
      closing && std::ranges::contains(leaving, model->GetActiveTab());
  base::AutoReset<bool> busy(&normalizing_, true);
  chrome::AddTabAt(browser_view_->browser(), GURL(), at, foreground, space);
}

void KoanFolderController::Delete(int id) {
  const KoanFolder* folder = GetFolder(id);
  if (!folder) {
    return;
  }
  const std::vector<tabs::TabInterface*> tab_list = GetTabs(id, true);
  KeepSpace(folder->space, tab_list, /*closing=*/true);
  std::vector<tabs::TabHandle> handles;
  for (tabs::TabInterface* tab : tab_list) {
    handles.push_back(tab->GetHandle());
  }
  RemoveFolder(id);
  Changed();
  TabStripModel* model = tab_strip_model();
  for (const tabs::TabHandle& handle : handles) {
    tabs::TabInterface* tab = handle.Get();
    const int index = tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    if (index != TabStripModel::kNoTab) {
      model->CloseWebContentsAt(index,
                                TabCloseTypes::CLOSE_USER_GESTURE |
                                    TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    }
  }
}

void KoanFolderController::ChangeSpace(int id,
                                       const tab_groups::TabGroupId& space) {
  KoanFolder* folder = GetMutableFolder(id);
  TabStripModel* model = tab_strip_model();
  if (!folder || folder->space == space || !model->group_model() ||
      !model->group_model()->ContainsTabGroup(space)) {
    return;
  }
  const std::vector<tabs::TabInterface*> tab_list = GetTabs(id, true);
  KeepSpace(folder->space, tab_list, /*closing=*/false);
  folder = GetMutableFolder(id);
  folder->parent = 0;
  const auto set_space = [&](const auto& self, int folder_id) -> void {
    GetMutableFolder(folder_id)->space = space;
    for (int child : GetSubfolders(folder_id)) {
      self(self, child);
    }
  };
  set_space(set_space, id);
  if (!tab_list.empty()) {
    base::AutoReset<bool> busy(&normalizing_, true);
    model->AddToExistingGroup(SortedIndices(model, tab_list), space);
  }
  // Like Zen, follow the folder to its new Space.
  if (model->GetFocusedGroup()) {
    model->EnterFocusMode(space);
  }
  Changed();
}

void KoanFolderController::ConvertToSpace(int id) {
  KoanFolder* folder = GetMutableFolder(id);
  KoanWorkspaceController* workspaces =
      KoanBrowserViewController::From(browser_view_)->workspaces();
  if (!folder || !workspaces->IsEnabled()) {
    return;
  }
  TabStripModel* model = tab_strip_model();
  std::vector<tabs::TabInterface*> tab_list = GetTabs(id, true);
  if (tab_list.empty()) {
    // A Space needs a tab.
    content::WebContents* contents = chrome::AddAndReturnTabAt(
        browser_view_->browser(), GURL(), model->count(), /*foreground=*/true);
    if (tabs::TabInterface* tab =
            contents ? model->GetTabForWebContents(contents) : nullptr) {
      tab_list.push_back(tab);
    }
    if (tab_list.empty()) {
      return;
    }
  }
  KeepSpace(folder->space, tab_list, /*closing=*/false);
  folder = GetMutableFolder(id);
  const std::u16string name = folder->name;
  // The folder's tabs become the new Space's loose tabs; its subfolders
  // stay folders, at the top level of the new Space.
  const std::vector<int> subfolders = GetSubfolders(id);
  std::erase_if(folders_,
                [id](const KoanFolder& folder) { return folder.id == id; });
  for (int child : subfolders) {
    GetMutableFolder(child)->parent = 0;
  }
  const tab_groups::TabGroupId space = [&] {
    base::AutoReset<bool> busy(&normalizing_, true);
    return workspaces->CreateWorkspaceFromTabs(SortedIndices(model, tab_list),
                                               name);
  }();
  const auto set_space = [&](const auto& self, int folder_id) -> void {
    GetMutableFolder(folder_id)->space = space;
    for (int child : GetSubfolders(folder_id)) {
      self(self, child);
    }
  };
  for (int child : subfolders) {
    set_space(set_space, child);
  }
  Changed();
}

void KoanFolderController::UnloadAllTabs(int id) {
  TabStripModel* model = tab_strip_model();
  const std::vector<tabs::TabInterface*> tab_list = GetTabs(id, true);
  tabs::TabInterface* active = model->GetActiveTab();
  if (std::ranges::contains(tab_list, active)) {
    // Like Zen, switch away first: to the most recently used tab that shows
    // (not hidden in a collapsed folder), of the same Space if there is one.
    tabs::TabInterface* next = nullptr;
    const auto rank = [&](tabs::TabInterface* tab) {
      return std::make_pair(tab->GetGroup() == active->GetGroup(),
                            tab->GetContents()->GetLastActiveTimeTicks());
    };
    for (tabs::TabInterface* tab : *model) {
      if (std::ranges::contains(tab_list, tab) ||
          GetRootMostCollapsed(GetFolderOf(tab))) {
        continue;
      }
      if (!next || rank(tab) > rank(next)) {
        next = tab;
      }
    }
    if (next) {
      ActivateTab(next);
    }
  }
  for (tabs::TabInterface* tab : tab_list) {
    if (tab->IsActivated()) {
      continue;
    }
    if (auto* unit =
            resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
                tab->GetContents())) {
      unit->DiscardTab(mojom::LifecycleUnitDiscardReason::EXTERNAL);
    }
  }
  SetCollapsed(id, true);
}

void KoanFolderController::MoveTabsToFolder(
    const std::vector<tabs::TabInterface*>& tab_list,
    int id) {
  AddTabs(id, tab_list);
  // Like Zen, a collapsed folder opens to show tabs added to it, unless it
  // shows the active tab.
  if (const KoanFolder* folder = GetFolder(id);
      folder && folder->collapsed && !HasActiveTab(id)) {
    SetCollapsed(id, false);
  }
  Changed();
}

void KoanFolderController::RemoveTabsFromFolder(
    const std::vector<tabs::TabInterface*>& tab_list) {
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [&tab_list](const tabs::TabHandle& handle) {
      return std::ranges::contains(tab_list, handle.Get());
    });
  }
  Changed();
}

void KoanFolderController::ActivateTab(tabs::TabInterface* tab) {
  TabStripModel* model = tab_strip_model();
  const int index = model->GetIndexOfTab(tab);
  if (index != TabStripModel::kNoTab) {
    model->ActivateTabAt(index,
                         TabStripUserGestureDetails(
                             TabStripUserGestureDetails::GestureType::kOther));
  }
}

void KoanFolderController::ShowFolderMenu(
    int id,
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  const KoanFolder* folder = GetFolder(id);
  if (!folder || !source->GetWidget()) {
    return;
  }
  menu_folder_ = id;
  // Zen's zenFolderActions, without the icon picker and sharing.
  // The old runner (if any) uses the old models.
  menu_runner_.reset();
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  AddItem(*menu_model_, kFolderRename, u"Rename Folder…",
          kKoanFolderRenameItem);
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(*menu_model_, kFolderUnloadAll, u"Unload All Tabs",
          kKoanFolderUnloadItem);
  AddItem(*menu_model_, kFolderNewSubfolder, u"New Subfolder…",
          kKoanFolderNewSubfolderItem);
  KoanWorkspaceController* workspaces =
      KoanBrowserViewController::From(browser_view_)->workspaces();
  if (workspaces->IsEnabled()) {
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_spaces_ = workspaces->GetWorkspaces();
    for (size_t i = 0; i < menu_spaces_.size(); ++i) {
      AddItem(*submenu_model_, kSpaceBase + static_cast<int>(i),
              tab_strip_model()
                  ->group_model()
                  ->GetTabGroup(menu_spaces_[i])
                  ->visual_data()
                  ->title(),
              kKoanFolderSpaceItem);
    }
    menu_model_->AddSubMenu(kFolderChangeSpace, u"Change Space",
                            submenu_model_.get());
    menu_model_->SetElementIdentifierAt(menu_model_->GetItemCount() - 1,
                                        kKoanFolderChangeSpaceItem);
    AddItem(*menu_model_, kFolderConvertToSpace, u"Convert folder to Space",
            kKoanFolderConvertItem);
  }
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(*menu_model_, kFolderUnpack, u"Unpack Folder", kKoanFolderUnpackItem);
  AddItem(*menu_model_, kFolderDelete, u"Delete Folder", kKoanFolderDeleteItem);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void KoanFolderController::ShowCreateMenu(views::View* anchor) {
  if (!anchor->GetWidget()) {
    return;
  }
  // Zen's zenCreateNewPopup.
  // The old runner (if any) uses the old models.
  menu_runner_.reset();
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  AddItem(*menu_model_, kCreateSpace, u"Create Space", kKoanCreateSpaceItem);
  AddItem(*menu_model_, kCreateFolder, u"Create Folder", kKoanCreateFolderItem);
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(*menu_model_, kCreateSplit, u"New Split", kKoanCreateSplitItem);
  AddItem(*menu_model_, kCreateTab, u"New Tab", kKoanCreateTabItem);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(anchor->GetWidget(), nullptr,
                          anchor->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kBubbleTopRight,
                          ui::mojom::MenuSourceType::kNone);
}

void KoanFolderController::AddTabMenuItems(ui::SimpleMenuModel& menu,
                                           int tab_index) {
  TabStripModel* model = tab_strip_model();
  tabs::TabInterface* tab = model->GetTabAtIndex(tab_index);
  std::vector<tabs::TabInterface*> tab_list;
  if (model->IsTabSelected(tab_index)) {
    for (size_t index :
         model->selection_model().GetListSelectionModel().selected_indices()) {
      tab_list.push_back(model->GetTabAtIndex(static_cast<int>(index)));
    }
  } else {
    tab_list.push_back(tab);
  }
  menu_tabs_.clear();
  for (tabs::TabInterface* selected : tab_list) {
    menu_tabs_.push_back(selected->GetHandle());
  }

  // Zen lists the folders of the tab's Space that don't hold one of the
  // tabs already, then "New Folder".
  tab_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_targets_.clear();
  const std::optional<tab_groups::TabGroupId> space =
      tab->IsPinned() ? GetActiveSpace() : tab->GetGroup();
  bool in_folder = false;
  for (tabs::TabInterface* selected : tab_list) {
    in_folder |= GetFolderOf(selected) != 0;
  }
  for (const Entry& entry : GetFolders(space)) {
    if (std::ranges::any_of(tab_list, [&](tabs::TabInterface* selected) {
          return GetFolderOf(selected) == entry.id;
        })) {
      continue;
    }
    AddItem(*tab_submenu_model_,
            kTargetBase + static_cast<int>(menu_targets_.size()),
            std::u16string(2 * entry.depth, u' ') + GetFolder(entry.id)->name,
            kKoanTabFolderItem);
    menu_targets_.push_back(entry.id);
  }
  if (!menu_targets_.empty()) {
    tab_submenu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  AddItem(*tab_submenu_model_, kTabNewFolder, u"New Folder",
          kKoanTabNewFolderItem);
  if (in_folder) {
    AddItem(*tab_submenu_model_, kTabRemoveFromFolder, u"Remove from Folder",
            kKoanTabRemoveFromFolderItem);
  }

  // Right before Chromium's "Add tab to group", like Zen.
  std::optional<size_t> index;
  for (const int command : {TabStripModel::CommandAddToExistingGroup,
                            TabStripModel::CommandAddToNewGroup,
                            TabStripModel::CommandMoveToExistingWindow,
                            TabStripModel::CommandMoveTabsToNewWindow}) {
    if ((index = menu.GetIndexOfCommandId(command))) {
      break;
    }
  }
  const size_t position = index.value_or(menu.GetItemCount());
  menu.InsertSubMenuAt(position, KoanFolderController::kMoveToFolderCommandId,
                       u"Move to Folder", tab_submenu_model_.get());
  menu.SetElementIdentifierAt(position, kKoanMoveToFolderItem);
}

void KoanFolderController::ShowSearch(int id, views::View* anchor) {
  // Not while typing (the URL bar, a rename), like Zen.
  if (const views::FocusManager* focus = browser_view_->GetFocusManager();
      focus && views::IsViewClass<views::Textfield>(focus->GetFocusedView())) {
    return;
  }
  search_.reset();
  const int generation = ++search_generation_;
  search_ = KoanFolderSearch::Show(
      this, id, anchor,
      base::BindOnce(
          [](base::WeakPtr<KoanFolderController> self, int generation) {
            // Not from inside the widget's own close; and only this popup,
            // not one opened since.
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce(
                    [](base::WeakPtr<KoanFolderController> self,
                       int generation) {
                      if (self && self->search_generation_ == generation) {
                        self->search_.reset();
                      }
                    },
                    self, generation));
          },
          weak_factory_.GetWeakPtr(), generation));
}

bool KoanFolderController::IsCommandIdEnabled(int command_id) const {
  if (command_id == kFolderNewSubfolder) {
    return GetDepth(menu_folder_) + 1 < kMaxDepth;
  }
  if (command_id >= kSpaceBase && command_id < kTargetBase) {
    const KoanFolder* folder = GetFolder(menu_folder_);
    const size_t i = command_id - kSpaceBase;
    return folder && i < menu_spaces_.size() &&
           folder->space != menu_spaces_[i];
  }
  return true;
}

void KoanFolderController::ExecuteCommand(int command_id, int event_flags) {
  std::vector<tabs::TabInterface*> tab_list;
  for (const tabs::TabHandle& handle : menu_tabs_) {
    if (tabs::TabInterface* tab = handle.Get();
        tab && tab_strip_model()->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
      tab_list.push_back(tab);
    }
  }
  BrowserWindowInterface* browser = browser_view_->browser();
  switch (command_id) {
    case kFolderRename:
      pending_rename_ = menu_folder_;
      SyncRows();
      return;
    case kFolderUnloadAll:
      return UnloadAllTabs(menu_folder_);
    case kFolderNewSubfolder:
      return CreateSubfolder(menu_folder_);
    case kFolderConvertToSpace:
      return ConvertToSpace(menu_folder_);
    case kFolderUnpack:
      return Unpack(menu_folder_);
    case kFolderDelete:
      return Delete(menu_folder_);
    case kCreateSpace:
      return KoanBrowserViewController::From(browser_view_)
          ->workspaces()
          ->CreateWorkspace();
    case kCreateFolder:
      CreateFolder({}, 0, GetActiveSpace(), u"New Folder", /*rename=*/true);
      return;
    case kCreateSplit:
      if (chrome::IsCommandEnabled(browser, IDC_NEW_SPLIT_TAB)) {
        chrome::ExecuteCommand(browser, IDC_NEW_SPLIT_TAB);
      }
      return;
    case kCreateTab:
      chrome::ExecuteCommand(browser, IDC_NEW_TAB);
      return;
    case kTabNewFolder: {
      if (tab_list.empty()) {
        return;
      }
      // Zen creates the folder where the tab is: inside the tab's folder.
      CreateFolder(tab_list, GetFolderOf(tab_list.front()),
                   tab_list.front()->IsPinned() ? GetActiveSpace()
                                                : tab_list.front()->GetGroup(),
                   u"New Folder", /*rename=*/true);
      return;
    }
    case kTabRemoveFromFolder:
      return RemoveTabsFromFolder(tab_list);
  }
  if (command_id >= kSpaceBase && command_id < kTargetBase) {
    const size_t i = command_id - kSpaceBase;
    if (i < menu_spaces_.size()) {
      ChangeSpace(menu_folder_, menu_spaces_[i]);
    }
  } else if (command_id >= kTargetBase && command_id < kCommandEnd) {
    const size_t i = command_id - kTargetBase;
    if (i < menu_targets_.size() && GetFolder(menu_targets_[i])) {
      MoveTabsToFolder(tab_list, menu_targets_[i]);
    }
  }
}

bool KoanFolderController::TakePendingRename(int id) {
  if (pending_rename_ != id) {
    return false;
  }
  pending_rename_ = 0;
  return true;
}

void KoanFolderController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (folders_.empty()) {
    return;
  }
  if (change.type() == TabStripModelChange::kMoved && !normalizing_ &&
      IsEnabled()) {
    OnTabMoved(change.GetMove()->tab, change.GetMove()->to_index);
  }
  if (change.type() == TabStripModelChange::kSelectionOnly) {
    // A collapsed folder shows its active tab; the row shows the dots.
    if (selection.active_tab_changed()) {
      StartAnimation();
      ScheduleSync();
    }
    return;
  }
  Changed();
}

void KoanFolderController::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  if (!folders_.empty()) {
    Changed();
  }
}

void KoanFolderController::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                   int index) {
  if (!folders_.empty()) {
    Changed();
  }
}

void KoanFolderController::OnTabGroupChanged(const TabGroupChange& change) {
  if (!folders_.empty()) {
    // A Space closed or changed: its folders go or follow.
    Changed();
  }
}

void KoanFolderController::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  if (!folders_.empty()) {
    ScheduleSync();
  }
}

void KoanFolderController::OnTabMoved(tabs::TabInterface* tab, int to_index) {
  TabStripModel* model = tab_strip_model();
  // Only drags: other moves (unpinning, a Space moved as a whole) aren't
  // the user placing a tab.
  if (!tab || tab->IsPinned() || browser_view_->IsTabStripEditable()) {
    return;
  }
  const std::optional<tab_groups::TabGroupId> space = tab->GetGroup();
  const auto neighbour = [&](int index) -> tabs::TabInterface* {
    if (!model->ContainsIndex(index)) {
      return nullptr;
    }
    tabs::TabInterface* other = model->GetTabAtIndex(index);
    // (The other tab of a split moves along; it isn't a neighbour.)
    const bool same_split =
        tab->GetSplit().has_value() && other->GetSplit() == tab->GetSplit();
    return !other->IsPinned() && other->GetGroup() == space && !same_split
               ? other
               : nullptr;
  };
  const int current = GetFolderOf(tab);
  tabs::TabInterface* prev = neighbour(to_index - 1);
  tabs::TabInterface* next = neighbour(to_index + 1);
  int target = current;
  if (current &&
      (GetFolderOf(prev) == current || GetFolderOf(next) == current)) {
    // Still beside its own folder's tabs: stays.
  } else if (const int next_folder = GetFolderOf(next)) {
    // Dropped before a folder's tab: into that folder (right under its row);
    // but right after another folder's last tab, above this folder's row, at
    // the end of that one.
    const int prev_folder = GetFolderOf(prev);
    const std::vector<tabs::TabInterface*> next_tabs =
        GetTabs(next_folder, /*recursive=*/false);
    target = prev_folder && !next_tabs.empty() && next_tabs.front() == next
                 ? prev_folder
                 : next_folder;
  } else if (prev && !GetFolderOf(prev)) {
    // Dropped among the loose tabs: out of its folder.
    target = 0;
  }
  if (target == current) {
    return;
  }
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [tab](const tabs::TabHandle& handle) {
      return handle.Get() == tab;
    });
  }
  if (target) {
    GetMutableFolder(target)->tabs.push_back(tab->GetHandle());
  }
}

void KoanFolderController::Prune() {
  TabStripModel* model = tab_strip_model();
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [&](const tabs::TabHandle& handle) {
      tabs::TabInterface* tab = handle.Get();
      return !tab || model->GetIndexOfTab(tab) == TabStripModel::kNoTab ||
             tab->IsPinned() || tab->GetGroup() != folder.space;
    });
  }
  std::vector<int> gone;
  for (const KoanFolder& folder : folders_) {
    if (folder.space &&
        (!model->group_model() ||
         !model->group_model()->ContainsTabGroup(*folder.space))) {
      gone.push_back(folder.id);
    }
  }
  for (int id : gone) {
    RemoveFolder(id);
  }
}

void KoanFolderController::Normalize() {
  normalize_pending_ = false;
  if (!browser_view_->IsTabStripEditable()) {
    // Not while a tab is being dragged; try again after the drop.
    drag_retry_timer_.Start(FROM_HERE, base::Milliseconds(200),
                            base::BindOnce(&KoanFolderController::Changed,
                                           weak_factory_.GetWeakPtr()));
    return;
  }
  TabStripModel* model = tab_strip_model();
  std::vector<std::optional<tab_groups::TabGroupId>> spaces;
  for (const KoanFolder& folder : folders_) {
    if (!std::ranges::contains(spaces, folder.space)) {
      spaces.push_back(folder.space);
    }
  }
  base::AutoReset<bool> busy(&normalizing_, true);
  for (const std::optional<tab_groups::TabGroupId>& space : spaces) {
    // Folder tabs in folder order, a split as one unit.
    std::vector<std::vector<tabs::TabInterface*>> units;
    std::vector<split_tabs::SplitTabId> splits;
    for (const Entry& entry : GetFolders(space)) {
      for (tabs::TabInterface* tab : GetTabs(entry.id, false)) {
        const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
        if (!split) {
          units.push_back({tab});
        } else if (!std::ranges::contains(splits, *split)) {
          splits.push_back(*split);
          std::vector<tabs::TabInterface*> unit;
          for (tabs::TabInterface* other : *model) {
            if (other->GetSplit() == split) {
              unit.push_back(other);
            }
          }
          units.push_back(std::move(unit));
        }
      }
    }
    int position =
        space
            ? static_cast<int>(
                  model->group_model()->GetTabGroup(*space)->ListTabs().start())
            : model->IndexOfFirstNonPinnedTab();
    // Each unit moves back to the next free slot, so every move lands on a
    // unit boundary and never splits a group or a split.
    for (const std::vector<tabs::TabInterface*>& unit : units) {
      const int index = model->GetIndexOfTab(unit.front());
      if (index != position) {
        if (const std::optional<split_tabs::SplitTabId> split =
                unit.front()->GetSplit()) {
          model->MoveSplitTo(*split, position, /*pinned=*/false, space);
        } else {
          model->MoveWebContentsAt(index, position,
                                   /*select_after_move=*/false, space);
        }
      }
      position += static_cast<int>(unit.size());
    }
  }
}

void KoanFolderController::Changed() {
  if (normalize_pending_) {
    return;
  }
  normalize_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanFolderController> self) {
                       if (!self) {
                         return;
                       }
                       self->Prune();
                       // Folders only show (and order tabs) with vertical
                       // tabs; they are kept, and saved, meanwhile.
                       if (self->IsEnabled()) {
                         self->Normalize();
                         self->SyncRows();
                       }
                       self->Save();
                     },
                     weak_factory_.GetWeakPtr()));
}

void KoanFolderController::ScheduleSync() {
  if (sync_pending_) {
    return;
  }
  sync_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanFolderController::SyncRows,
                                weak_factory_.GetWeakPtr()));
}

void KoanFolderController::SyncRows() {
  sync_pending_ = false;
  views::View* region = browser_view_->tab_strip_view();
  if (!region || region->GetClassName() != "VerticalTabStripRegionView") {
    return;
  }
  // The containers folders live in: each Space's group view and the
  // ungrouped tabs' container.
  std::vector<std::pair<views::View*, std::optional<tab_groups::TabGroupId>>>
      containers;
  const auto find = [&](const auto& self, views::View* view) -> void {
    if (auto* group = views::AsViewClass<TabGroupView>(view)) {
      if (group->IsValid()) {
        containers.emplace_back(group, group->GetTabGroup().id());
      }
      return;
    }
    if (views::IsViewClass<UnpinnedTabContainerView>(view)) {
      containers.emplace_back(view, std::nullopt);
    }
    for (views::View* child : view->children()) {
      self(self, child);
    }
  };
  find(find, region);

  for (auto& [container, space] : containers) {
    std::vector<int> wanted;
    for (const Entry& entry : GetFolders(space)) {
      wanted.push_back(entry.id);
    }
    std::vector<KoanFolderRowView*> rows;
    for (views::View* child : container->children()) {
      if (auto* row = views::AsViewClass<KoanFolderRowView>(child)) {
        rows.push_back(row);
      }
    }
    for (KoanFolderRowView* row : rows) {
      if (row->removing()) {
        continue;
      }
      if (!std::ranges::contains(wanted, row->folder_id())) {
        // Like upstream's tabs: the container's animating layout manager
        // shrinks the row away and only then removes it (it keeps views in
        // its layout maps until then).
        row->set_removing();
        static_cast<TabCollectionAnimatingLayoutManager*>(
            container->GetLayoutManager())
            ->AnimateAndDestroyChildView(row);
        continue;
      }
      std::erase(wanted, row->folder_id());
      row->Update();
      if (TakePendingRename(row->folder_id())) {
        row->StartRename();
      }
    }
    for (int id : wanted) {
      auto* row = container->AddChildView(
          std::make_unique<KoanFolderRowView>(this, id));
      if (TakePendingRename(id)) {
        row->StartRename();
      }
    }
    container->InvalidateLayout();
  }
}

void KoanFolderController::StartAnimation() {
  animate_until_ = base::TimeTicks::Now() + kCollapseAnimation;
  // Once it's over, hide the collapsed tabs for real.
  animation_end_timer_.Start(FROM_HERE, kCollapseAnimation,
                             base::BindOnce(&KoanFolderController::SyncRows,
                                            weak_factory_.GetWeakPtr()));
}

void KoanFolderController::Save() {
  SessionService* service = SessionServiceFactory::GetForProfileIfExisting(
      browser_view_->GetProfile());
  if (!service) {
    return;
  }
  std::string data = Serialize();
  if (data == saved_) {
    return;
  }
  service->AddWindowExtraData(browser_view_->browser()->GetSessionID(),
                              kSessionKey, data);
  saved_ = std::move(data);
}

std::string KoanFolderController::Serialize() const {
  if (folders_.empty()) {
    return std::string();
  }
  TabStripModel* model = tab_strip_model();
  std::map<int, int> position;
  for (size_t i = 0; i < folders_.size(); ++i) {
    position[folders_[i].id] = static_cast<int>(i);
  }
  base::ListValue list;
  for (const KoanFolder& folder : folders_) {
    base::DictValue dict;
    dict.Set("name", folder.name);
    dict.Set("collapsed", folder.collapsed);
    dict.Set("parent", folder.parent ? position[folder.parent] : -1);
    // A Space by the index of its first tab: group ids change on restore.
    dict.Set("space",
             folder.space && model->group_model() &&
                     model->group_model()->ContainsTabGroup(*folder.space)
                 ? static_cast<int>(model->group_model()
                                        ->GetTabGroup(*folder.space)
                                        ->ListTabs()
                                        .start())
                 : -1);
    base::ListValue tab_list;
    for (tabs::TabInterface* tab : GetTabs(folder.id, false)) {
      tab_list.Append(model->GetIndexOfTab(tab));
    }
    dict.Set("tabs", std::move(tab_list));
    list.Append(std::move(dict));
  }
  return base::WriteJson(list).value_or(std::string());
}

void KoanFolderController::Restore(int first_tab_index,
                                   const std::string& data) {
  // Once per window.
  if (restored_) {
    return;
  }
  restored_ = true;
  std::optional<base::ListValue> list =
      base::JSONReader::ReadList(data, base::JSON_PARSE_RFC);
  if (!list) {
    return;
  }
  TabStripModel* model = tab_strip_model();
  const auto tab_at = [&](std::optional<int> index) -> tabs::TabInterface* {
    if (!index || *index < 0 ||
        !model->ContainsIndex(first_tab_index + *index)) {
      return nullptr;
    }
    return model->GetTabAtIndex(first_tab_index + *index);
  };
  // Folder ids by entry; a parent is always an earlier entry (Serialize
  // writes folders in the order they were made), anything else is ignored.
  std::vector<int> ids;
  for (const base::Value& value : *list) {
    ids.push_back(0);
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      continue;
    }
    KoanFolder folder;
    folder.id = next_id_++;
    ids.back() = folder.id;
    if (const std::string* name = dict->FindString("name")) {
      folder.name = base::UTF8ToUTF16(*name);
    }
    folder.collapsed = dict->FindBool("collapsed").value_or(false);
    if (tabs::TabInterface* tab = tab_at(dict->FindInt("space"))) {
      folder.space = tab->GetGroup();
    }
    if (const base::ListValue* tab_list = dict->FindList("tabs")) {
      for (const base::Value& index : *tab_list) {
        if (tabs::TabInterface* tab = tab_at(index.GetIfInt())) {
          folder.tabs.push_back(tab->GetHandle());
        }
      }
    }
    const int parent = dict->FindInt("parent").value_or(-1);
    if (parent >= 0 && parent < static_cast<int>(ids.size()) - 1 &&
        ids[parent]) {
      folder.parent = ids[parent];
      // A subfolder is in its parent's Space.
      folder.space = GetFolder(folder.parent)->space;
    }
    folders_.push_back(std::move(folder));
  }
  Changed();
}

// FolderRows ------------------------------------------------------------------

class FolderRows::Impl {
 public:
  explicit Impl(const views::View& container)
      : controller_(KoanFolderController::FromView(&container)) {
    if (!controller_) {
      return;
    }
    if (const auto* group = views::AsViewClass<TabGroupView>(&container)) {
      if (!group->IsValid()) {
        controller_ = nullptr;
        return;
      }
      space_ = group->GetTabGroup().id();
      // A collapsed group (in "All tabs") hides its folders too.
      all_hidden_ = group->IsCollapsed();
    } else {
      unpinned_ = true;
      // Ungrouped tabs are hidden while a Space is shown.
      all_hidden_ = controller_->GetActiveSpace().has_value();
    }
    entries_ = controller_->GetFolders(space_);
    if (entries_.empty()) {
      controller_ = nullptr;
      return;
    }
    const auto* state = tabs::VerticalTabStripStateController::From(
        controller_->browser_view()->browser());
    narrow_ = state && state->IsCollapsed();
    for (views::View* child : container.children()) {
      if (auto* row = views::AsViewClass<KoanFolderRowView>(child);
          row && !row->removing()) {
        rows_[row->folder_id()] = row;
      }
    }
  }

  FolderRows::Slot Place(const views::View& child,
                         int x,
                         const views::SizeBounds& size_bounds,
                         int& y,
                         views::ProposedLayout& layout) {
    if (!controller_) {
      return {};
    }
    const tabs::TabInterface* tab = GetFirstTab(child);
    int folder = controller_->GetFolderOf(tab);
    if (folder && controller_->GetFolder(folder)->space != space_) {
      folder = 0;
    }
    const auto it = std::ranges::find(entries_, folder, &Entry::id);
    if (it == entries_.end()) {
      // Loose tabs come after all folders.
      AddRows(entries_.size(), x, size_bounds, y, layout);
      return {};
    }
    AddRows(static_cast<size_t>(it - entries_.begin()) + 1, x, size_bounds, y,
            layout);
    FolderRows::Slot slot;
    int depth = it->depth;
    // A collapsed folder still shows its active tab, at the level of the
    // outermost collapsed folder (Zen's has-active).
    if (const int root = controller_->GetRootMostCollapsed(folder)) {
      slot.hidden = !ShowsActiveTab(child, controller_->tab_strip_model());
      depth = controller_->GetDepth(root);
    }
    slot.indent = narrow_ ? 0 : kFolderIndent * (depth + 1);
    return slot;
  }

  void Finish(int x,
              const views::SizeBounds& size_bounds,
              int& y,
              views::ProposedLayout& layout) {
    if (controller_) {
      AddRows(entries_.size(), x, size_bounds, y, layout);
    }
  }

  bool IsVisible(const FolderRows::Slot& slot, bool upstream_visible) const {
    return upstream_visible &&
           (!slot.hidden || (controller_ && controller_->animating()));
  }

 private:
  using Entry = KoanFolderController::Entry;

  // Adds the rows of entries [next_, end).
  void AddRows(size_t end,
               int x,
               const views::SizeBounds& size_bounds,
               int& y,
               views::ProposedLayout& layout) {
    for (; next_ < end; ++next_) {
      const Entry& entry = entries_[next_];
      const auto row = rows_.find(entry.id);
      if (row == rows_.end()) {
        // Not created yet (the folder is new): next time.
        controller_->ScheduleSync();
        continue;
      }
      const KoanFolder* folder = controller_->GetFolder(entry.id);
      const bool hidden =
          all_hidden_ ||
          (folder->parent && controller_->GetRootMostCollapsed(folder->parent));
      const int left = x + (narrow_ ? 0 : kFolderIndent * entry.depth);
      const gfx::Size size = row->second->GetPreferredSize({});
      int right = left + size.width();
      if (size_bounds.width().is_bounded()) {
        right =
            size_bounds.width().value() - (unpinned_ ? SidebarStripInset() : 0);
      }
      layout.child_layouts.emplace_back(
          row->second.get(),
          !hidden || (!all_hidden_ && controller_->animating()),
          gfx::Rect(left, y, std::max(0, right - left),
                    hidden ? 0 : size.height()));
      if (!hidden) {
        y += size.height() + kRowSpacing;
      }
    }
  }

  raw_ptr<KoanFolderController> controller_;
  std::optional<tab_groups::TabGroupId> space_;
  bool unpinned_ = false;
  bool all_hidden_ = false;
  bool narrow_ = false;
  std::vector<Entry> entries_;
  size_t next_ = 0;
  std::map<int, raw_ptr<KoanFolderRowView>> rows_;
};

FolderRows::FolderRows(const views::View& container)
    : impl_(std::make_unique<Impl>(container)) {}

FolderRows::~FolderRows() = default;

FolderRows::Slot FolderRows::Place(const views::View& child,
                                   int x,
                                   const views::SizeBounds& size_bounds,
                                   int& y,
                                   views::ProposedLayout& layout) {
  return impl_->Place(child, x, size_bounds, y, layout);
}

void FolderRows::Finish(int x,
                        const views::SizeBounds& size_bounds,
                        int& y,
                        views::ProposedLayout& layout) {
  impl_->Finish(x, size_bounds, y, layout);
}

bool FolderRows::IsVisible(const Slot& slot, bool upstream_visible) const {
  return impl_->IsVisible(slot, upstream_visible);
}

// Hooks -----------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> GetFolderSessionData(
    BrowserWindowInterface* browser) {
  KoanFolderController* controller = KoanFolderController::FromBrowser(browser);
  if (!controller) {
    return {};
  }
  return {{kSessionKey, controller->Serialize()}};
}

void RestoreFolderSessionData(
    BrowserWindowInterface* browser,
    int first_tab_index,
    const std::map<std::string, std::string>& extra_data) {
  const auto it = extra_data.find(kSessionKey);
  if (it == extra_data.end() || it->second.empty()) {
    return;
  }
  if (KoanFolderController* controller =
          KoanFolderController::FromBrowser(browser)) {
    controller->Restore(first_tab_index, it->second);
  }
}

}  // namespace koan
