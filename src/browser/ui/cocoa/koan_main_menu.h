// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COCOA_KOAN_MAIN_MENU_H_
#define KOAN_BROWSER_UI_COCOA_KOAN_MAIN_MENU_H_

#include <vector>

#include "ui/base/accelerators/accelerator.h"

#if defined(__OBJC__)
@class NSMenu;
#endif

// Koan's macOS menus: the menu bar's and the Dock's. Declarations for the
// hooks in chrome/browser/ui/cocoa/main_menu_builder.mm and
// chrome/browser/app_controller_mac.mm; the definitions are in
// koan_main_menu_mac.mm (//koan/browser/ui/cocoa:impl).
namespace koan {

#if defined(__OBJC__)
// Called by BuildMainMenu() on the finished menu bar: View gets compact
// mode's items, a Spaces menu follows it (like Zen's), the Tab menu gets
// split view and the pinned tabs actions, and Chrome's own items that share a
// key with a Koan shortcut give it up (or take Koan's key for the same
// action, e.g. Save Page). The menus use the shortcuts of the window that is
// active when they open.
void ExtendMainMenu(NSMenu* main_menu, bool is_pwa);

// Called by -[AppController applicationDockMenu:] on its finished menu: New
// Private Window instead of Incognito, and the active window's Spaces.
void ExtendDockMenu(NSMenu* dock_menu);
#endif  // __OBJC__

// Every shortcut Chrome has on macOS: those of its menus and those it keeps
// in a table outside them. For the settings page to say that a key replaces
// one.
std::vector<ui::Accelerator> ChromiumMacAccelerators();

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COCOA_KOAN_MAIN_MENU_H_
