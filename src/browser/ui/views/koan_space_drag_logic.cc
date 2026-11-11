// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_drag_logic.h"

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

}  // namespace koan
