// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_traffic_lights.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/views/widget/widget.h"

namespace koan {

void SetTrafficLightsShown(const BrowserView* browser_view, bool shown) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    [[window standardWindowButton:type] setHidden:!shown];
  }
}

bool IsPointerOverWindow(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window || !window.isOnActiveSpace) {
    return false;
  }
  // The topmost window of the screen at the pointer (windows that are not
  // on screen are not listed), this one or one that hangs from it.
  const NSInteger number = [NSWindow windowNumberAtPoint:NSEvent.mouseLocation
                             belowWindowWithWindowNumber:0];
  for (NSWindow* under = [NSApp windowWithWindowNumber:number]; under;
       under = under.parentWindow) {
    if (under == window) {
      return true;
    }
  }
  return false;
}

void ClearTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  window.titlebarAppearsTransparent = YES;
  window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
  window.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

namespace {

// The title bar view that holds the traffic lights (the ancestor of the
// lights with that class name; it moves between the window's frame and
// AppKit's fullscreen window, so look for it from the lights each time): its
// opacity, 1 as AppKit leaves it.
void SetTitlebarContainerOpacity(NSWindow* window, CGFloat alpha) {
  for (NSView* view =
           [window standardWindowButton:NSWindowCloseButton].superview;
       view; view = view.superview) {
    if ([NSStringFromClass([view class])
            isEqualToString:@"NSTitlebarContainerView"]) {
      view.alphaValue = alpha;
    }
  }
}

}  // namespace

void ShowFullscreenTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  SetTitlebarContainerOpacity(window, 1);
  SetTrafficLightsShown(browser_view, true);
}

}  // namespace koan
