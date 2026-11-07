// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/zen/compact-mode/sidebar.inc.css
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_CURVE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_CURVE_H_

#include "base/time/time.h"

namespace koan {

// How long the hidden sidebar takes to slide in (`--zen-compact-mode-time`)
// and out (its `transition: translate 0.15s ease`), in Zen's compact mode.
inline constexpr base::TimeDelta kSidebarRevealDuration =
    base::Milliseconds(250);
inline constexpr base::TimeDelta kSidebarHideDuration = base::Milliseconds(150);

// `zen.view.compact.sidebar-keep-hover.duration`: how long the revealed
// sidebar stays after the pointer leaves it or the menu it opened closes
// (measured live: it collapses 150-185 ms after the pointer leaves).
inline constexpr base::TimeDelta kSidebarKeepHoverDuration =
    base::Milliseconds(150);

// Zen's `--zen-compact-mode-func`: the `translate` easing of the revealed
// sidebar, a spring-shaped `linear()` curve. `t` is the fraction of the time
// (clamped to 0..1); the result is 0 at the start, overshoots 1 by about 1%
// and ends at 1.003423.
double SidebarSpringAt(double t);

// CSS `ease` (cubic-bezier(0.25, 0.1, 0.25, 1)), for the slide out.
double SidebarEaseAt(double t);

// Where the slide is, 0 (hidden) to 1 (out), `t` of the way from `from` to
// `target`: the spring going out, the ease going in.
double SidebarSlideValue(double from, double target, double t);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_CURVE_H_
