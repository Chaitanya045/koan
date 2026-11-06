// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_glance_controller.h"

#import <Cocoa/Cocoa.h>

#include "base/apple/foundation_util.h"
#include "base/apple/owned_objc.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "components/input/native_web_keyboard_event.h"
#import "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

// The card window's CommandDispatcherDelegate (CardKeyDispatcher).
@interface KoanGlanceKeyDispatcher : NSObject <CommandDispatcherDelegate>
- (instancetype)initWithHandler:(koan::CardKeyDispatcher::Handler)handler;
@end

@implementation KoanGlanceKeyDispatcher {
  koan::CardKeyDispatcher::Handler _handler;
}

- (instancetype)initWithHandler:(koan::CardKeyDispatcher::Handler)handler {
  if ((self = [super init])) {
    _handler = std::move(handler);
  }
  return self;
}

- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  // As DefaultCommandDispatcherDelegate, which this replaces: a page holding
  // the keyboard (Keyboard Lock) gets its keys.
  NSResponder* responder = window.firstResponder;
  if ([responder respondsToSelector:@selector(isKeyLocked:)] &&
      [(id)responder isKeyLocked:event]) {
    return ui::PerformKeyEquivalentResult::kUnhandled;
  }
  // The card's steps, with the browser window's FocusManager. (The parent's
  // ChromeCommandDispatcherDelegate looks for accelerators on the FocusManager
  // of the event's window, the card's, which has none of Koan's.)
  if (event.type == NSEventTypeKeyDown) {
    switch (_handler.Run(
        input::NativeWebKeyboardEvent(base::apple::OwnedNSEvent(event)))) {
      case koan::CardKeyResult::kHandled:
        return ui::PerformKeyEquivalentResult::kHandled;
      case koan::CardKeyResult::kDrop:
        return ui::PerformKeyEquivalentResult::kDrop;
      case koan::CardKeyResult::kUnhandled:
        break;
    }
  }
  // Otherwise as DefaultCommandDispatcherDelegate: the parent window's
  // delegate (extension commands and the rest of the browser's steps).
  if (![window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    return ui::PerformKeyEquivalentResult::kUnhandled;
  }
  NSWindow<CommandDispatchingWindow>* parent =
      static_cast<NSWindow<CommandDispatchingWindow>*>(window)
          .commandDispatchParent;
  return [[parent commandDispatcher].delegate prePerformKeyEquivalent:event
                                                               window:parent];
}

- (ui::PerformKeyEquivalentResult)postPerformKeyEquivalent:(NSEvent*)event
                                                    window:(NSWindow*)window
                                              isRedispatch:(BOOL)isRedispatch {
  // What the page and the steps above left goes on to the browser window
  // (commandDispatchParent) and the menu bar, as before.
  return ui::PerformKeyEquivalentResult::kUnhandled;
}

@end

namespace koan {

struct CardKeyDispatcher::ObjC {
  KoanGlanceKeyDispatcher* __strong delegate;
  NativeWidgetMacNSWindow* __weak window;
};

CardKeyDispatcher::CardKeyDispatcher(views::Widget* card, Handler handler)
    : objc_(std::make_unique<ObjC>()) {
  // The card's window is a NativeWidgetMacNSWindow (in-process): its
  // CommandDispatcher holds its delegate weakly, so this keeps it.
  NativeWidgetMacNSWindow* window =
      base::apple::ObjCCast<NativeWidgetMacNSWindow>(
          card->GetNativeWindow().GetNativeNSWindow());
  if (!window) {
    return;
  }
  objc_->delegate =
      [[KoanGlanceKeyDispatcher alloc] initWithHandler:std::move(handler)];
  objc_->window = window;
  [window setCommandDispatcherDelegate:objc_->delegate];
}

CardKeyDispatcher::~CardKeyDispatcher() {
  if (NativeWidgetMacNSWindow* window = objc_->window) {
    [window setCommandDispatcherDelegate:nil];
  }
}


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

// The Window menu's item with `action` (Minimize and Zoom live there).
NSMenuItem* WindowMenuItemWithAction(SEL action) {
  for (NSMenuItem* item in NSApp.windowsMenu.itemArray) {
    if (item.action == action) {
      return item;
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
  // The item as the browser window sees it: matched by its key equivalent
  // (it validates against the card's window, and is disabled, while that is
  // key), and enabled for the browser window (not in native fullscreen, for
  // instance, where Minimize is off).
  for (SEL action : {@selector(performMiniaturize:), @selector(performZoom:)}) {
    NSMenuItem* item = WindowMenuItemWithAction(action);
    if (!IsKeyEquivalentOf(item, ns_event) ||
        ![window validateUserInterfaceItem:item]) {
      continue;
    }
    if (action == @selector(performMiniaturize:)) {
      [window performMiniaturize:nil];
    } else {
      [window performZoom:nil];
    }
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
