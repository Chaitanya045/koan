// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "ui/menus/simple_menu_model.h"

class TabStripModel;

// Koan's part of the tab context menu, for every feature that adds to it.
//
// Upstream calls the hooks at the end of this file (patches to
// tab_menu_model.cc and tab_strip_model.cc); they go through kFeatures in
// koan_tab_menu.cc, one TabMenuFeature per feature. To add a feature's items:
// give it a TabMenuFeatureId (its range of command ids), write its
// TabMenuFeature functions, and add it to kFeatures. Features find upstream's
// items by command id (SimpleMenuModel::GetIndexOfCommandId), never by
// position, so their order in kFeatures only matters for items they add at
// the same place.
namespace koan {

// Each feature's command ids: kTabMenuCommandsPerFeature of them from
// FirstTabMenuCommand(). Clear of upstream's tab menu commands
// (tabs::TabContextMenuCommand, 0-30), its submenus' ranges (1000-2100, see
// ExistingBaseSubMenuModel) and extension items (49000+). The views
// controller casts every id to tabs::TabContextMenuCommand on its way to
// TabStripModel; that is formally unspecified for these values but harmless
// (no -fstrict-enums).
enum class TabMenuFeatureId {
  kSplitView = 0,   // koan_split_view.h ("Split Out Tab" is 3001).
  kPinnedTabs = 1,  // koan_pinned_tabs.h.
  kSpaces = 2,
  kFolders = 3,
};
inline constexpr int kTabMenuCommandsFirst = 3000;
inline constexpr int kTabMenuCommandsPerFeature = 100;
inline constexpr int kTabMenuCommandsLast =
    kTabMenuCommandsFirst + 4 * kTabMenuCommandsPerFeature - 1;
constexpr int FirstTabMenuCommand(TabMenuFeatureId feature) {
  return kTabMenuCommandsFirst +
         static_cast<int>(feature) * kTabMenuCommandsPerFeature;
}

// The menu being built, for a feature's `add_items`.
struct TabMenuContext {
  TabMenuContext(ui::SimpleMenuModel& menu,
                 ui::SimpleMenuModel::Delegate* delegate,
                 TabStripModel& tab_strip,
                 int index,
                 int num_tabs,
                 std::vector<std::unique_ptr<ui::SimpleMenuModel>>& submenus);
  TabMenuContext(const TabMenuContext&) = delete;
  TabMenuContext& operator=(const TabMenuContext&) = delete;
  ~TabMenuContext();

  // A new submenu whose items run like the menu's own (through
  // TabStripModel and the feature's `execute`), owned by the menu.
  ui::SimpleMenuModel* AddSubmenu();

  const raw_ref<ui::SimpleMenuModel> menu;
  const raw_ptr<ui::SimpleMenuModel::Delegate> delegate;
  const raw_ref<TabStripModel> tab_strip;
  // The tab the menu is for, and how many tabs the commands apply to.
  const int index;
  const int num_tabs;

 private:
  const raw_ref<std::vector<std::unique_ptr<ui::SimpleMenuModel>>> submenus_;
};

// One feature's part of the tab context menu.
struct TabMenuFeature {
  TabMenuFeatureId id;
  // Adds the feature's items for `context.index`, or changes upstream's.
  void (*add_items)(TabMenuContext& context);
  // Whether the feature's command `command_id` (one of its range) applies
  // to the tab at `index`.
  bool (*is_enabled)(const TabStripModel* tab_strip, int index, int command_id);
  // Runs `command_id` for the tab at `index` and returns true, or returns
  // false. Called with every command, upstream's too, so a feature may run
  // one of upstream's its own way (pinned tabs' Pin / Unpin).
  bool (*execute)(TabStripModel* tab_strip, int index, int command_id);
};

// --- Hooks, called from upstream ---

// At the end of TabMenuModel::Build(), once upstream's items are in, for the
// tab at `index` (`num_tabs` tabs selected for the command). `delegate` is
// the menu's own; `submenus` lives as long as the menu (a TabMenuModel
// member).
void AddKoanTabMenuItems(
    ui::SimpleMenuModel* menu,
    ui::SimpleMenuModel::Delegate* delegate,
    TabStripModel* tab_strip,
    int index,
    int num_tabs,
    std::vector<std::unique_ptr<ui::SimpleMenuModel>>& submenus);

// First thing in TabStripModel's IsContextMenuCommandEnabled(): whether
// `command_id` is one of Koan's, and then whether it applies.
bool IsKoanTabMenuCommand(int command_id);
bool IsKoanTabMenuCommandEnabled(const TabStripModel* tab_strip,
                                 int index,
                                 int command_id);

// First thing in TabStripModel's ExecuteContextMenuCommand(), for every
// command: returns true if a Koan feature ran it.
bool ExecuteKoanTabMenuCommand(TabStripModel* tab_strip,
                               int index,
                               int command_id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_H_
