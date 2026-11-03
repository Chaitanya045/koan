// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/fonts/koan_fonts.h"

namespace koan::fonts {

// Koan ships on macOS only; elsewhere the system font stays.
bool RegisterBundledFonts() {
  return false;
}

}  // namespace koan::fonts
