// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/menus/simple_menu_model.h"

namespace koan {

namespace {

// Zen's "Move To" submenu: the other Spaces, each "<icon>  <name>".
class KoanMoveToSpaceMenuModel : public ui::SimpleMenuModel,
                                 public ui::SimpleMenuModel::Delegate {
 public:
  KoanMoveToSpaceMenuModel(KoanWorkspaceController* spaces,
                           TabStripModel* tab_strip,
                           int index)
      : ui::SimpleMenuModel(this), spaces_(spaces) {
    // The selected tabs if the menu is for one of them, like Zen.
    if (tab_strip->IsTabSelected(index)) {
      const ui::ListSelectionModel& selection =
          tab_strip->selection_model().GetListSelectionModel();
      for (size_t selected : selection.selected_indices()) {
        tabs_.push_back(
            tab_strip->GetTabAtIndex(static_cast<int>(selected))->GetHandle());
      }
    } else {
      tabs_.push_back(tab_strip->GetTabAtIndex(index)->GetHandle());
    }
    int command = 0;
    for (const KoanWorkspaceController::SpaceInfo& space :
         spaces_->GetSpaces()) {
      if (space.active) {
        continue;
      }
      AddItem(command, space.icon.empty()
                           ? space.name
                           : base::StrCat({space.icon, u"  ", space.name}));
      commands_.emplace(command++, space.id);
    }
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    auto it = commands_.find(command_id);
    if (it == commands_.end()) {
      return;
    }
    // After the menu has gone: showing the other Space takes down the tab
    // context menu, and this with it.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<KoanWorkspaceController> spaces,
                          const std::vector<tabs::TabHandle>& handles,
                          const tab_groups::TabGroupId& space) {
                         std::vector<tabs::TabInterface*> tabs;
                         for (const tabs::TabHandle& handle : handles) {
                           if (tabs::TabInterface* tab = handle.Get()) {
                             tabs.push_back(tab);
                           }
                         }
                         if (spaces) {
                           spaces->MoveTabsToSpace(tabs, space);
                         }
                       },
                       spaces_->GetWeakPtr(), tabs_, it->second));
  }

 private:
  const raw_ptr<KoanWorkspaceController> spaces_;
  std::vector<tabs::TabHandle> tabs_;
  std::map<int, tab_groups::TabGroupId> commands_;
};

}  // namespace

void ReplaceTabGroupMenuItems(ui::SimpleMenuModel* menu,
                              TabStripModel* tab_strip,
                              int index) {
  KoanWorkspaceController* spaces =
      KoanWorkspaceController::FromTabStrip(tab_strip);
  if (!spaces) {
    return;
  }
  std::optional<size_t> first;
  for (int command : {TabStripModel::CommandToggleFocusGroup,
                      TabStripModel::CommandAddToExistingGroup,
                      TabStripModel::CommandAddToNewGroup,
                      TabStripModel::CommandRemoveFromGroup}) {
    if (std::optional<size_t> at = menu->GetIndexOfCommandId(command)) {
      first = std::min(first.value_or(*at), *at);
      menu->RemoveItemAt(*at);
    }
  }
  // Pinned tabs (Essentials) belong to no Space.
  if (!first || !tab_strip->ContainsIndex(index) ||
      tab_strip->IsTabPinned(index)) {
    return;
  }
  auto move_to =
      std::make_unique<KoanMoveToSpaceMenuModel>(spaces, tab_strip, index);
  if (move_to->GetItemCount() == 0) {
    return;
  }
  menu->InsertSubMenuAt(*first, TabStripModel::CommandAddToExistingGroup,
                        u"Move To", move_to.get());
  spaces->KeepTabMenuSubmenu(std::move(move_to));
}

}  // namespace koan
