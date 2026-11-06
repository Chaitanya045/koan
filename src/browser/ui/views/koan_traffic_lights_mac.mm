// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_traffic_lights.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/remote_cocoa/app_shim/immersive_mode_controller_cocoa.h"
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
  // on screen are not listed), this one or one that hangs from it. In native
  // fullscreen, AppKit's title strip with the traffic lights (below the menu
  // bar, over the top of the window) is a window of its own: the pointer on it
  // is still on this window, as far as Koan's reveals are concerned (it is
  // also over this screen's one fullscreen window).
  const NSInteger number = [NSWindow windowNumberAtPoint:NSEvent.mouseLocation
                             belowWindowWithWindowNumber:0];
  for (NSWindow* under = [NSApp windowWithWindowNumber:number]; under;
       under = under.parentWindow) {
    if (under == window) {
      return true;
    }
    if (window.styleMask & NSWindowStyleMaskFullScreen &&
        remote_cocoa::IsNSToolbarFullScreenWindow(under) &&
        under.screen == window.screen) {
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

void ShowFullscreenTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window = widget && !widget->IsClosed()
                         ? widget->GetNativeWindow().GetNativeNSWindow()
                         : nil;
  if (!window) {
    return;
  }
  // (Only where AppKit's buttons are hidden: this runs on every layout.)
  if ([window standardWindowButton:NSWindowCloseButton].hidden) {
    SetTrafficLightsShown(browser_view, true);
  }
}

}  // namespace koan
