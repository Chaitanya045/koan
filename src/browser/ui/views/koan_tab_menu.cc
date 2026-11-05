// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_menu.h"

#include <optional>

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_vertical_tabs.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_spaces.h"
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

// Spaces: "Move To" a Space in place of the tab group items
// (koan_spaces.h). Its submenu runs its items through its own delegate, so
// Spaces have no commands of their own here.
void AddSpacesItems(TabMenuContext& context) {
  ReplaceTabGroupMenuItems(&*context.menu, &*context.tab_strip, context.index);
}

bool IsSpacesCommandEnabled(const TabStripModel* tab_strip,
                            int index,
                            int command_id) {
  return false;
}

bool ExecuteSpacesCommand(TabStripModel* tab_strip, int index, int command_id) {
  return false;
}

// Every feature's part of the menu, in the order they add their items.
constexpr TabMenuFeature kFeatures[] = {
    {TabMenuFeatureId::kSplitView, &AddSplitViewItems,
     &IsSplitViewCommandEnabled, &ExecuteSplitViewCommand},
    {TabMenuFeatureId::kPinnedTabs, &AddPinnedTabMenuItems,
     &IsPinnedTabMenuCommandEnabled, &ExecutePinnedTabMenuCommand},
    {TabMenuFeatureId::kSpaces, &AddSpacesItems, &IsSpacesCommandEnabled,
     &ExecuteSpacesCommand},
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
  // Zen has no horizontal tabs: no "Show tabs horizontally", and not the
  // separator that sat above it alone.
  if (OwnsSidebar()) {
    if (const std::optional<size_t> toggle =
            menu->GetIndexOfCommandId(TabStripModel::CommandToggleVertical)) {
      menu->RemoveItemAt(*toggle);
      const bool separator_before =
          *toggle > 0 &&
          menu->GetTypeAt(*toggle - 1) == ui::MenuModel::TYPE_SEPARATOR;
      const bool nothing_after =
          *toggle >= menu->GetItemCount() ||
          menu->GetTypeAt(*toggle) == ui::MenuModel::TYPE_SEPARATOR;
      if (separator_before && nothing_after) {
        menu->RemoveItemAt(*toggle - 1);
      }
    }
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
