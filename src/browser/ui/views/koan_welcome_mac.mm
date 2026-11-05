// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_welcome_mac.h"

#import <AppKit/AppKit.h>

#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace koan {

void SetWindowButtonsHidden(views::Widget* window, bool hidden) {
  if (!window) {
    return;
  }
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window) {
    return;
  }
  for (NSWindowButton button :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    [[ns_window standardWindowButton:button] setHidden:hidden];
  }
}

}  // namespace koan
