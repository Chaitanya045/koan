// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_welcome_state.h"

namespace koan {

namespace {

// Only compared, never used: the window, or null.
const void* g_welcome_window = nullptr;

}  // namespace

void SetWelcomeWindow(const BrowserWindowInterface* browser) {
  g_welcome_window = browser;
}

bool IsWelcomeShowing(const BrowserWindowInterface& browser) {
  return g_welcome_window && g_welcome_window == &browser;
}

}  // namespace koan
