// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_keyboard_state.h"

#import <AppKit/AppKit.h>

namespace koan {

bool IsControlKeyDown() {
  // The hardware state now, not that of the event being handled.
  return (NSEvent.modifierFlags & NSEventModifierFlagControl) != 0;
}

}  // namespace koan
