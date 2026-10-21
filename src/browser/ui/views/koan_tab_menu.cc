// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_split_view.h"
#include "koan/browser/ui/views/koan_tab_menu_hooks.h"

namespace koan {

void AddKoanTabMenuItems(ui::SimpleMenuModel* menu,
                         TabStripModel* tab_strip,
                         int index,
                         int num_tabs) {
  AddZenSplitTabMenuItems(menu, tab_strip, index, num_tabs);
  ReplaceTabGroupMenuItems(menu, tab_strip, index);
}

}  // namespace koan
