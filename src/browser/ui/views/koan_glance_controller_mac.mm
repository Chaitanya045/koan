// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_glance_controller.h"

#import <Cocoa/Cocoa.h>

#include "base/strings/sys_string_conversions.h"
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
  // Like the delegate: a repeating ⌘T / ⌘N opens nothing more.
  if (ns_event.isARepeat && (result.chrome_command == IDC_NEW_TAB ||
                             result.chrome_command == IDC_NEW_WINDOW ||
                             result.chrome_command ==
                                 IDC_NEW_INCOGNITO_WINDOW)) {
    return 0;
  }
  chrome::BrowserCommandController* commands =
      chrome::BrowserCommandController::From(browser);
  return commands &&
                 commands->IsReservedCommandOrKey(result.chrome_command, event)
             ? result.chrome_command
             : 0;
}

std::string DescribeKeyWindow(views::Widget* card, views::Widget* browser) {
  NSWindow* key = NSApp.keyWindow;
  NSWindow* card_window =
      card ? card->GetNativeWindow().GetNativeNSWindow() : nil;
  NSWindow* browser_window =
      browser ? browser->GetNativeWindow().GetNativeNSWindow() : nil;
  NSString* who = !key                     ? @"none"
                  : key == card_window     ? @"the card's"
                  : key == browser_window  ? @"the browser window"
                                           : @"another window";
  NSString* parent = !card_window.parentWindow ? @"none"
                     : card_window.parentWindow == browser_window
                         ? @"the browser window"
                         : @"another window";
  NSString* text = [NSString
      stringWithFormat:
          @"key window: %@ (%@ \"%@\"), its first responder %@; card window "
          @"%@: canBecomeKey %d, isKey %d, visible %d, parent %@; card widget "
          @"active %d, can activate %d; app active %d",
          who, key.className, key.title, key.firstResponder.className,
          card_window ? card_window.className : @"none",
          card_window.canBecomeKeyWindow, card_window.isKeyWindow,
          card_window.isVisible, parent, card ? card->IsActive() : 0,
          card ? card->CanActivate() : 0, NSApp.isActive];
  return base::SysNSStringToUTF8(text);
}

void MakeCardWindowKey(views::Widget* card) {
  NSWindow* window = card ? card->GetNativeWindow().GetNativeNSWindow() : nil;
  [window makeKeyWindow];
}

}  // namespace koan
