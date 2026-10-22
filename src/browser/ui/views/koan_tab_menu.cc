// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_menu.h"

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "koan/browser/ui/views/koan_folders.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_split_view.h"

namespace koan {

namespace {

// Zen's split view items (koan_split_view.h).
void AddSplitViewItems(TabMenuContext& context) {
  AddZenSplitTabMenuItems(&*context.menu, &*context.tab_strip, context.index,
                          context.num_tabs);
}

bool IsSplitViewCommandEnabled(const TabStripModel* tab_strip,
                               int index,
                               int command_id) {
  return IsSplitOutTabCommand(command_id) && CanSplitOutTab(tab_strip, index);
}

bool ExecuteSplitViewCommand(TabStripModel* tab_strip,
                             int index,
                             int command_id) {
  if (!IsSplitOutTabCommand(command_id)) {
    return false;
  }
  SplitOutTab(tab_strip, index);
  return true;
}

// Every feature's part of the menu, in the order they add their items.
constexpr TabMenuFeature kFeatures[] = {
    {TabMenuFeatureId::kSplitView, &AddSplitViewItems,
     &IsSplitViewCommandEnabled, &ExecuteSplitViewCommand},
    {TabMenuFeatureId::kPinnedTabs, &AddPinnedTabMenuItems,
     &IsPinnedTabMenuCommandEnabled, &ExecutePinnedTabMenuCommand},
    {TabMenuFeatureId::kFolders, &AddFolderTabMenuItems,
     &IsFolderTabMenuCommandEnabled, &ExecuteFolderTabMenuCommand},
};

// The feature whose range `command_id` is in, or null.
const TabMenuFeature* FeatureOf(int command_id) {
  for (const TabMenuFeature& feature : kFeatures) {
    const int first = FirstTabMenuCommand(feature.id);
    if (command_id >= first &&
        command_id < first + kTabMenuCommandsPerFeature) {
      return &feature;
    }
  }
  return nullptr;
}

}  // namespace

TabMenuContext::TabMenuContext(
    ui::SimpleMenuModel& menu,
    ui::SimpleMenuModel::Delegate* delegate,
    TabStripModel& tab_strip,
    int index,
    int num_tabs,
    std::vector<std::unique_ptr<ui::SimpleMenuModel>>& submenus)
    : menu(menu),
      delegate(delegate),
      tab_strip(tab_strip),
      index(index),
      num_tabs(num_tabs),
      submenus_(submenus) {}

TabMenuContext::~TabMenuContext() = default;

ui::SimpleMenuModel* TabMenuContext::AddSubmenu() {
  submenus_->push_back(std::make_unique<ui::SimpleMenuModel>(delegate.get()));
  return submenus_->back().get();
}

void AddKoanTabMenuItems(
    ui::SimpleMenuModel* menu,
    ui::SimpleMenuModel::Delegate* delegate,
    TabStripModel* tab_strip,
    int index,
    int num_tabs,
    std::vector<std::unique_ptr<ui::SimpleMenuModel>>& submenus) {
  TabMenuContext context(*menu, delegate, *tab_strip, index, num_tabs,
                         submenus);
  for (const TabMenuFeature& feature : kFeatures) {
    feature.add_items(context);
  }
}

bool IsKoanTabMenuCommand(int command_id) {
  return command_id >= kTabMenuCommandsFirst &&
         command_id <= kTabMenuCommandsLast;
}

bool IsKoanTabMenuCommandEnabled(const TabStripModel* tab_strip,
                                 int index,
                                 int command_id) {
  const TabMenuFeature* feature = FeatureOf(command_id);
  return feature && feature->is_enabled(tab_strip, index, command_id);
}

bool ExecuteKoanTabMenuCommand(TabStripModel* tab_strip,
                               int index,
                               int command_id) {
  for (const TabMenuFeature& feature : kFeatures) {
    if (feature.execute(tab_strip, index, command_id)) {
      return true;
    }
  }
  return false;
}

}  // namespace koan
