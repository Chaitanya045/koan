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
#include "base/third_party/icu/icu_utf.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/serialized_user_agent_override.h"
#include "components/tabs/public/tab_group.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_vertical_tabs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_folder_views.h"
#include "koan/browser/ui/views/koan_folder_zen_assets.h"
#include "koan/browser/ui/views/koan_folders.h"
#include "koan/browser/ui/views/koan_pinned.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_sidebar_animation.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_tabs.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_stored_space.h"
#include "koan/browser/ui/views/koan_tab_drag.h"
#include "koan/browser/ui/views/koan_tab_menu.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/display/screen.h"
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
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCreateFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanMoveToFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanTabFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanTabNewFolderItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanTabRemoveFromFolderItem);

namespace {

// Folder names are cut to this many characters.
constexpr size_t kMaxFolderNameLength = 200;

// How long hidden tabs stay visible while they shrink away (upstream's tab
// layout animation takes 200 ms).
constexpr base::TimeDelta kCollapseAnimation = base::Milliseconds(250);

// Menu commands of the folder menu.
enum Command : int {
  kFolderRename = 19400,
  kFolderUnloadAll,
  kFolderNewSubfolder,
  kFolderChangeSpace,
  kFolderConvertToSpace,
  kFolderUnpack,
  kFolderDelete,
  // + index in menu_spaces_.
  kSpaceBase = 19500,
  kCommandEnd = 19600,
};

// The tab menu's commands, in folders' range (koan_tab_menu.h): "Move to
// Folder" and its items, the folders from kTabFolderFirst on (+ index in
// menu_targets_).
constexpr int kTabMoveToFolder =
    FirstTabMenuCommand(TabMenuFeatureId::kFolders);
constexpr int kTabNewFolder = kTabMoveToFolder + 1;
constexpr int kTabRemoveFromFolder = kTabMoveToFolder + 2;
constexpr int kTabFolderFirst = kTabMoveToFolder + 10;
constexpr int kTabCommandsEnd = kTabMoveToFolder + kTabMenuCommandsPerFeature;

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

// The window's Spaces (tab groups) in tab strip order.
std::vector<tab_groups::TabGroupId> SpacesInOrder(const TabStripModel* model) {
  std::vector<tab_groups::TabGroupId> spaces;
  for (int i = 0; i < model->count(); ++i) {
    const std::optional<tab_groups::TabGroupId> group =
        model->GetTabGroupForTab(i);
    if (group && !std::ranges::contains(spaces, *group)) {
      spaces.push_back(*group);
    }
  }
  return spaces;
}

// `name` cut to kMaxFolderNameLength characters (not between the two halves
// of a surrogate pair).
std::u16string CapName(std::u16string name) {
  if (name.size() > kMaxFolderNameLength) {
    size_t length = kMaxFolderNameLength;
    if (CBU16_IS_LEAD(name[length - 1])) {
      --length;
    }
    name.resize(length);
  }
  return name;
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

bool KoanFolderController::IsEnabled() const {
  // A folder belongs to a Space's pinned section, like Zen's: it needs Spaces
  // and pinned tabs.
  if (!features::IsFoldersEnabled() || !features::IsPinnedTabsEnabled() ||
      !browser_view_->GetIsNormalType()) {
    return false;
  }
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  // Koan's sidebar: its tabs are always vertical.
  return controller && controller->workspaces()->IsEnabled() && OwnsSidebar();
}

void KoanFolderController::OnAddedToWidget() {
  // Zen's "Create Folder" in the Spaces' "+" menu, after Create Space.
  KoanWorkspaceController* workspaces =
      KoanBrowserViewController::From(browser_view_)->workspaces();
  if (features::IsFoldersEnabled() && browser_view_->GetIsNormalType() &&
      workspaces->IsEnabled()) {
    KoanWorkspaceController::CreateMenuEntry entry;
    entry.label = kZenCreateFolder;
    entry.icon = &vector_icons::kFolderFlippableIcon;
    entry.element_id = kKoanCreateFolderItem;
    entry.is_enabled = base::BindRepeating(
        [](base::WeakPtr<KoanFolderController> self) {
          return self && self->IsEnabled();
        },
        weak_factory_.GetWeakPtr());
    entry.run = base::BindRepeating(&KoanFolderController::CreateEmptyFolder,
                                    weak_factory_.GetWeakPtr());
    workspaces->AddCreateMenuEntry(std::move(entry));
    // Folders follow their Spaces once Spaces have settled (see Prune()).
    spaces_subscription_ =
        workspaces->AddSpacesChangedCallback(base::BindRepeating(
            &KoanFolderController::OnSpacesChanged, base::Unretained(this)));
    if (!widget_observation_.IsObserving()) {
      widget_observation_.Observe(browser_view_->GetWidget());
    }
  }
  ScheduleSync();
}

void KoanFolderController::OnBrowserViewDestroying() {
  spaces_subscription_ = {};
  widget_observation_.Reset();
  menu_hold_.reset();
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
  if (parent) {
    space = GetFolder(parent)->space;
  }
  // A folder belongs to a Space, like Zen's.
  if (!space) {
    return 0;
  }
  KoanFolder folder;
  folder.id = next_id_++;
  folder.token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  folder.name = std::move(name);
  folder.parent = parent;
  folder.space = space;
  const int id = folder.id;
  folders_.push_back(std::move(folder));
  had_folders_ = true;
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
    if (!std::ranges::contains(all, tab)) {
      std::ranges::copy(WithSplit(tab), std::back_inserter(all));
    }
  }
  // Like Zen, a folder's tabs are pinned tabs of its Space (a folder is part
  // of the Space's pinned section): a tab moved in is pinned, and an
  // Essential leaves the Essentials keeping its pinned URL (Zen's
  // removeEssentials(tab, false))...
  KoanPinnedTabController* pinned =
      KoanBrowserViewController::From(browser_view_)->pinned_tabs();
  for (tabs::TabInterface* tab : all) {
    if (!IsPinnedInSpace(tab)) {
      pinned->PinInSpace(tab);
    }
  }
  // ...and joins the folder's Space.
  std::vector<tabs::TabInterface*> regroup;
  for (tabs::TabInterface* tab : all) {
    if (tab->GetGroup() != folder->space) {
      regroup.push_back(tab);
    }
  }
  if (!regroup.empty()) {
    model->AddToExistingGroup(SortedIndices(model, regroup), *folder->space);
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
  arrange_ = true;
}

void KoanFolderController::RemoveFolder(int id) {
  for (int child : GetSubfolders(id)) {
    RemoveFolder(child);
  }
  std::erase_if(folders_,
                [id](const KoanFolder& folder) { return folder.id == id; });
}

void KoanFolderController::SetSpace(
    int id,
    const std::optional<tab_groups::TabGroupId>& space) {
  GetMutableFolder(id)->space = space;
  for (int child : GetSubfolders(id)) {
    SetSpace(child, space);
  }
}

void KoanFolderController::Rename(int id, std::u16string name) {
  KoanFolder* folder = GetMutableFolder(id);
  if (!folder) {
    return;
  }
  base::TrimWhitespace(name, base::TRIM_ALL, &name);
  // Short enough for the session data (a session command has a size limit).
  name = CapName(std::move(name));
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

void KoanFolderController::CreateEmptyFolder() {
  CreateFolder({}, 0, GetActiveSpace(), kZenNewFolder, /*rename=*/true);
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
  arrange_ = true;
  Changed();
}

void KoanFolderController::KeepSpace(
    const std::optional<tab_groups::TabGroupId>& space,
    const std::vector<tabs::TabInterface*>& leaving,
    bool closing) {
  TabStripModel* model = tab_strip_model();
  if (!space || !model->group_model() ||
      !model->group_model()->ContainsTabGroup(*space)) {
    return;
  }
  const gfx::Range range =
      model->group_model()->GetTabGroup(*space)->ListTabs();
  int remaining = static_cast<int>(range.length());
  const int at = static_cast<int>(range.end());
  for (tabs::TabInterface* tab : leaving) {
    if (tab->GetGroup() == space) {
      --remaining;
    }
  }
  if (remaining > 0) {
    return;
  }
  // The active tab is going (it's one of them): the empty tab takes its
  // place.
  const bool foreground =
      closing && std::ranges::contains(leaving, model->GetActiveTab());
  base::AutoReset<bool> busy(&normalizing_, true);
  EnsureEmptyTab(browser_view_->browser(), space, at, foreground);
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
  GetMutableFolder(id)->parent = 0;
  SetSpace(id, space);
  if (!tab_list.empty()) {
    base::AutoReset<bool> busy(&normalizing_, true);
    model->AddToExistingGroup(SortedIndices(model, tab_list), space);
  }
  // Like Zen, follow the folder to its new Space.
  KoanBrowserViewController::From(browser_view_)->workspaces()->SwitchTo(space);
  arrange_ = true;
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
    // A Space needs a tab: a new Space's is its empty tab.
    if (tabs::TabInterface* tab =
            EnsureEmptyTab(browser_view_->browser(), std::nullopt,
                           model->count(), /*foreground=*/true)) {
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
    return workspaces->CreateSpaceFromTabs(SortedIndices(model, tab_list),
                                           {.name = name});
  }();
  for (int child : subfolders) {
    SetSpace(child, space);
  }
  arrange_ = true;
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
  // Koan's unload, as for pinned tabs (it switches away itself if no other
  // tab shows).
  KoanBrowserViewController::From(browser_view_)
      ->pinned_tabs()
      ->UnloadTabs(tab_list);
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
  // A split leaves as a unit.
  std::vector<tabs::TabInterface*> all;
  for (tabs::TabInterface* tab : tab_list) {
    for (tabs::TabInterface* each : WithSplit(tab)) {
      if (!std::ranges::contains(all, each)) {
        all.push_back(each);
      }
    }
  }
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [&all](const tabs::TabHandle& handle) {
      return std::ranges::contains(all, handle.Get());
    });
  }
  arrange_ = true;
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
  // No renaming in a sidebar that shows icons only, like Zen.
  if (source->width() >= kMinExpandedRowWidth) {
    AddItem(*menu_model_, kFolderRename, kZenRenameFolder,
            kKoanFolderRenameItem);
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  AddItem(*menu_model_, kFolderUnloadAll, kZenUnloadAllTabs,
          kKoanFolderUnloadItem);
  AddItem(*menu_model_, kFolderNewSubfolder, kZenNewSubfolder,
          kKoanFolderNewSubfolderItem);
  KoanWorkspaceController* workspaces =
      KoanBrowserViewController::From(browser_view_)->workspaces();
  if (workspaces->IsEnabled()) {
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_spaces_.clear();
    for (const KoanWorkspaceController::SpaceInfo& space :
         workspaces->GetSpaces()) {
      AddItem(*submenu_model_,
              kSpaceBase + static_cast<int>(menu_spaces_.size()),
              KoanWorkspaceController::JoinTitle(space.icon, space.name),
              kKoanFolderSpaceItem);
      menu_spaces_.push_back(space.id);
    }
    menu_model_->AddSubMenu(kFolderChangeSpace, kZenChangeSpace,
                            submenu_model_.get());
    menu_model_->SetElementIdentifierAt(menu_model_->GetItemCount() - 1,
                                        kKoanFolderChangeSpaceItem);
    AddItem(*menu_model_, kFolderConvertToSpace, kZenConvertFolderToSpace,
            kKoanFolderConvertItem);
  }
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(*menu_model_, kFolderUnpack, kZenUnpackFolder, kKoanFolderUnpackItem);
  AddItem(*menu_model_, kFolderDelete, kZenDeleteFolder, kKoanFolderDeleteItem);
  // The sidebar stays out while the menu is open (compact mode's peek, like
  // the Space menu's).
  menu_hold_ = HoldSidebarOpen(source);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(
          [](base::WeakPtr<KoanFolderController> self) {
            if (self) {
              self->menu_hold_.reset();
            }
          },
          weak_factory_.GetWeakPtr()));
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
  // No menu, or it ran to its end (macOS).
  if (!menu_runner_->IsRunning()) {
    menu_hold_.reset();
  }
}

void KoanFolderController::AddTabMenuItems(TabMenuContext& context) {
  TabStripModel* model = tab_strip_model();
  tabs::TabInterface* tab = model->GetTabAtIndex(context.index);
  std::vector<tabs::TabInterface*> tab_list;
  if (model->IsTabSelected(context.index)) {
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
  ui::SimpleMenuModel* submenu = context.AddSubmenu();
  menu_targets_.clear();
  const std::optional<tab_groups::TabGroupId> space =
      tab->IsPinned() ? GetActiveSpace() : tab->GetGroup();
  bool in_folder = false;
  for (tabs::TabInterface* selected : tab_list) {
    in_folder |= GetFolderOf(selected) != 0;
  }
  for (const Entry& entry : GetFolders(space)) {
    const int command_id =
        kTabFolderFirst + static_cast<int>(menu_targets_.size());
    if (command_id == kTabCommandsEnd) {
      break;
    }
    if (std::ranges::any_of(tab_list, [&](tabs::TabInterface* selected) {
          return GetFolderOf(selected) == entry.id;
        })) {
      continue;
    }
    AddItem(*submenu, command_id,
            std::u16string(2 * entry.depth, u' ') + GetFolder(entry.id)->name,
            kKoanTabFolderItem);
    menu_targets_.push_back(entry.id);
  }
  if (!menu_targets_.empty()) {
    submenu->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  AddItem(*submenu, kTabNewFolder, kZenNewFolder, kKoanTabNewFolderItem);
  if (in_folder) {
    AddItem(*submenu, kTabRemoveFromFolder, u"Remove from Folder",
            kKoanTabRemoveFromFolderItem);
  }

  // Right before Chromium's "Add tab to group", like Zen.
  ui::SimpleMenuModel& menu = *context.menu;
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
  menu.InsertSubMenuAt(position, kTabMoveToFolder, kZenMoveToFolder, submenu);
  menu.SetElementIdentifierAt(position, kKoanMoveToFolderItem);
}

void KoanFolderController::ExecuteTabMenuCommand(int command_id) {
  const std::vector<tabs::TabInterface*> tab_list = GetMenuTabs();
  if (tab_list.empty()) {
    return;
  }
  if (command_id == kTabNewFolder) {
    // Zen creates the folder where the tab is: inside the tab's folder.
    CreateFolder(tab_list, GetFolderOf(tab_list.front()),
                 tab_list.front()->IsPinned() ? GetActiveSpace()
                                              : tab_list.front()->GetGroup(),
                 kZenNewFolder, /*rename=*/true);
  } else if (command_id == kTabRemoveFromFolder) {
    RemoveTabsFromFolder(tab_list);
  } else if (const size_t i = command_id - kTabFolderFirst;
             command_id >= kTabFolderFirst && i < menu_targets_.size() &&
             GetFolder(menu_targets_[i])) {
    MoveTabsToFolder(tab_list, menu_targets_[i]);
  }
}

std::vector<tabs::TabInterface*> KoanFolderController::GetMenuTabs() const {
  std::vector<tabs::TabInterface*> tab_list;
  for (const tabs::TabHandle& handle : menu_tabs_) {
    if (tabs::TabInterface* tab = handle.Get();
        tab && tab_strip_model()->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
      tab_list.push_back(tab);
    }
  }
  return tab_list;
}

void KoanFolderController::ShowSearch(int id, views::View* anchor) {
  // Not while typing (the URL bar, a rename), like Zen.
  if (const views::FocusManager* focus = browser_view_->GetFocusManager();
      focus && views::IsViewClass<views::Textfield>(focus->GetFocusedView())) {
    return;
  }
  // Nor during Koan's tab drag (the hovered folder's row pressed to drag it).
  if (KoanWorkspaceController* spaces = Spaces();
      spaces && spaces->tab_drag() && spaces->tab_drag()->IsDragging()) {
    return;
  }
  search_.reset();
  // In compact mode, the peeked sidebar stays out while the popup is open.
  KoanBrowserViewController::From(browser_view_)
      ->compact_mode()
      ->HoldSidebarForBubble();
  const int generation = ++search_generation_;
  search_folder_ = id;
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
  if (command_id >= kSpaceBase && command_id < kCommandEnd) {
    const KoanFolder* folder = GetFolder(menu_folder_);
    const size_t i = command_id - kSpaceBase;
    return folder && i < menu_spaces_.size() &&
           folder->space != menu_spaces_[i];
  }
  return true;
}

void KoanFolderController::ExecuteCommand(int command_id, int event_flags) {
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
  }
  if (const size_t i = command_id - kSpaceBase;
      command_id >= kSpaceBase && i < menu_spaces_.size()) {
    ChangeSpace(menu_folder_, menu_spaces_[i]);
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
  if (change.type() == TabStripModelChange::kInserted) {
    std::vector<tabs::TabInterface*> added;
    for (const auto& inserted : change.GetInsert()->contents) {
      added.push_back(inserted.tab);
    }
    OnTabsAdded(added);
  }
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      // Gone to another window: its folder is this window's, so it forgets
      // it. (A closed tab keeps it, to come back in its folder.)
      if (removed.remove_reason ==
          TabRemovedReason::kInsertedIntoOtherTabStrip) {
        SetTabSessionData(removed.contents, kFolderSessionKey, std::string());
      }
    }
  }
  if (change.type() == TabStripModelChange::kMoved && !normalizing_ &&
      IsEnabled()) {
    OnTabMoved(change.GetMove()->tab, change.GetMove()->from_index);
  }
  if (change.type() == TabStripModelChange::kSelectionOnly) {
    // A collapsed folder shows its active tab; the row shows the dots. (Its
    // other tabs stay hidden: animate only when a collapsed folder gains or
    // loses the active tab.)
    if (selection.active_tab_changed()) {
      if (GetRootMostCollapsed(GetFolderOf(selection.old_tab)) ||
          GetRootMostCollapsed(GetFolderOf(selection.new_tab))) {
        StartAnimation();
      }
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

void KoanFolderController::OnTabChangedAt(tabs::TabInterface* tab,
                                          TabChangeType change_type) {
  if (folders_.empty() || !GetFolderOf(tab)) {
    return;
  }
  // Unpinned, or made an Essential (Koan's pinned tabs refresh the tab when
  // they change its pinned state in place): it leaves its folder. Navigated
  // or retitled: the stored folders keep its page.
  if (!IsPinnedInSpace(tab) || change_type == TabChangeType::kAll) {
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

void KoanFolderController::OnTabMoved(tabs::TabInterface* tab, int from_index) {
  // Only drags: other moves (unpinning, a Space moved as a whole) aren't
  // the user placing a tab.
  if (!tab || browser_view_->IsTabStripEditable()) {
    return;
  }
  // Decided when the drag ends (FinishDrag()): a drag moves the tab on every
  // step, and may be cancelled. Remember where it started.
  if (!std::ranges::contains(dragged_, tab, [](const DraggedTab& dragged) {
        return dragged.tab.Get();
      })) {
    dragged_.push_back({tab->GetHandle(), from_index});
  }
}

void KoanFolderController::FinishDrag() {
  TabStripModel* model = tab_strip_model();
  // Dropped on a folder's row: into the folder at its start, or beside it
  // (Zen's bands).
  const RowDrop drop = RowDropAt(std::exchange(drop_point_, gfx::Point()));
  std::vector<tabs::TabInterface*> done;
  for (const DraggedTab& dragged : std::exchange(dragged_, {})) {
    tabs::TabInterface* tab = dragged.tab.Get();
    const int index = tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    // Gone (closed, another window), an Essential, or decided with its split.
    if (index == TabStripModel::kNoTab || tab->IsPinned() ||
        std::ranges::contains(done, tab)) {
      continue;
    }
    const std::vector<tabs::TabInterface*> unit = WithSplit(tab);
    done.insert(done.end(), unit.begin(), unit.end());
    // Back where it started (the drag was cancelled, or dragged back): it
    // keeps its folder.
    if (index == dragged.from_index) {
      continue;
    }
    const KoanFolder* row = GetFolder(drop.folder);
    if (row && row->space == tab->GetGroup()) {
      if (drop.into) {
        MoveToFolderStart(unit, row->id);
      } else {
        // Before or after the folder: in the pinned section, in the folder
        // that holds it (if any).
        SetFolder(unit, row->parent);
      }
    } else {
      ApplyDragRule(unit);
    }
  }
}

KoanFolderController::RowDrop KoanFolderController::RowDropAt(
    const gfx::Point& screen_point) const {
  views::View* region = browser_view_->tab_strip_view();
  RowDrop drop;
  if (!region || screen_point.IsOrigin()) {
    return drop;
  }
  const auto find = [&](const auto& self, views::View* view) -> void {
    for (views::View* child : view->children()) {
      if (drop.folder) {
        return;
      }
      auto* row = views::AsViewClass<KoanFolderRowView>(child);
      if (!row) {
        self(self, child);
        continue;
      }
      const gfx::Rect bounds = row->GetBoundsInScreen();
      const KoanFolder* folder = GetFolder(row->folder_id());
      if (!folder || row->removing() || !row->IsDrawn() || bounds.IsEmpty() ||
          !bounds.Contains(screen_point)) {
        continue;
      }
      const float fraction =
          static_cast<float>(screen_point.y() - bounds.y()) / bounds.height();
      const size_t items = GetTabs(folder->id, /*recursive=*/false).size() +
                           GetSubfolders(folder->id).size();
      drop.folder = folder->id;
      drop.into = FolderRowZone(fraction, folder->collapsed, items) ==
                  FolderZone::kInto;
    }
  };
  find(find, region);
  return drop;
}

void KoanFolderController::MoveToFolderStart(
    const std::vector<tabs::TabInterface*>& unit,
    int id) {
  TabStripModel* model = tab_strip_model();
  const std::vector<tabs::TabInterface*> first = GetTabs(id, false);
  MoveTabsToFolder(unit, id);
  // Before the folder's first tab (a split stays where it is: moves never
  // cut one).
  if (first.empty() || unit.size() > 1 || first.front() == unit.front() ||
      first.front()->GetSplit()) {
    return;
  }
  const int from = model->GetIndexOfTab(unit.front());
  const int to = model->GetIndexOfTab(first.front());
  if (from == TabStripModel::kNoTab || to == TabStripModel::kNoTab) {
    return;
  }
  base::AutoReset<bool> busy(&normalizing_, true);
  model->MoveWebContentsAt(from, from < to ? to - 1 : to,
                           /*select_after_move=*/false,
                           unit.front()->GetGroup());
}

std::vector<tabs::TabInterface*> KoanFolderController::WithSplit(
    tabs::TabInterface* tab) const {
  const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
  if (!split) {
    return {tab};
  }
  std::vector<tabs::TabInterface*> unit;
  for (tabs::TabInterface* other : *tab_strip_model()) {
    if (other->GetSplit() == split) {
      unit.push_back(other);
    }
  }
  return unit;
}

void KoanFolderController::SetFolder(
    const std::vector<tabs::TabInterface*>& unit,
    int target) {
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [&unit](const tabs::TabHandle& handle) {
      return std::ranges::contains(unit, handle.Get());
    });
  }
  if (KoanFolder* folder = GetMutableFolder(target)) {
    for (tabs::TabInterface* tab : unit) {
      folder->tabs.push_back(tab->GetHandle());
    }
  }
  // In the pinned section (a folder, or beside one): pinned, where it was
  // dropped. (Pinning takes the split along.)
  if (!IsPinnedInSpace(unit.front())) {
    KoanBrowserViewController::From(browser_view_)
        ->pinned_tabs()
        ->PinInSpace(unit.front());
  }
  arrange_ = true;
}

void KoanFolderController::DropInFolder(
    const std::vector<tabs::TabInterface*>& tab_list,
    int id) {
  if (!IsEnabled()) {
    return;
  }
  std::vector<tabs::TabInterface*> done;
  for (tabs::TabInterface* tab : tab_list) {
    if (std::ranges::contains(done, tab) ||
        tab_strip_model()->GetIndexOfTab(tab) == TabStripModel::kNoTab) {
      continue;
    }
    const std::vector<tabs::TabInterface*> unit = WithSplit(tab);
    done.insert(done.end(), unit.begin(), unit.end());
    const KoanFolder* folder = GetFolder(id);
    if (folder && folder->space == tab->GetGroup() && !tab->IsPinned()) {
      // (Pins it, where it was dropped.)
      SetFolder(unit, id);
    } else if (GetFolderOf(tab)) {
      // Out of its folder, pinned or not as the drop left it.
      for (KoanFolder& each : folders_) {
        std::erase_if(each.tabs, [&unit](const tabs::TabHandle& handle) {
          return std::ranges::contains(unit, handle.Get());
        });
      }
      arrange_ = true;
    }
  }
  Changed();
}

bool KoanFolderController::CanHoldFolder(int parent, int id) const {
  if (!GetFolder(id)) {
    return false;
  }
  if (!parent) {
    return true;
  }
  for (const KoanFolder* folder = GetFolder(parent); folder;
       folder = GetFolder(folder->parent)) {
    if (folder->id == id) {
      return false;
    }
  }
  // How many levels `id`'s subfolders add below it.
  const auto height = [this](const auto& self, int folder) -> int {
    int levels = 0;
    for (int sub : GetSubfolders(folder)) {
      levels = std::max(levels, 1 + self(self, sub));
    }
    return levels;
  };
  return GetDepth(parent) + 1 + height(height, id) < kMaxDepth;
}

void KoanFolderController::DropFolder(int id, int parent, int before) {
  if (!IsEnabled() || !CanHoldFolder(parent, id)) {
    return;
  }
  if (const std::optional<tab_groups::TabGroupId> space = GetActiveSpace();
      space && GetFolder(id)->space != space) {
    ChangeSpace(id, *space);
  }
  const KoanFolder* holder = GetFolder(parent);
  if (parent && (!holder || holder->space != GetFolder(id)->space)) {
    return;
  }
  // A folder's subfolders show in the order of `folders_`.
  const auto from = std::ranges::find(folders_, id, &KoanFolder::id);
  KoanFolder moved = std::move(*from);
  folders_.erase(from);
  moved.parent = parent;
  auto to = std::ranges::find(folders_, before, &KoanFolder::id);
  if (!before || to == folders_.end() || to->parent != parent) {
    to = folders_.end();
  }
  folders_.insert(to, std::move(moved));
  arrange_ = true;
  Changed();
}

void KoanFolderController::ApplyDragRule(
    const std::vector<tabs::TabInterface*>& unit) {
  TabStripModel* model = tab_strip_model();
  const std::optional<tab_groups::TabGroupId> space = unit.front()->GetGroup();
  // The tabs next to the unit in its Space (not Essentials).
  const auto neighbour = [&](int index) -> tabs::TabInterface* {
    if (!model->ContainsIndex(index)) {
      return nullptr;
    }
    tabs::TabInterface* other = model->GetTabAtIndex(index);
    return !other->IsPinned() && other->GetGroup() == space ? other : nullptr;
  };
  const int current = GetFolderOf(unit.front());
  tabs::TabInterface* prev = neighbour(model->GetIndexOfTab(unit.front()) - 1);
  tabs::TabInterface* next = neighbour(model->GetIndexOfTab(unit.back()) + 1);
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
  } else if ((prev && !GetFolderOf(prev)) ||
             (!prev && next && !GetFolderOf(next))) {
    // Dropped among the tabs outside folders (pinned or not, or at the top
    // of the Space): out of its folder.
    target = 0;
  }
  if (target == current) {
    return;
  }
  if (target) {
    // A drop among the folders is inside the pinned section: the tab is
    // pinned where it was dropped.
    SetFolder(unit, target);
    return;
  }
  // Out of folders: pinned or not as Koan's pinned tabs decide from where it
  // was dropped (among the pinned tabs or the normal ones).
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [&unit](const tabs::TabHandle& handle) {
      return std::ranges::contains(unit, handle.Get());
    });
  }
}

void KoanFolderController::OnTabsAdded(
    const std::vector<tabs::TabInterface*>& added) {
  for (tabs::TabInterface* tab : added) {
    const std::string token =
        GetTabSessionData(tab->GetContents(), kFolderSessionKey);
    const auto folder = std::ranges::find(folders_, token, &KoanFolder::token);
    if (token.empty() || folder == folders_.end() ||
        GetFolderOf(tab) == folder->id) {
      continue;
    }
    // In one folder only (Prune() runs later).
    for (KoanFolder& other : folders_) {
      std::erase_if(other.tabs, [tab](const tabs::TabHandle& handle) {
        return handle.Get() == tab;
      });
    }
    folder->tabs.push_back(tab->GetHandle());
    arrange_ = true;
  }
}

void KoanFolderController::FinishRestore() {
  restoring_ = false;
  const std::vector<tab_groups::TabGroupId> spaces =
      SpacesInOrder(tab_strip_model());
  for (const auto& [id, place] : restored_spaces_) {
    const KoanFolder* folder = GetFolder(id);
    if (!folder) {
      continue;
    }
    // A subfolder is in its parent's Space.
    if (const KoanFolder* parent = GetFolder(folder->parent)) {
      if (!restored_spaces_.contains(parent->id)) {
        SetSpace(id, parent->space);
      }
      continue;
    }
    // The Space its tabs came back in; for folders without tabs, the one in
    // the same place among the window's Spaces.
    const std::vector<tabs::TabInterface*> tab_list = GetTabs(id, true);
    std::optional<tab_groups::TabGroupId> space;
    if (!tab_list.empty()) {
      space = tab_list.front()->GetGroup();
    } else if (const size_t i = spaces_before_restore_ + place;
               place >= 0 && i < spaces.size()) {
      space = spaces[i];
    }
    // Every folder has a Space: else the one shown (or the first).
    if (!space) {
      space = tab_strip_model()->GetFocusedGroup();
    }
    if (!space && !spaces.empty()) {
      space = spaces.front();
    }
    SetSpace(id, space);
  }
  restored_spaces_.clear();
  arrange_ = true;
  Changed();
}

void KoanFolderController::Prune(bool spaces_settled) {
  TabStripModel* model = tab_strip_model();
  for (KoanFolder& folder : folders_) {
    std::erase_if(folder.tabs, [&](const tabs::TabHandle& handle) {
      tabs::TabInterface* tab = handle.Get();
      if (!tab || model->GetIndexOfTab(tab) == TabStripModel::kNoTab ||
          tab->IsPinned()) {
        // Closed, in another window, or made an Essential.
        return true;
      }
      // Out of its Space only for a moment (upstream ungroups and regroups a
      // tab it pins, moves out of the group's range or that an extension
      // ungroups; Spaces put loose tabs back): it stays.
      if (!tab->GetGroup()) {
        return false;
      }
      if (!IsPinnedInSpace(tab)) {
        // Unpinned.
        return true;
      }
      // Moved to another Space (decided once Spaces have put back the tabs
      // they put back).
      return spaces_settled && tab->GetGroup() != folder.space;
    });
  }
  if (!spaces_settled) {
    return;
  }
  // A Space that is still gone after Spaces' update (which brings back a
  // Space whose group went for a moment) takes its folders along.
  std::vector<int> gone;
  for (const KoanFolder& folder : folders_) {
    if (!folder.space || !model->group_model() ||
        !model->group_model()->ContainsTabGroup(*folder.space)) {
      gone.push_back(folder.id);
    }
  }
  for (int id : gone) {
    RemoveFolder(id);
  }
}

void KoanFolderController::ArrangeFolders() {
  if (folders_.empty() || IsClosing() || !IsEnabled()) {
    return;
  }
  // A drag not decided yet (the drop decides first), an update to come, or
  // a drag going on: Update() arranges after them.
  if (!dragged_.empty() || update_pending_ ||
      !browser_view_->IsTabStripEditable()) {
    arrange_ = true;
    Changed();
    return;
  }
  // From what the tabs are now (a tab Koan's pinned tabs just unpinned has
  // left its folder).
  Prune(/*spaces_settled=*/false);
  TabStripModel* model = tab_strip_model();
  std::vector<std::optional<tab_groups::TabGroupId>> spaces;
  for (const KoanFolder& folder : folders_) {
    if (!std::ranges::contains(spaces, folder.space)) {
      spaces.push_back(folder.space);
    }
  }
  base::AutoReset<bool> busy(&normalizing_, true);
  for (const std::optional<tab_groups::TabGroupId>& space : spaces) {
    if (!space || !model->group_model()->ContainsTabGroup(*space)) {
      continue;
    }
    // Folder tabs in folder order, a split as one unit. A tab out of the
    // Space for a moment (a temporary ungroup) is left to Spaces, which put
    // it back.
    std::vector<std::vector<tabs::TabInterface*>> units;
    std::vector<tabs::TabInterface*> placed;
    for (const Entry& entry : GetFolders(space)) {
      for (tabs::TabInterface* tab : GetTabs(entry.id, false)) {
        if (tab->GetGroup() != space || !IsPinnedInSpace(tab) ||
            std::ranges::contains(placed, tab)) {
          continue;
        }
        std::vector<tabs::TabInterface*> unit = WithSplit(tab);
        placed.insert(placed.end(), unit.begin(), unit.end());
        units.push_back(std::move(unit));
      }
    }
    int position = static_cast<int>(
        model->group_model()->GetTabGroup(*space)->ListTabs().start());
    // After the Space's other pinned tabs at its top, like Zen
    // (createFolder); the folders' tabs are pinned too, so all of them stay
    // in the pinned section. (A pinned tab the user put below the folders
    // stays there.) A split counts as one: moves never cut one.
    while (model->ContainsIndex(position) &&
           model->GetTabGroupForTab(position) == space) {
      const std::vector<tabs::TabInterface*> unit =
          WithSplit(model->GetTabAtIndex(position));
      if (!std::ranges::all_of(unit, [&](tabs::TabInterface* tab) {
            return IsPinnedForUser(tab) && !GetFolderOf(tab);
          })) {
        break;
      }
      position += static_cast<int>(unit.size());
    }
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
  if (update_pending_) {
    return;
  }
  update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanFolderController::Update,
                                weak_factory_.GetWeakPtr()));
}

void KoanFolderController::Update() {
  update_pending_ = false;
  // Once the restored tabs are in (FinishRestore()). And never while the
  // window closes: its tabs and Spaces go, and the session must keep its
  // folders (closing the last window on macOS keeps the app, and the window
  // comes back from the session).
  if (restoring_ || IsClosing()) {
    return;
  }
  if (!browser_view_->IsTabStripEditable() && (!dragged_.empty() || arrange_)) {
    // Still dragging: the pointer is where the tab will be dropped
    // (FinishDrag()). Look again soon.
    drop_point_ = display::Screen::Get()->GetCursorScreenPoint();
    drag_retry_timer_.Start(FROM_HERE, base::Milliseconds(50),
                            base::BindOnce(&KoanFolderController::Changed,
                                           weak_factory_.GetWeakPtr()));
    return;
  }
  if (!dragged_.empty()) {
    FinishDrag();
  }
  Prune(/*spaces_settled=*/false);
  if (std::exchange(arrange_, false)) {
    ArrangeFolders();
  }
  if (IsEnabled()) {
    SyncRows();
  }
  Save();
}

bool KoanFolderController::IsClosing() const {
  const TabStripModel* model = tab_strip_model();
  return model->empty() || model->closing_all() ||
         browser_view_->browser()->IsDeleteScheduled();
}

void KoanFolderController::OnSpacesChanged() {
  if (restoring_ || IsClosing()) {
    return;
  }
  if (!std::exchange(recreate_checked_, true)) {
    // Not from Spaces' own update: the tab strip changes.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanFolderController::RecreateStoredFolders,
                                  weak_factory_.GetWeakPtr()));
  }
  // Stored with each Spaces update, as #159's stored Spaces are, and when the
  // window becomes active (OnWidgetActivationChanged()): what is stored is
  // the folders of the window the user is in, or was in last.
  if (stores_folders_) {
    StoreFolders();
  }
  if (folders_.empty()) {
    return;
  }
  Prune(/*spaces_settled=*/true);
  Changed();
}

KoanWorkspaceController* KoanFolderController::Spaces() const {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return controller ? controller->workspaces() : nullptr;
}

std::vector<tab_groups::TabGroupId> KoanFolderController::StoredSpaceOrder()
    const {
  // Spaces' own list (no ephemeral groups), as #159 stores the Spaces.
  // (None while the window's controllers go away.)
  std::vector<tab_groups::TabGroupId> spaces;
  if (KoanWorkspaceController* workspaces = Spaces()) {
    for (const KoanWorkspaceController::SpaceInfo& space :
         workspaces->GetSpaces()) {
      spaces.push_back(space.id);
    }
  }
  return spaces;
}

base::ListValue KoanFolderController::StoredFolders() const {
  const std::vector<tab_groups::TabGroupId> spaces = StoredSpaceOrder();
  base::ListValue list;
  for (const KoanFolder& folder : folders_) {
    const auto space =
        folder.space ? std::ranges::find(spaces, *folder.space) : spaces.end();
    if (space == spaces.end()) {
      continue;
    }
    base::ListValue tab_list;
    for (tabs::TabInterface* tab : GetTabs(folder.id, /*recursive=*/false)) {
      content::WebContents* contents = tab->GetContents();
      // (Also while it isn't loaded: restored, unloaded or imported.)
      tab_list.Append(base::DictValue()
                          .Set("url", CurrentTabUrl(contents).spec())
                          .Set("title", contents->GetTitle())
                          .Set("pinned", GetTabSessionData(
                                             contents, kPinnedTabSessionKey)));
    }
    base::DictValue dict;
    dict.Set("token", folder.token);
    dict.Set("name", folder.name);
    dict.Set("collapsed", folder.collapsed);
    if (const KoanFolder* parent = GetFolder(folder.parent)) {
      dict.Set("parent", parent->token);
    }
    // Its Space by id (#159's stored Spaces), and by place for a Space
    // stored without one.
    dict.Set("space_id", Spaces()->GetStoredSpaceId(*space));
    dict.Set("space", static_cast<int>(space - spaces.begin()));
    dict.Set("tabs", std::move(tab_list));
    list.Append(std::move(dict));
  }
  // After a crash, the stored folders this window didn't bring back stay.
  for (const base::Value& value : carried_) {
    const std::string* token = value.GetDict().FindString("token");
    if (token && !std::ranges::contains(folders_, *token, &KoanFolder::token)) {
      list.Append(value.Clone());
    }
  }
  return list;
}

void KoanFolderController::StoreFolders() {
  // (Folders off by a flag in this run: the stored ones stay.)
  if (!IsEnabled()) {
    return;
  }
  Profile* profile = browser_view_->GetProfile();
  base::ListValue list = StoredFolders();
  // After a crash the session may still be restored (into this window): the
  // stored folders stay until this window's change.
  if (!first_stored_) {
    first_stored_ = list.Clone();
    may_store_ =
        ExitTypeService::GetLastSessionExitType(profile) != ExitType::kCrashed;
  }
  if (!may_store_) {
    if (list == *first_stored_) {
      return;
    }
    may_store_ = true;
  }
  // The window the user is in, or was in last.
  const views::Widget* widget = browser_view_->GetWidget();
  const BrowserWindowInterface* last_active =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  // (Compared with what is stored, which another window may have written.)
  // A window that never had a folder (⌘N next to one that has them) leaves
  // the stored ones alone: quitting from it doesn't lose them.
  const base::ListValue& stored =
      profile->GetPrefs()->GetList(prefs::kFolderList);
  if (!((widget && widget->IsActive()) || !last_active ||
        last_active == browser_view_->browser()) ||
      list == stored || (!had_folders_ && list.empty())) {
    return;
  }
  profile->GetPrefs()->SetList(prefs::kFolderList, std::move(list));
}

void KoanFolderController::OnWidgetActivationChanged(views::Widget* widget,
                                                     bool active) {
  // The user is in this window now (another one may have stored its own
  // folders, or none, and then closed).
  if (active && stores_folders_ && !restoring_ && !IsClosing()) {
    StoreFolders();
  }
}

void KoanFolderController::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

void KoanFolderController::RecreateStoredFolders() {
  // From now on this window keeps the stored folders.
  stores_folders_ = true;
  Profile* profile = browser_view_->GetProfile();
  // Only a window that opened without a session restore: startup set to the
  // New Tab page or to pages (Zen's pinned-only restore when not resuming
  // the session), not after a crash (the user may still restore it), and
  // the profile's only window (one opened next to another starts without).
  // (A session restored at startup, also one a restart forces, sets
  // restored_last_session().)
  const PrefService* prefs = profile->GetPrefs();
  if (!IsEnabled() || restored_from_session_ || !folders_.empty() ||
      IsClosing() || SessionRestore::IsRestoring(profile) ||
      profile->restored_last_session() ||
      SessionStartupPref::GetStartupPref(profile).type ==
          SessionStartupPref::LAST) {
    return;
  }
  if (ExitTypeService::GetLastSessionExitType(profile) == ExitType::kCrashed) {
    // Kept until the user restores the session or a later start brings
    // them back.
    carried_ = prefs->GetList(prefs::kFolderList).Clone();
    return;
  }
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser != browser_view_->browser() &&
        browser->GetProfile() == profile &&
        browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
      return;
    }
  }
  TabStripModel* model = tab_strip_model();
  const std::vector<tab_groups::TabGroupId> spaces = StoredSpaceOrder();
  std::vector<std::string> space_ids;
  for (const tab_groups::TabGroupId& space : spaces) {
    space_ids.push_back(Spaces()->GetStoredSpaceId(space));
  }
  // The folders first, so their tabs join them as they come
  // (OnTabsAdded()). A Space that is no longer there takes its folders
  // along; a parent is an earlier entry.
  std::vector<std::pair<std::string, const base::ListValue*>> tab_lists;
  for (const base::Value& value : prefs->GetList(prefs::kFolderList)) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* token = dict ? dict->FindString("token") : nullptr;
    // Its Space: the one with its stored id, else the one at its place (a
    // Space that isn't there, as fewer came back, gives its folders to the
    // last one: they show, and can be moved or deleted).
    const std::optional<size_t> space =
        dict ? StoredSpaceIndex(space_ids, dict->FindString("space_id"),
                                dict->FindInt("space"))
             : std::nullopt;
    if (!space || !token || token->empty() ||
        std::ranges::contains(folders_, *token, &KoanFolder::token)) {
      continue;
    }
    KoanFolder folder;
    folder.id = next_id_++;
    folder.token = *token;
    if (const std::string* name = dict->FindString("name")) {
      folder.name = CapName(base::UTF8ToUTF16(*name));
    }
    folder.collapsed = dict->FindBool("collapsed").value_or(false);
    folder.space = spaces[*space];
    if (const std::string* parent = dict->FindString("parent")) {
      const auto it = std::ranges::find(folders_, *parent, &KoanFolder::token);
      if (it != folders_.end()) {
        folder.parent = it->id;
        folder.space = it->space;
      }
    }
    tab_lists.emplace_back(*token, dict->FindList("tabs"));
    folders_.push_back(std::move(folder));
    had_folders_ = true;
  }
  // Their tabs, unloaded (restored tabs load when shown) and pinned to the
  // Space, with their pinned URL, label and icon.
  for (const auto& [token, tab_list] : tab_lists) {
    const auto folder = std::ranges::find(folders_, token, &KoanFolder::token);
    if (!tab_list || folder == folders_.end() ||
        !model->group_model()->ContainsTabGroup(*folder->space)) {
      continue;
    }
    const tab_groups::TabGroupId space = *folder->space;
    for (const base::Value& value : *tab_list) {
      const base::DictValue* dict = value.GetIfDict();
      const std::string* url = dict ? dict->FindString("url") : nullptr;
      if (!url || !GURL(*url).is_valid()) {
        continue;
      }
      sessions::SerializedNavigationEntry entry;
      entry.set_index(0);
      entry.set_virtual_url(GURL(*url));
      if (const std::string* title = dict->FindString("title")) {
        entry.set_title(base::UTF8ToUTF16(*title));
      }
      entry.set_timestamp(base::Time::Now());
      const std::string* pinned = dict->FindString("pinned");
      KoanTabState state =
          pinned ? KoanTabState::Deserialize(*pinned) : KoanTabState();
      if (!state.pinned_url.is_valid()) {
        state.pinned_url = GURL(*url);
      }
      state.pinned_in_space = true;
      const int index = static_cast<int>(
          model->group_model()->GetTabGroup(space)->ListTabs().end());
      chrome::AddRestoredTab(
          browser_view_->browser(), base::span_from_ref(entry), index,
          /*selected_navigation=*/0, /*extension_app_id=*/std::string(), space,
          /*select=*/false, /*pin=*/false, base::TimeTicks(), base::Time(),
          /*storage_namespace=*/nullptr,
          sessions::SerializedUserAgentOverride(),
          {{kFolderSessionKey, token},
           {kPinnedTabSessionKey, state.Serialize()}},
          /*from_session_restore=*/true, /*is_active_browser=*/std::nullopt);
    }
  }
  arrange_ = true;
  Changed();
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
  // The containers folders live in: each Space's group view.
  std::vector<std::pair<views::View*, std::optional<tab_groups::TabGroupId>>>
      containers;
  const auto find = [&](const auto& self, views::View* view) -> void {
    if (auto* group = views::AsViewClass<TabGroupView>(view)) {
      if (group->IsValid()) {
        containers.emplace_back(group, group->GetTabGroup().id());
      }
      return;
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
        // Its search popup goes with it (it points at the row).
        if (search_ && search_folder_ == row->folder_id()) {
          search_.reset();
        }
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
  BrowserWindowInterface* browser = browser_view_->browser();
  bool changed = false;
  if (std::string data = Serialize(); data != saved_) {
    SetWindowSessionData(browser, kFolderSessionKey, data);
    saved_ = std::move(data);
    changed = true;
  }
  if (stores_folders_) {
    StoreFolders();
  }
  // Each tab names its folder, if it's in one.
  for (tabs::TabInterface* tab : *tab_strip_model()) {
    const KoanFolder* folder = GetFolder(GetFolderOf(tab));
    const std::string token = folder ? folder->token : std::string();
    if (GetTabSessionData(tab->GetContents(), kFolderSessionKey) != token) {
      SetTabSessionData(tab->GetContents(), kFolderSessionKey, token);
      changed = true;
    }
  }
  if (changed) {
    folders_changed_.Notify();
  }
}

int KoanFolderController::FindFolderByToken(const std::string& token) const {
  const auto it = std::ranges::find(folders_, token, &KoanFolder::token);
  return token.empty() || it == folders_.end() ? 0 : it->id;
}

int KoanFolderController::AddSyncedFolder(std::string token,
                                          std::u16string name,
                                          bool collapsed,
                                          int parent,
                                          const tab_groups::TabGroupId& space) {
  if (token.empty() || FindFolderByToken(token)) {
    return FindFolderByToken(token);
  }
  KoanFolder folder;
  folder.id = next_id_++;
  folder.token = std::move(token);
  folder.name = CapName(std::move(name));
  folder.collapsed = collapsed;
  folder.parent = GetFolder(parent) ? parent : 0;
  folder.space = space;
  const int id = folder.id;
  folders_.push_back(std::move(folder));
  had_folders_ = true;
  arrange_ = true;
  Changed();
  return id;
}

void KoanFolderController::UpdateSyncedFolder(
    int id,
    std::u16string name,
    bool collapsed,
    int parent,
    const tab_groups::TabGroupId& space) {
  KoanFolder* folder = GetMutableFolder(id);
  if (!folder) {
    return;
  }
  // Never into itself (the other window's tree has no cycle, but this one
  // may still be on its way there).
  for (const KoanFolder* up = GetFolder(parent); up;
       up = GetFolder(up->parent)) {
    if (up->id == id) {
      parent = folder->parent;
      break;
    }
  }
  folder->name = CapName(std::move(name));
  folder->collapsed = collapsed;
  folder->parent = GetFolder(parent) ? parent : 0;
  if (folder->space != space) {
    SetSpace(id, space);
  }
  arrange_ = true;
  Changed();
}

void KoanFolderController::OrderSyncedFolders(const std::vector<int>& ids) {
  std::vector<int> wanted;
  for (int id : ids) {
    if (GetFolder(id) && !std::ranges::contains(wanted, id)) {
      wanted.push_back(id);
    }
  }
  // The places the folders take, in their current order...
  std::vector<size_t> slots;
  for (size_t i = 0; i < folders_.size(); ++i) {
    if (std::ranges::contains(wanted, folders_[i].id)) {
      slots.push_back(i);
    }
  }
  // ...get them in the wanted order.
  std::vector<KoanFolder> moved;
  for (int id : wanted) {
    moved.push_back(
        std::move(*std::ranges::find(folders_, id, &KoanFolder::id)));
  }
  for (size_t i = 0; i < slots.size(); ++i) {
    folders_[slots[i]] = std::move(moved[i]);
  }
  arrange_ = true;
  Changed();
}

void KoanFolderController::SetSyncedFolderOf(tabs::TabInterface* tab, int id) {
  if (GetFolderOf(tab) == id) {
    return;
  }
  SetFolder({tab}, GetFolder(id) ? id : 0);
  Changed();
}

base::CallbackListSubscription KoanFolderController::AddFoldersChangedCallback(
    base::RepeatingClosure callback) {
  return folders_changed_.Add(std::move(callback));
}

std::string KoanFolderController::Serialize() const {
  if (folders_.empty()) {
    return std::string();
  }
  // A Space by its place among the window's Spaces: group ids change on
  // restore. Only folders without tabs need it; the others go where their
  // tabs come back.
  const std::vector<tab_groups::TabGroupId> spaces =
      SpacesInOrder(tab_strip_model());
  base::ListValue list;
  for (const KoanFolder& folder : folders_) {
    base::DictValue dict;
    dict.Set("token", folder.token);
    dict.Set("name", folder.name);
    dict.Set("collapsed", folder.collapsed);
    if (const KoanFolder* parent = GetFolder(folder.parent)) {
      dict.Set("parent", parent->token);
    }
    const auto it =
        folder.space ? std::ranges::find(spaces, *folder.space) : spaces.end();
    dict.Set("space",
             it == spaces.end() ? -1 : static_cast<int>(it - spaces.begin()));
    list.Append(std::move(dict));
  }
  return base::WriteJson(list).value_or(std::string());
}

void KoanFolderController::Restore(const std::string& data) {
  restored_from_session_ = true;
  // The session's folders replace what a crash left in the list.
  carried_ = base::ListValue();
  std::optional<base::ListValue> list =
      base::JSONReader::ReadList(data, base::JSON_PARSE_RFC);
  if (!list) {
    return;
  }
  if (!restoring_) {
    restoring_ = true;
    spaces_before_restore_ =
        static_cast<int>(SpacesInOrder(tab_strip_model()).size());
    // Session restore adds the window's tabs right after this, in the same
    // task.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanFolderController::FinishRestore,
                                  weak_factory_.GetWeakPtr()));
  }
  for (const base::Value& value : *list) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* token = dict ? dict->FindString("token") : nullptr;
    // Each folder once, even restored into a window that has it already.
    if (!token || token->empty() ||
        std::ranges::contains(folders_, *token, &KoanFolder::token)) {
      continue;
    }
    KoanFolder folder;
    folder.id = next_id_++;
    folder.token = *token;
    if (const std::string* name = dict->FindString("name")) {
      folder.name = CapName(base::UTF8ToUTF16(*name));
    }
    folder.collapsed = dict->FindBool("collapsed").value_or(false);
    // A parent is an earlier entry (Serialize() writes folders in the order
    // they were made); anything else is ignored.
    if (const std::string* parent = dict->FindString("parent")) {
      const auto it = std::ranges::find(folders_, *parent, &KoanFolder::token);
      folder.parent = it == folders_.end() ? 0 : it->id;
    }
    restored_spaces_[folder.id] = dict->FindInt("space").value_or(-1);
    folders_.push_back(std::move(folder));
    had_folders_ = true;
  }
}

// SpaceGroupRows --------------------------------------------------------------

// The folders' part: a row before each folder's tabs.
class SpaceGroupRows::FolderRows {
 public:
  FolderRows(const views::View& tab_group_view, int tab_padding)
      : controller_(KoanFolderController::FromView(&tab_group_view)),
        tab_padding_(tab_padding),
        narrow_now_(tab_group_view.width() < kMinExpandedRowWidth) {
    const auto* group = views::AsViewClass<TabGroupView>(&tab_group_view);
    if (!controller_ || !group || !group->IsValid()) {
      controller_ = nullptr;
      return;
    }
    space_ = group->GetTabGroup().id();
    entries_ = controller_->GetFolders(space_);
    if (entries_.empty()) {
      controller_ = nullptr;
      return;
    }
    // A collapsed group hides its folders too.
    all_hidden_ = group->IsCollapsed();
    for (const Entry& entry : entries_) {
      for (tabs::TabInterface* tab : controller_->GetTabs(entry.id, false)) {
        folder_of_[tab] = entry.id;
      }
    }
    for (views::View* child : tab_group_view.children()) {
      if (auto* row = views::AsViewClass<KoanFolderRowView>(child);
          row && !row->removing()) {
        rows_[row->folder_id()] = row;
      }
    }
  }

  Slot Place(const views::View& child,
             int x,
             const views::SizeBounds& size_bounds,
             int& y,
             views::ProposedLayout& layout) {
    if (!controller_) {
      return {};
    }
    const tabs::TabInterface* tab = GetFirstTab(child);
    const auto found = folder_of_.find(tab);
    const int folder = found == folder_of_.end() ? 0 : found->second;
    const auto it = std::ranges::find(entries_, folder, &Entry::id);
    if (it == entries_.end()) {
      // The Space's other pinned tabs at its top come before its folders;
      // the rest (normal tabs, a pinned tab put below the folders) after
      // them.
      if (!IsPinnedForUser(tab) || next_ > 0) {
        AddRows(entries_.size(), x, size_bounds, y, layout);
      }
      return {};
    }
    AddRows(static_cast<size_t>(it - entries_.begin()) + 1, x, size_bounds, y,
            layout);
    Slot slot;
    int depth = it->depth;
    // A collapsed folder still shows its active tab, at the level of the
    // outermost collapsed folder (Zen's has-active).
    if (const int root = controller_->GetRootMostCollapsed(folder)) {
      slot.hidden = !ShowsActiveTab(child, controller_->tab_strip_model());
      depth = controller_->GetDepth(root);
    }
    slot.indent = IsNarrow(size_bounds) ? 0 : kFolderIndent * (depth + 1);
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

  bool IsVisible(const Slot& slot, bool upstream_visible) const {
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
      const int left =
          x + (IsNarrow(size_bounds) ? 0 : kFolderIndent * entry.depth);
      const gfx::Size size = row->second->GetPreferredSize({});
      const int right = size_bounds.width().is_bounded()
                            ? size_bounds.width().value()
                            : left + size.width();
      layout.child_layouts.emplace_back(
          row->second.get(),
          !hidden || (!all_hidden_ && controller_->animating()),
          gfx::Rect(left, y, std::max(0, right - left),
                    hidden ? 0 : size.height()));
      if (!hidden) {
        y += size.height() + tab_padding_;
      }
    }
  }

  // Icons only (no indent) when too narrow for a row's title: at the width
  // being laid out, else the one it has.
  bool IsNarrow(const views::SizeBounds& size_bounds) const {
    return size_bounds.width().is_bounded()
               ? size_bounds.width().value() < kMinExpandedRowWidth
               : narrow_now_;
  }

  raw_ptr<KoanFolderController> controller_;
  const int tab_padding_;
  const bool narrow_now_;
  std::optional<tab_groups::TabGroupId> space_;
  bool all_hidden_ = false;
  std::vector<Entry> entries_;
  std::map<const tabs::TabInterface*, int> folder_of_;
  size_t next_ = 0;
  std::map<int, raw_ptr<KoanFolderRowView>> rows_;
};

SpaceGroupRows::SpaceGroupRows(const views::View& tab_group_view,
                               bool collapsed,
                               int tab_padding)
    : tab_group_view_(tab_group_view),
      collapsed_(collapsed),
      tab_padding_(tab_padding),
      folders_(std::make_unique<FolderRows>(tab_group_view, tab_padding)) {}

SpaceGroupRows::~SpaceGroupRows() = default;

SpaceGroupRows::Slot SpaceGroupRows::Place(const views::View& child,
                                           int x,
                                           const views::SizeBounds& size_bounds,
                                           int& y,
                                           views::ProposedLayout& layout) {
  x_ = x;
  const Slot slot = folders_->Place(child, x, size_bounds, y, layout);
  // Then the separator and New Tab rows, before the first normal tab.
  y += PlaceSpaceTabRows(&*tab_group_view_, &child, collapsed_, y, size_bounds,
                         tab_padding_, layout);
  return slot;
}

void SpaceGroupRows::Finish(const views::SizeBounds& size_bounds,
                            int& y,
                            views::ProposedLayout& layout) {
  folders_->Finish(x_, size_bounds, y, layout);
  y += PlaceSpaceTabRows(&*tab_group_view_, nullptr, collapsed_, y, size_bounds,
                         tab_padding_, layout);
}

bool SpaceGroupRows::IsVisible(const Slot& slot, bool upstream_visible) const {
  return folders_->IsVisible(slot, upstream_visible);
}

bool IsFolderRowView(const views::View* view) {
  return views::IsViewClass<KoanFolderRowView>(view);
}

// Hooks -----------------------------------------------------------------------

namespace {

// The folders of the window the tab at `index` is in, if they apply.
KoanFolderController* ControllerForTab(const TabStripModel* tab_strip,
                                       int index) {
  return tab_strip->ContainsIndex(index)
             ? KoanFolderController::FromBrowser(
                   tab_strip->GetTabAtIndex(index)->GetBrowserWindowInterface())
             : nullptr;
}

}  // namespace

void AddFolderTabMenuItems(TabMenuContext& context) {
  if (KoanFolderController* controller =
          ControllerForTab(&*context.tab_strip, context.index)) {
    controller->AddTabMenuItems(context);
  }
}

bool IsFolderTabMenuCommandEnabled(const TabStripModel* tab_strip,
                                   int index,
                                   int command_id) {
  return ControllerForTab(tab_strip, index) != nullptr;
}

bool ExecuteFolderTabMenuCommand(TabStripModel* tab_strip,
                                 int index,
                                 int command_id) {
  if (command_id < kTabMoveToFolder || command_id >= kTabCommandsEnd) {
    return false;
  }
  if (KoanFolderController* controller = ControllerForTab(tab_strip, index)) {
    controller->ExecuteTabMenuCommand(command_id);
  }
  return true;
}

void OnRestoringFolders(BrowserWindowInterface* browser,
                        const std::string& data,
                        bool from_session_restore) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (controller && controller->folders()->IsEnabled()) {
    controller->folders()->Restore(data);
  }
}

}  // namespace koan
