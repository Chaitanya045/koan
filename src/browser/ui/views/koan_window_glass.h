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

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_GLASS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_GLASS_H_

class CustomCornersBackground;

namespace views {
class View;
}

// Zen's translucent window on macOS ("glass"): the sidebar, the toolbar and
// the gutter around the page are the desktop blurred by the window server (an
// NSVisualEffectView under the whole window) with a tint over it; the page
// stays an opaque card.
//
// It is Chromium's own glass frame (GlassFrameService, BrowserNativeWidgetMac
// and the vertical-tab layout, which already draw the frame, the sidebar and
// the corners translucent; macOS 26 or later, as in Chromium), made to work
// the way Zen does: for every window (not one), with Zen's blur (the HUD
// window material) instead of Liquid Glass, over the whole window, and with
// Zen's default-theme tint (kColorKoanGlassTint). The hooks are listed in
// docs/REBASE.md.
namespace koan {

// Whether Koan can make windows translucent on this platform at all
// (macOS). The setting (Chromium's local-state pref glass_frame.enabled) is
// shown only then.
bool IsWindowGlassSupported();

// Whether windows may be translucent right now: not while macOS's Reduce
// Transparency is on (Zen's prefers-reduced-transparency). Always true off
// macOS, where nothing makes windows translucent.
bool IsWindowGlassAllowed();

// Starts watching Reduce Transparency. Called with every browser window;
// does nothing after the first.
void StartWindowGlass();

// Called from the tabbed layout after it has set the backgrounds: in glass
// mode the toolbar and the top container paint nothing (Zen's #nav-bar and
// toolbox are transparent), so they are part of the same translucent surface
// as the sidebar, except in compact mode, where they float over the page and
// keep their colours as a backing. Out of glass mode, upstream's colours are
// left alone.
void OnGlassLayout(const views::View* browser_view,
                   bool in_glass_mode,
                   CustomCornersBackground* toolbar_background,
                   CustomCornersBackground* top_container_background);

// Whether the tabbed layout leaves the top container's colour to
// OnGlassLayout(): in glass mode, not in compact mode. (Upstream sets it on
// every layout; the two setting it back and forth repainted it each time.)
bool KeepsTopContainerClear(bool in_glass_mode,
                            const views::View* browser_view);

// The opacity of the sidebar region's colour in glass mode: 0, or 1 in compact
// mode, where the sidebar floats over the page and needs a backing.
float SidebarGlassOpacity(const views::View* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_GLASS_H_
