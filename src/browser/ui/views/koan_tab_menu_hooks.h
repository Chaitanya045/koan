// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_HOOKS_H_

// Hooks for the tab context menu, called from upstream (see patches/). The
// definitions live in //koan/browser/ui/views:impl.

class TabStripModel;

namespace ui {
class SimpleMenuModel;
}  // namespace ui

namespace koan {

// Adds Zen's items to the tab context menu for the tab at `index` (`num_tabs`
// tabs selected), each finding upstream's items by command id: the split
// view items (koan_split_view.h) and, with Spaces, "Move To" in place of the
// tab group items (koan_spaces.h). Called at the end of TabMenuModel::Build().
void AddKoanTabMenuItems(ui::SimpleMenuModel* menu,
                         TabStripModel* tab_strip,
                         int index,
                         int num_tabs);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_MENU_HOOKS_H_
