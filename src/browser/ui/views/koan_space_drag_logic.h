// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "base/time/time.h"

namespace koan {

// Zen's drag and drop between Spaces (ZenDragAndDrop.js, ZenSpaceIcons.mjs)
// and its Space switcher's layout, as plain logic for the unit tests.

// zen.workspaces.dnd-switch-padding: how near (either side) the sidebar's
// left or right edge a dragged tab switches Space.
inline constexpr int kSpaceDragSwitchPadding = 20;
// zen.tabs.dnd-switch-space-delay: how long it has to stay there.
inline constexpr base::TimeDelta kSpaceDragSwitchDelay =
    base::Milliseconds(500);
// ZenSpaceIcons.mjs: how far a Space button moves before it is reordered.
inline constexpr int kSpaceReorderThreshold = 5;

// Which way a tab dragged to `x` (in the sidebar's coordinates, LTR) switches
// Space in a sidebar `width` wide: -1 (the previous Space, near the left
// edge), 1 (the next, near the right edge) or 0.
int SpaceDragEdge(int x, int width);

// One Space button's extent along the bar (x in a row, y in a column).
struct SpaceButtonSpan {
  int start = 0;
  int end = 0;
};

// Zen's reorder: where the dragged button (`dragged`, an index into `spans`,
// which are in order) goes with the pointer at `pointer`. Over another
// button's first half it goes before it, over its second half after it;
// elsewhere it stays. Returns the dragged button's new index.
size_t ReorderedSpaceIndex(const std::vector<SpaceButtonSpan>& spans,
                           size_t dragged,
                           int pointer);

// Zen's Space switcher in the sidebar's bottom bar (#zen-workspaces-button,
// ZenSpaceManager.mjs onWindowResize(), zen-workspaces.css): `count` buttons
// in a strip `room` wide, each as wide as fits between `min_width` (the
// icons-overflow minimum) and `max_width`. They are centred while they fit;
// beyond that the strip scrolls sideways (overflow-x: auto, no scrollbar)
// and they start at its start (justify-content: safe center).
struct SpaceStripLayout {
  int button_width = 0;
  // Where the first button starts in the strip, before scrolling.
  int start = 0;
  // How far the strip scrolls: 0 while the buttons fit.
  int max_scroll = 0;
};
SpaceStripLayout LayoutSpaceStrip(int room,
                                  int count,
                                  int min_width,
                                  int max_width);

// The scroll-margin of a Space button (zen-workspaces.css).
inline constexpr int kSpaceStripScrollMargin = 20;

// Zen's scrollIntoView({inline: "nearest"}) on a Space button
// (ZenSpaceIcons.mjs, for the current Space and the hovered one): the
// strip's scroll offset, from `scroll`, that shows the button spanning
// [start, end) (unscrolled, in the strip) with its scroll-margin, moving as
// little as it can, within [0, max_scroll].
int ScrollSpaceIntoView(int scroll,
                        int room,
                        int max_scroll,
                        int start,
                        int end);

// How long Firefox's smooth scroll (scroll-behavior: smooth, which Zen's
// strip and scrollIntoView() use) takes to settle, and how far along it is
// after `elapsed` (0 to 1): a critically damped spring
// (layout.css.scroll-behavior.spring-constant 250, damping-ratio 1).
inline constexpr base::TimeDelta kSpaceStripScrollDuration =
    base::Milliseconds(600);
double SpaceStripScrollProgress(base::TimeDelta elapsed);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_
