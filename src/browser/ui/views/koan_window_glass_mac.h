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

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_GLASS_MAC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_GLASS_MAC_H_

#import <AppKit/AppKit.h>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider_key.h"

namespace ui {
class ColorProvider;
}

// Objective-C++ only: what BrowserNativeWidgetMac's glass frame calls (see
// koan_window_glass.h).
class BrowserView;

namespace koan {

// The view under a window's contents that shows the blurred desktop: an
// NSVisualEffectView behind the window that lets every click through. It
// follows the window's active state: an inactive window is flat, like Zen's
// greyed-out inactive windows.
NSView* CreateGlassBackgroundView(NSRect frame);

// The colour laid over the blur, with alpha (koan_glass_tint.h): the default
// theme's tint, kColorKoanGlassTint, and while the window is inactive that
// tint over the system's window background, opaque (Zen's InactiveCaption).
//
// `color_mode` is the window's (Widget::GetColorMode()): it picks the dark or
// light system window background for the inactive state, whatever colour a
// theme gives the frame.
//
// Where the Space's theme paints `browser_view`'s window
// (koan::PaintsWindowBackground()): no tint (the theme paints its own
// version of it), and while inactive only the opaque window background, in
// the window's own scheme (Zen's wash is on #zen-main-app-wrapper, outside
// the Space's #browser), which the theme paints over.
SkColor GetGlassTint(const BrowserView* browser_view,
                     const ui::ColorProvider& colors,
                     ui::ColorProviderKey::ColorMode color_mode,
                     bool window_active);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_GLASS_MAC_H_
