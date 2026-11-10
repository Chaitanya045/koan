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

// Zen's drag and drop between Spaces (ZenDragAndDrop.js, ZenSpaceIcons.mjs),
// as plain logic for the unit tests.

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

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_
