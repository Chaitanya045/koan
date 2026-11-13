// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_rearrange.h"

namespace koan {

RearrangeSide RearrangeSideAt(const gfx::RectF& pane,
                              const gfx::PointF& point) {
  const float horizontal = pane.width() * kRearrangeEdgeFraction;
  const float vertical = pane.height() * kRearrangeEdgeFraction;
  if (point.x() <= pane.x() + horizontal) {
    return RearrangeSide::kLeft;
  }
  if (point.x() > pane.right() - horizontal) {
    return RearrangeSide::kRight;
  }
  if (point.y() <= pane.y() + vertical) {
    return RearrangeSide::kTop;
  }
  if (point.y() > pane.bottom() - vertical) {
    return RearrangeSide::kBottom;
  }
  return RearrangeSide::kCenter;
}

gfx::RectF RearrangeDropZone(const gfx::RectF& pane, RearrangeSide side) {
  gfx::RectF zone = pane;
  switch (side) {
    case RearrangeSide::kCenter:
      break;
    case RearrangeSide::kLeft:
      zone.set_width(pane.width() / 2);
      break;
    case RearrangeSide::kRight:
      zone.set_x(pane.x() + pane.width() / 2);
      zone.set_width(pane.width() / 2);
      break;
    case RearrangeSide::kTop:
      zone.set_height(pane.height() / 2);
      break;
    case RearrangeSide::kBottom:
      zone.set_y(pane.y() + pane.height() / 2);
      zone.set_height(pane.height() / 2);
      break;
  }
  return zone;
}

RearrangeResult RearrangeSplit(const split_tabs::SplitTabVisualData& current,
                               int dragged_index,
                               RearrangeSide side,
                               bool rtl) {
  if (side == RearrangeSide::kCenter) {
    return {.reverse = true, .visual_data = current};
  }
  const bool side_by_side =
      side == RearrangeSide::kLeft || side == RearrangeSide::kRight;
  // The dragged pane's place in the new split: first on the left (the
  // right in a right-to-left UI) or on top.
  const bool first = side_by_side ? ((side == RearrangeSide::kLeft) != rtl)
                                  : side == RearrangeSide::kTop;
  return {.reverse = (first ? 0 : 1) != dragged_index,
          .visual_data = split_tabs::SplitTabVisualData(
              side_by_side ? split_tabs::SplitTabLayout::kSideBySide
                           : split_tabs::SplitTabLayout::kStacked,
              0.5)};
}

}  // namespace koan
