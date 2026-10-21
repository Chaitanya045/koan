// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_H_

#include <memory>

class TabStripModel;

namespace ui {
class SimpleMenuModel;
}

namespace koan {

// Hook for TabMenuModel::Build(), once upstream's items are in: Koan's
// changes to the tab context menu for the tab at `index` (`num_tabs` tabs
// selected for the command), each finding upstream's items by command id:
// Zen's split view items (koan_split_view.h) and, with Spaces, "Move To" in
// place of the tab group items (koan_spaces.h). Returns a submenu the caller
// keeps alive while the menu is open, or null.
std::unique_ptr<ui::SimpleMenuModel> AddZenTabMenuItems(
    ui::SimpleMenuModel* menu,
    TabStripModel* tab_strip,
    int index,
    int num_tabs);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_H_
