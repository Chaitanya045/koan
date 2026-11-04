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

// AppKit draws the strip itself (a grey bar under the menu bar, about 32pt)
// with the title bar view that holds the traffic lights, however transparent
// the window says its title bar is. Fades that view (the ancestor of the
// lights with that class name; it moves between the window's frame and
// AppKit's fullscreen window, so look for it from the lights each time).
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

void HideFullscreenTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  window.titlebarAppearsTransparent = YES;
  window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
  SetTrafficLightsShown(browser_view, false);
  SetTitlebarContainerOpacity(window, 0);
}

void RestoreFullscreenTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (window) {
    SetTitlebarContainerOpacity(window, 1);
  }
}

}  // namespace koan
