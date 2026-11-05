// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   nsZenNativeWindowMove.mm
//   expose_tiled_attribute_to_all_platforms.patch (WindowIsTiled)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_zen_window_drag.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// The browser window's NSWindow, or null: a PWA's window lives in its own app
// shim process, and the NSWindow here is an invisible placeholder that nothing
// moves.
NSWindow* DraggableWindow(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  return window && window.isVisible ? window : nil;
}

NSEvent* MouseEvent(NSWindow* window, NSEventType type) {
  return [NSEvent
      mouseEventWithType:type
                location:[window convertPointFromScreen:NSEvent.mouseLocation]
           modifierFlags:0
               timestamp:NSProcessInfo.processInfo.systemUptime
            windowNumber:window.windowNumber
                 context:nil
             eventNumber:0
              clickCount:1
                pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
}

// Zen's WindowIsTiled() (expose_tiled_attribute_to_all_platforms.patch): the
// tiling state lives in a private NSWindow ivar, "_tilingStateController",
// whose -tilingState is non-nil only while the system window manager has the
// window tiled (macOS 15 and later). None of this is public API: it is all
// guarded and degrades to "not tiled" if the internals ever change.
bool WindowIsTiled(NSWindow* window) {
  // (Only if it is an object: a future AppKit that keeps the name but changes
  // the type would have `object_getIvar` read a struct as an `id`.)
  static Ivar controller_ivar = [] {
    Ivar ivar =
        class_getInstanceVariable([NSWindow class], "_tilingStateController");
    const char* type = ivar ? ivar_getTypeEncoding(ivar) : nullptr;
    return type && type[0] == '@' ? ivar : nullptr;
  }();
  if (!window || !controller_ivar) {
    return false;
  }
  id controller = object_getIvar(window, controller_ivar);
  if (![controller respondsToSelector:@selector(tilingState)]) {
    return false;
  }
  return [controller performSelector:@selector(tilingState)] != nil;
}

// The view that had the button down when the drag started: it, and no other,
// gets the release that the window server swallowed.
__weak NSResponder* g_drag_view = nil;

}  // namespace

bool StartNativeWindowMove(const BrowserView* browser_view) {
  NSWindow* window = DraggableWindow(browser_view);
  // The button must still be down: the window follows the pointer until it
  // goes up, and a drag started after a release would stick to the pointer.
  if (!window || !(NSEvent.pressedMouseButtons & 1)) {
    return false;
  }
  // The event that is being handled, if it is the drag of this window (AppKit
  // documents the parameter as the initiating mouse event); else, as Zen's
  // StartNativeWindowMoveCocoa(): a left-drag event at the pointer's place
  // (this runs from a posted task, after the event that moved the pointer).
  NSEvent* event = NSApp.currentEvent;
  if (!event || event.type != NSEventTypeLeftMouseDragged ||
      event.windowNumber != window.windowNumber) {
    event = MouseEvent(window, NSEventTypeLeftMouseDragged);
  }
  if (!event) {
    return false;
  }
  g_drag_view = window.firstResponder;
  [window performWindowDragWithEvent:event];
  return true;
}

void EndNativeWindowMove(const BrowserView* browser_view) {
  // The window server's drag swallowed the real release, and the page's view
  // still has its button down (-[RenderWidgetHostViewCocoa
  // _hasOpenMouseDown]): it would go on taking the pointer's moves, hover and
  // cursor over the sidebar, until the next click. Send it a release, to the
  // view that had the press (not to whatever is under the pointer now), and
  // only while the physical button is up: a button that is down again is a
  // new press.
  NSResponder* responder = g_drag_view;
  g_drag_view = nil;
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window || !responder || (NSEvent.pressedMouseButtons & 1) ||
      ![responder isKindOfClass:[NSView class]] ||
      static_cast<NSView*>(responder).window != window) {
    return;
  }
  if (NSEvent* event = MouseEvent(window, NSEventTypeLeftMouseUp)) {
    [responder mouseUp:event];
  }
}

bool IsWindowSnapped(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  return (widget && widget->IsMaximized()) || WindowIsTiled(window);
}

}  // namespace koan
