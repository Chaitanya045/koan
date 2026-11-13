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

FolderZone FolderRowZone(float fraction, bool collapsed, size_t items) {
  if (fraction < kFolderDragoverThreshold) {
    return FolderZone::kBefore;
  }
  // An open folder with two or more items shows them below its row: a drop
  // there goes into it (among them).
  if (fraction > 1 - kFolderDragoverThreshold && (collapsed || items < 2)) {
    return FolderZone::kAfter;
  }
  return FolderZone::kInto;
}

bool DropsBefore(const gfx::Rect& row, int y) {
  return y < row.y() + row.height() / 2;
}

SpaceListDrop PlaceInSpaceList(const std::vector<SpaceListRow>& rows,
                               const SpaceListLayout& layout,
                               int y) {
  SpaceListDrop drop;
  const size_t pinned_count = static_cast<size_t>(
      std::ranges::find_if_not(rows, &SpaceListRow::pinned) - rows.begin());
  // Zen's .zen-current-workspace-indicator: first among the pinned tabs.
  if (!layout.header.IsEmpty() && y >= layout.header.y() &&
      y < layout.header.bottom()) {
    drop.pinned = true;
    drop.lit_header = true;
    return drop;
  }
  drop.pinned = IsInPinnedDropSection(y, layout.separator.bottom());
  const size_t first = drop.pinned ? 0 : pinned_count;
  const size_t last = drop.pinned ? pinned_count : rows.size();
  if (first == last) {
    // An empty section: on the separator's top, or below the New Tab row.
    drop.slot = first;
    drop.line_row = layout.separator;
    drop.line_y = drop.pinned ? layout.separator.y() : layout.normal_start;
    return drop;
  }
  // The row under the pointer, or the nearest one between two rows.
  size_t row = first;
  while (row < last && rows[row].bounds.bottom() <= y) {
    ++row;
  }
  if (row == last) {
    // Past the section's last row: its end, outside folders
    // (#tabbrowser-arrowscrollbox-periphery, the separator, the empty
    // space).
    drop.slot = last;
    drop.line_row = rows[last - 1].bounds;
    drop.line_y = rows[last - 1].bounds.bottom();
    return drop;
  }
  if (y < rows[row].bounds.y() && row > first &&
      y - rows[row - 1].bounds.bottom() < rows[row].bounds.y() - y) {
    --row;
  }
  const SpaceListRow& target = rows[row];
  const int inside =
      std::clamp(y, target.bounds.y(),
                 std::max(target.bounds.y(), target.bounds.bottom() - 1));
  drop.line_row = target.bounds;
  if (target.kind == SpaceListRow::Kind::kTab) {
    const bool before = DropsBefore(target.bounds, inside);
    drop.slot = before ? row : row + 1;
    drop.folder = target.folder;
    drop.line_y = before ? target.bounds.y() : target.bounds.bottom();
    return drop;
  }
  const float fraction = target.bounds.height() > 0
                             ? static_cast<float>(inside - target.bounds.y()) /
                                   target.bounds.height()
                             : 0.f;
  switch (FolderRowZone(fraction, target.collapsed, target.items)) {
    case FolderZone::kBefore:
      drop.slot = row;
      drop.folder = target.folder;
      drop.line_y = target.bounds.y();
      break;
    case FolderZone::kAfter:
      drop.slot = std::clamp(target.end, row + 1, rows.size());
      drop.folder = target.folder;
      drop.line_y = target.bounds.bottom();
      break;
    case FolderZone::kInto:
      drop.slot = row + 1;
      drop.folder = target.folder_id;
      drop.lit_row = row;
      break;
  }
  return drop;
}

gfx::Rect DropLineBounds(const gfx::Rect& row, int y) {
  // The line from 10px in to 2px before the end (Zen's 2px separation, the
  // ring's 4px and 4px more), the ring's room before it.
  const int line_x = row.x() + 10;
  const int line_width = std::max(0, row.width() - 12);
  return gfx::Rect(line_x - kDropLineRingRoom, y - kDropLineTop,
                   line_width + kDropLineRingRoom, kDropLineRingSize);
}

std::u16string TruncateDragLabel(std::u16string_view title, int label_width) {
  size_t keep = static_cast<size_t>(std::max(0, label_width) / 6);
  if (title.size() <= keep) {
    return std::u16string(title);
  }
  // (Not half a surrogate pair.)
  if (keep > 0 && (title[keep - 1] & 0xFC00) == 0xD800) {
    --keep;
  }
  return std::u16string(title.substr(0, keep));
}

gfx::Vector2d DragImageGrab(size_t tab_count, const gfx::Vector2d& grab) {
  return tab_count > 1 ? gfx::Vector2d(18, 18) : grab;
}

std::optional<bool> SplitBandSide(const gfx::Rect& row,
                                  const gfx::Point& point,
                                  bool rtl) {
  if (row.IsEmpty() || !row.Contains(point)) {
    return std::nullopt;
  }
  const float ratio = static_cast<float>(point.y() - row.y()) /
                      static_cast<float>(row.height());
  if (ratio < kSplitDragThreshold || ratio > 1 - kSplitDragThreshold) {
    return std::nullopt;
  }
  const bool left = point.x() < row.x() + row.width() / 2;
  return left != rtl;
}

gfx::Rect SplitFakeTabBounds(const gfx::Rect& row, bool start, bool rtl) {
  gfx::Rect half = row;
  half.Inset(kSplitFakeTabMargin);
  half.set_width(half.width() / 2);
  if (start == rtl) {
    half.set_x(row.right() - kSplitFakeTabMargin - half.width());
  }
  return half;
}

std::optional<SplitEdge> PageSplitEdge(const gfx::Rect& panels,
                                       const gfx::Point& point) {
  gfx::Rect inner = panels;
  inner.Inset(kSplitEdgeIgnored);
  if (inner.IsEmpty() || !inner.Contains(point)) {
    return std::nullopt;
  }
  const int quarter_width = panels.width() / 4;
  const int quarter_height = panels.height() / 4;
  struct Candidate {
    SplitEdge edge;
    int distance;
    int threshold;
  };
  const Candidate candidates[] = {
      {SplitEdge::kLeft, point.x() - panels.x(), quarter_width},
      {SplitEdge::kRight, panels.right() - point.x(), quarter_width},
      {SplitEdge::kTop, point.y() - panels.y(), quarter_height},
      {SplitEdge::kBottom, panels.bottom() - point.y(), quarter_height},
  };
  std::optional<SplitEdge> nearest;
  int best = 0;
  for (const Candidate& candidate : candidates) {
    if (candidate.distance < candidate.threshold &&
        (!nearest || candidate.distance < best)) {
      nearest = candidate.edge;
      best = candidate.distance;
    }
  }
  return nearest;
}

gfx::Rect FakePaneBounds(const gfx::Rect& panels,
                         SplitEdge edge,
                         int panes,
                         int gap) {
  panes = std::max(panes, 1);
  gfx::Rect pane = panels;
  switch (edge) {
    case SplitEdge::kLeft:
    case SplitEdge::kRight:
      pane.set_width(std::max(0, panels.width() / panes - gap));
      if (edge == SplitEdge::kRight) {
        pane.set_x(panels.right() - pane.width());
      }
      break;
    case SplitEdge::kTop:
    case SplitEdge::kBottom:
      pane.set_height(std::max(0, panels.height() / panes - gap));
      if (edge == SplitEdge::kBottom) {
        pane.set_y(panels.bottom() - pane.height());
      }
      break;
  }
  return pane;
}

}  // namespace koan
