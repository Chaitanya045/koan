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

}  // namespace koan
