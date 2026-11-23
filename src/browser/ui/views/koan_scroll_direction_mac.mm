// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_scroll_direction.h"

#import <AppKit/AppKit.h>

namespace koan {

bool ScrollFollowsFingers(const ui::ScrollEvent& event) {
  if (!event.HasNativeEvent()) {
    return true;
  }
  return event.native_event().Get().directionInvertedFromDevice;
}

}  // namespace koan
