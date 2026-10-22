// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenWindowDragChild.sys.mjs
//   zen-default-prefs.js
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// Where on a page a press may drag the window, and how far it must move
// first.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_WINDOW_DRAG_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_WINDOW_DRAG_H_

#include <string>

namespace koan {

// zen.view.drag-window-from-content.height-percentage: the drag region is
// the top 5% of the page.
inline constexpr float kWindowDragRegionFraction = 0.05f;

// Movement below this is a click, not a drag. Un-snapping a maximized (or
// tiled) window takes a firmer gesture, as with a title bar.
inline constexpr float kWindowDragThreshold = 4;
inline constexpr float kWindowDragSnappedThreshold = 50;

// JavaScript that evaluates to whether the page has nothing interactive at
// (x, y), in CSS pixels: no link, form control, image, media, frame,
// editable or focusable element, ARIA widget, selectable text or
// interactive cursor, and not the scrollbar. Zen's ZenWindowDragChild
// checks.
std::u16string IsEmptySpaceScript(double x, double y);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_WINDOW_DRAG_H_
