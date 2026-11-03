// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TRAFFIC_LIGHTS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TRAFFIC_LIGHTS_H_

#include <optional>

#include "build/build_config.h"

class BrowserView;

namespace views {
class View;
}

namespace koan {

// The macOS window buttons ("traffic lights") are AppKit's: Koan can't move
// or draw them, but it can say how tall the title bar is, and AppKit centres
// them in it (Chromium's BrowserNativeWidgetWindow), and it can hide them.
// Zen puts them in the sidebar's top row (ZenVerticalTabsManager:
// `topButtons.prepend(windowButtons)`) and hides them with the sidebar and
// toolbar in compact mode; Chromium already keeps room for them beside the
// sidebar's toggle (the caption button width of its vertical tab strip), so
// what Koan adds is the row's height and the hiding (KoanCompactModeController
// runs it like the other platforms' caption buttons). Nothing but
// SetTrafficLightsShown() is macOS-only code, so the Linux build compiles the
// rest; only macOS asks.

// Hook for BrowserNativeWidgetMac::GetWindowFrameTitlebarHeight() (a hook
// patch): the height the traffic lights are centred in, or null to leave it
// to Chromium (the toolbar's height). `browser_view` is the window's
// BrowserView.
std::optional<int> GetTrafficLightsTitlebarHeight(
    const views::View* browser_view);

// Whether the pointer is over the traffic lights. Those sit over the window
// but not in its views, so the pointer over them is a mouse exit for the
// window; compact mode keeps the sidebar and toolbar it revealed while the
// pointer is on them (Zen's ZenCompactMode.mjs, onLeave).
bool IsPointerOverTrafficLights(const BrowserView* browser_view);

#if BUILDFLAG(IS_MAC)
// Shows or hides the window's traffic lights.
void SetTrafficLightsShown(const BrowserView* browser_view, bool shown);

// Makes sure nothing of the window's title bar paints: the content fills the
// window, the title bar is transparent, with no separator line (Chromium sets
// the first two; the last is automatic, which macOS may draw as a strip under
// the traffic lights). Idempotent.
void ClearTitlebar(const BrowserView* browser_view);
#endif

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TRAFFIC_LIGHTS_H_
