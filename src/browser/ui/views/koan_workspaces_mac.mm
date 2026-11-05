// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_workspaces.h"

#import <AppKit/AppKit.h>

#include "chrome/app/chrome_command_ids.h"

namespace koan {

namespace {

NSMenuItem* FindItemWithTag(NSMenu* menu, NSInteger tag) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.tag == tag) {
      return item;
    }
    if (item.hasSubmenu) {
      if (NSMenuItem* found = FindItemWithTag(item.submenu, tag)) {
        return found;
      }
    }
  }
  return nil;
}

}  // namespace

void SetTabGroupMenuAlternate(bool alternate) {
  // Tab > Group Selected Tab, the Option alternate of Group Tab
  // (main_menu_builder.mm). AppKit shows an alternate in its primary's place
  // while Option is held even when the primary item is hidden, and ignores
  // the alternate's own hidden state, so in a Spaces window it stops being an
  // alternate: then the menu validation hides it like any item
  // (HidesMenuCommand()). Other windows get it back as Group Tab's alternate.
  NSMenuItem* item = FindItemWithTag(NSApp.mainMenu, IDC_GROUP_TARGET_TAB);
  if (item && item.alternate != alternate) {
    item.alternate = alternate;
  }
}

}  // namespace koan
