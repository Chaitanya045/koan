// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_drag_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace koan {

int SpaceDragEdge(int x, int width) {
  // Zen's #shouldSwitchSpace(): within the padding of an edge, on either
  // side of it.
  if (std::abs(x) <= kSpaceDragSwitchPadding) {
    return -1;
  }
  if (std::abs(x - width) <= kSpaceDragSwitchPadding) {
    return 1;
  }
  return 0;
}

size_t ReorderedSpaceIndex(const std::vector<SpaceButtonSpan>& spans,
                           size_t dragged,
                           int pointer) {
  for (size_t i = 0; i < spans.size(); ++i) {
    const SpaceButtonSpan& span = spans[i];
    if (i == dragged || pointer <= span.start || pointer >= span.end) {
      continue;
    }
    const bool before = pointer < span.start + (span.end - span.start) / 2;
    // Indices count the dragged button out of its old place.
    const size_t target = before ? i : i + 1;
    return target > dragged ? target - 1 : target;
  }
  return dragged;
}

SpaceStripLayout LayoutSpaceStrip(int room,
                                  int count,
                                  int min_width,
                                  int max_width) {
  if (count <= 0) {
    return {};
  }
  room = std::max(0, room);
  SpaceStripLayout layout;
  layout.button_width = std::clamp(room / count, min_width, max_width);
  const int content = count * layout.button_width;
  layout.start = std::max(0, (room - content) / 2);
  layout.max_scroll = std::max(0, content - room);
  return layout;
}

int ScrollSpaceIntoView(int scroll,
                        int room,
                        int max_scroll,
                        int start,
                        int end) {
  // "nearest": the start's side if it is cut off there, else the end's.
  if (start - kSpaceStripScrollMargin < scroll) {
    scroll = start - kSpaceStripScrollMargin;
  } else if (end + kSpaceStripScrollMargin > scroll + room) {
    scroll = end + kSpaceStripScrollMargin - room;
  }
  return std::clamp(scroll, 0, std::max(0, max_scroll));
}

double SpaceStripScrollProgress(base::TimeDelta elapsed) {
  if (elapsed >= kSpaceStripScrollDuration) {
    return 1;
  }
  // x(t) = 1 - (1 + wt) e^(-wt), w = sqrt(spring constant).
  const double wt = std::sqrt(250.0) * std::max(0.0, elapsed.InSecondsF());
  return 1 - (1 + wt) * std::exp(-wt);
}

}  // namespace koan
