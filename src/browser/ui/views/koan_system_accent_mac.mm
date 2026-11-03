// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_system_accent.h"

#import <AppKit/AppKit.h>

#include "skia/ext/skia_utils_mac.h"

namespace koan {

std::optional<SkColor> GetSystemAccentColor() {
  // Chromium's macOS settings provider has no accent colour; read AppKit's
  // (a catalog colour, so convert it to one with components first).
  NSColor* color = [NSColor.controlAccentColor
      colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
  if (!color) {
    return std::nullopt;
  }
  return skia::NSDeviceColorToSkColor(color);
}

}  // namespace koan
