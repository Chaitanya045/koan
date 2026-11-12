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

size_t EssentialsDropSlot(const std::vector<gfx::Rect>& tiles,
                          const gfx::Point& point,
                          bool rtl) {
  if (!tiles.empty() && point.y() < tiles.front().y()) {
    return 0;
  }
  for (size_t i = 0; i < tiles.size(); ++i) {
    const gfx::Rect& tile = tiles[i];
    if (!tile.Contains(point)) {
      continue;
    }
    const bool far_half = rtl ? point.x() < tile.CenterPoint().x()
                              : point.x() > tile.CenterPoint().x();
    return far_half ? i + 1 : i;
  }
  return tiles.size();
}

gfx::Rect EmptyEssentialsDropZone(const gfx::Size& sidebar,
                                  const gfx::Rect& header,
                                  const std::optional<gfx::Rect>& promo) {
  // (A promo not laid out yet is empty, and the header still where it was.)
  const int bottom = promo && !promo->IsEmpty()
                         ? promo->bottom()
                         : header.y() + header.height() / 2;
  return gfx::Rect(0, 0, sidebar.width(),
                   std::clamp(bottom, 0, sidebar.height()));
}

gfx::Rect EssentialsTopDropZone(const gfx::Size& sidebar,
                                const gfx::Rect& grid) {
  if (grid.IsEmpty()) {
    return gfx::Rect();
  }
  return gfx::Rect(0, 0, sidebar.width(),
                   std::clamp(grid.y(), 0, sidebar.height()));
}

size_t SpaceListDropSlot(const std::vector<gfx::Rect>& rows, int y) {
  // The rows are top to bottom, so the slot is the number of rows whose
  // middle is above the pointer.
  size_t slot = 0;
  while (slot < rows.size() && rows[slot].CenterPoint().y() < y) {
    ++slot;
  }
  return slot;
}

SpaceDropPlace SpaceListDropPlace(const std::vector<gfx::Rect>& rows,
                                  size_t pinned_count,
                                  const gfx::Rect& separator,
                                  int normal_start,
                                  int y) {
  pinned_count = std::min(pinned_count, rows.size());
  SpaceDropPlace place;
  place.pinned = IsInPinnedDropSection(y, separator.bottom());
  const auto first = rows.begin() + (place.pinned ? 0 : pinned_count);
  const auto last = place.pinned ? rows.begin() + pinned_count : rows.end();
  const std::vector<gfx::Rect> section(first, last);
  const size_t slot = SpaceListDropSlot(section, y);
  place.slot = static_cast<size_t>(first - rows.begin()) + slot;
  if (slot < section.size()) {
    place.line_y = section[slot].y();
  } else if (!section.empty()) {
    place.line_y = section.back().bottom();
  } else {
    place.line_y = place.pinned ? separator.y() : normal_start;
  }
  return place;
}

bool DragDropsPinned(std::optional<bool> prev_pinned,
                     std::optional<bool> next_pinned,
                     bool pointer_pinned) {
  // (Checked in this order, as KoanPinnedTabController::Normalize() does: a
  // pinned tab below a normal one leaves the pinned tabs first.)
  if (prev_pinned == false) {
    return false;
  }
  if (next_pinned == true) {
    return true;
  }
  return pointer_pinned;
}

}  // namespace koan
