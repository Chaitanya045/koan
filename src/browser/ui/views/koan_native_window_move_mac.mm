// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   nsZenNativeWindowMove.mm
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_zen_window_drag.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/views/widget/widget.h"

namespace koan {

bool StartNativeWindowMove(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  // The button must still be down: the window follows the pointer until it
  // goes up, and a drag started after a release would stick to the pointer.
  if (!window || !(NSEvent.pressedMouseButtons & 1)) {
    return false;
  }
  // As Zen's StartNativeWindowMoveCocoa(): a left-drag event at the pointer's
  // place, for AppKit to track from (this runs from a posted task, not inside
  // the event that moved the pointer).
  NSEvent* event = [NSEvent
      mouseEventWithType:NSEventTypeLeftMouseDragged
                location:[window convertPointFromScreen:NSEvent.mouseLocation]
           modifierFlags:0
               timestamp:NSProcessInfo.processInfo.systemUptime
            windowNumber:window.windowNumber
                 context:nil
             eventNumber:0
              clickCount:1
                pressure:1.0];
  if (!event) {
    return false;
  }
  [window performWindowDragWithEvent:event];
  return true;
}

}  // namespace koan
