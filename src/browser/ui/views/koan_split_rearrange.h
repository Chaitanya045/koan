// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's split view rearrange (ZenViewSplitter's onBrowserDragOver and
// onBrowserDrop), for Chromium's two panes: where the pointer is over the
// other pane, what the drop zone covers, and the split the drop makes. Pure
// geometry and logic, for the unit tests (koan_split_header.h draws it).

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_REARRANGE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_REARRANGE_H_

#include "components/split_tabs/split_tab_visual_data.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"

namespace koan {

// zen.splitView.rearrange-edge-hover-size: a pane's outer 24% on each side
// is that side's drop area, the rest its centre. (Zen's prefs file spells
// it rearrange-hover-size, which nothing reads; the code's default is 24.)
inline constexpr float kRearrangeEdgeFraction = 0.24f;

// Where over the pane under the pointer a dragged pane would go
// (calculateHoverSide()), in screen terms: kLeft is the left edge also in a
// right-to-left UI.
enum class RearrangeSide { kCenter, kLeft, kRight, kTop, kBottom };

// The side of `pane` under `point` (which must be inside it). Zen tests the
// left and right edges first, then the top and bottom: a corner is a left
// or right drop.
RearrangeSide RearrangeSideAt(const gfx::RectF& pane, const gfx::PointF& point);

// The drop zone over `pane`: the whole pane for kCenter, else its half on
// that side (Zen's inset grows on the opposite side by half the pane).
gfx::RectF RearrangeDropZone(const gfx::RectF& pane, RearrangeSide side);

// The split after dropping the pane at `dragged_index` (0, the split's
// first tab, or 1) on `side` of the other pane, from `current`:
// - kCenter swaps the two panes (swapNodes()): each slot keeps its size, so
//   the ratio stays and the tabs are reversed;
// - an edge puts the dragged pane beside the other on that side, at half
//   the space (removeNode() then splitIntoNode(..., 0.5)): side by side for
//   kLeft / kRight, stacked for kTop / kBottom, ratio 0.5.
// In a right-to-left UI a split's first tab is on the right.
struct RearrangeResult {
  // Whether the split's two tabs swap places (TabStripModel's
  // ReverseTabsInSplit()).
  bool reverse = false;
  split_tabs::SplitTabVisualData visual_data;

  friend bool operator==(const RearrangeResult&,
                         const RearrangeResult&) = default;
};
RearrangeResult RearrangeSplit(const split_tabs::SplitTabVisualData& current,
                               int dragged_index,
                               RearrangeSide side,
                               bool rtl);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_REARRANGE_H_
