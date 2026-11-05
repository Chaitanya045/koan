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

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_TINT_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_TINT_H_

#include "third_party/skia/include/core/SkColor.h"

// The rules of a translucent window that need no views, so they can be
// unit-tested.
namespace koan {

// The tint over a translucent window's blur: `tint` (kColorKoanGlassTint,
// Zen's default theme: dark black at 40%, light none) while the window is
// active, and clear while it isn't (Zen's inactive window is flat: no blur,
// no tint).
SkColor ResolveGlassTint(SkColor tint, bool window_active);

// Whether the toolbar and the top container paint nothing: in glass mode,
// except in compact mode, where they float over the page and keep their
// colours as a backing.
bool ToolbarIsClear(bool in_glass_mode, bool compact_mode);

// The opacity of the sidebar region's colour in glass mode: 0, or 1 in compact
// mode, where the sidebar floats over the page and needs a backing.
float SidebarBackingOpacity(bool compact_mode);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_TINT_H_
