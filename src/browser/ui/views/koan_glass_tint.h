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

// The colour laid over a translucent window's blur: `tint`
// (kColorKoanGlassTint, Zen's default theme: dark black at 40%, light none)
// while the window is active. While it isn't, Zen's window is flat: its
// wrapper is painted the opaque system colour InactiveCaption
// (`#zen-main-app-wrapper:-moz-window-inactive`, zen-browser-ui.css; on macOS
// NSColor.windowBackgroundColor), and the theme's tint, a child of the
// wrapper, still paints over it. So inactive, the result is `tint` composited
// over the opaque `inactive_wash`: opaque, no blur.
SkColor ResolveGlassTint(SkColor tint,
                         bool window_active,
                         SkColor inactive_wash);

// Whether the toolbar and the top container paint nothing: in glass mode,
// except in compact mode, where they float over the page and keep their
// colours as a backing, and except where the Space's theme paints the window
// (`themed`, koan::PaintsWindowBackground()): they keep upstream's theme
// colour, which the theme's ThemedBackground hook paints translucent.
bool ToolbarIsClear(bool in_glass_mode, bool compact_mode, bool themed);

// The opacity of the sidebar region's colour in glass mode: 0, or 1 in compact
// mode, where the sidebar floats over the page and needs a backing.
float SidebarBackingOpacity(bool compact_mode);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_TINT_H_
