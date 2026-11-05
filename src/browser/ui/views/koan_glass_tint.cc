// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/widget/cocoa/nsCocoaWindow-mm.patch (the window material view)
//   prefs/zen/macos.yaml (window vibrancy and material)
//   src/zen/spaces/ZenGradientGenerator.mjs (the default theme's tint)
//   src/zen/common/styles/zen-theme.css, zen-toolbar.css (the transparent
//   window and toolbar)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_glass_tint.h"

namespace koan {

SkColor ResolveGlassTint(SkColor tint, bool window_active) {
  return window_active ? tint : SK_ColorTRANSPARENT;
}

bool ToolbarIsClear(bool in_glass_mode, bool compact_mode) {
  return in_glass_mode && !compact_mode;
}

float SidebarBackingOpacity(bool compact_mode) {
  return compact_mode ? 1.0f : 0.0f;
}

}  // namespace koan
