// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Koan's changes, Copyright 2026 The Koan Authors. See
// koan_theme_platform.h for the material it ports.

#include "koan/browser/ui/views/koan_theme_platform.h"

namespace koan {

bool UsesNativePopoverMaterial() {
  return false;
}

void UpdatePopoverMaterial(views::Widget* widget,
                           const gfx::Rect& panel_in_widget,
                           int radius,
                           bool dark) {}

}  // namespace koan
