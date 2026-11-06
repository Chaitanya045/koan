// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_glance_controller.h"

#import <Cocoa/Cocoa.h>

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "components/input/native_web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace koan {

int ReservedCommandForKeyEvent(BrowserWindowInterface* browser,
                               const input::NativeWebKeyboardEvent& event) {
  // As ChromeCommandDispatcherDelegate's prePerformKeyEquivalent: the key
  // equivalent's command (the menu bar's, or Chrome's fixed ones), if the
  // browser reserves it.
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown) {
    return 0;
  }
  NSEvent* ns_event = event.os_event.Get();
  if (!ns_event || ns_event.type != NSEventTypeKeyDown) {
    return 0;
  }
  CommandForKeyEventResult result = CommandForKeyEvent(ns_event);
  if (!result.found()) {
    return 0;
  }
  // Like the delegate: a repeating ⌘T / ⌘N opens nothing more (it drops
  // the key; passed on, it would reach the menu bar's item again).
  if (ns_event.isARepeat && (result.chrome_command == IDC_NEW_TAB ||
                             result.chrome_command == IDC_NEW_WINDOW ||
                             result.chrome_command ==
                                 IDC_NEW_INCOGNITO_WINDOW)) {
    return kDropKeyEvent;
  }
  chrome::BrowserCommandController* commands =
      chrome::BrowserCommandController::From(browser);
  return commands &&
                 commands->IsReservedCommandOrKey(result.chrome_command, event)
             ? result.chrome_command
             : 0;
}

namespace {

// The menu bar's item with `action`, searched depth-first.
NSMenuItem* MenuItemWithAction(NSMenu* menu, SEL action) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.action == action) {
      return item;
    }
    if (item.hasSubmenu) {
      if (NSMenuItem* found = MenuItemWithAction(item.submenu, action)) {
        return found;
      }
    }
  }
  return nil;
}

// Whether `event` is `item`'s key equivalent, whether or not the item is
// enabled: while the card's window is key, Window > Minimize and Zoom
// validate against it (borderless, not miniaturizable) and are disabled, so
// cr_firesForKeyEquivalentEvent: (which needs an enabled item) never fires.
bool IsKeyEquivalentOf(NSMenuItem* item, NSEvent* event) {
  NSString* key = item.keyEquivalent;
  if (key.length == 0) {
    return false;
  }
  constexpr NSEventModifierFlags kRelevant =
      NSEventModifierFlagCommand | NSEventModifierFlagOption |
      NSEventModifierFlagControl | NSEventModifierFlagShift;
  NSEventModifierFlags mask = item.keyEquivalentModifierMask;
  // An upper-case key equivalent means Shift.
  if (![key isEqualToString:key.lowercaseString]) {
    mask |= NSEventModifierFlagShift;
  }
  return (event.modifierFlags & kRelevant) == (mask & kRelevant) &&
         [key.lowercaseString
             isEqualToString:event.charactersIgnoringModifiers.lowercaseString];
}

}  // namespace

bool PerformWindowMenuKey(views::Widget* browser,
                          const input::NativeWebKeyboardEvent& event) {
  NSEvent* ns_event = event.os_event.Get();
  NSWindow* window =
      browser ? browser->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!ns_event || ns_event.type != NSEventTypeKeyDown || !window) {
    return false;
  }
  NSMenu* main_menu = [NSApp mainMenu];
  if (IsKeyEquivalentOf(
          MenuItemWithAction(main_menu, @selector(performMiniaturize:)),
          ns_event)) {
    [window performMiniaturize:nil];
    return true;
  }
  if (IsKeyEquivalentOf(MenuItemWithAction(main_menu, @selector(performZoom:)),
                        ns_event)) {
    [window performZoom:nil];
    return true;
  }
  return false;
}

void PerformMainMenuKey(const input::NativeWebKeyboardEvent& event) {
  NSEvent* ns_event = event.os_event.Get();
  if (ns_event && ns_event.type == NSEventTypeKeyDown) {
    [[NSApp mainMenu] performKeyEquivalent:ns_event];
  }
}

void PrepareCardWindow(views::Widget* card) {
  NSWindow* window = card->GetNativeWindow().GetNativeNSWindow();
  // VoiceOver names the card's window (the page's dialog is inside it).
  window.title = @"Glance";
  window.excludedFromWindowsMenu = YES;
}

}  // namespace koan
