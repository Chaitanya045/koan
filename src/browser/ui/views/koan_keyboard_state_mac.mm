// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_keyboard_state.h"

#import <AppKit/AppKit.h>

namespace koan {

namespace {

class MacControlKeyState : public ControlKeyState {
 public:
  bool IsDown() const override {
    // The hardware state now, not that of the event being handled.
    return (NSEvent.modifierFlags & NSEventModifierFlagControl) != 0;
  }
};

}  // namespace

// static
std::unique_ptr<ControlKeyState> ControlKeyState::Create(
    views::Widget* widget) {
  return std::make_unique<MacControlKeyState>();
}

}  // namespace koan
