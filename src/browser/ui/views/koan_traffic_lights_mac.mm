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
  for (NSWindowButton type : {NSWindowCloseButton, NSWindowMiniaturizeButton,
                              NSWindowZoomButton}) {
    [[window standardWindowButton:type] setHidden:!shown];
  }
}

}  // namespace koan
