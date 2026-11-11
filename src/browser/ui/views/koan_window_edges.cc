// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_edges.h"

#include <algorithm>
#include <cstdlib>

namespace koan {

WindowEdge CrossedWindowEdge(const gfx::Rect& window, const gfx::Point& point) {
  if (window.IsEmpty()) {
    return WindowEdge::kNone;
  }
  // (A rect's right and bottom are one past its last pixel: the pointer
  // stopped by the screen at the window's last column is on that edge.)
  const int x = std::clamp(point.x(), window.x(), window.right());
  const int y = std::clamp(point.y(), window.y(), window.bottom());
  if (std::abs(y - window.y()) <= kCrossedEdgeDistance) {
    return WindowEdge::kTop;
  }
  if (std::abs(y - window.bottom()) <= kCrossedEdgeDistance) {
    return WindowEdge::kBottom;
  }
  if (std::abs(x - window.x()) <= kCrossedEdgeDistance) {
    return WindowEdge::kLeft;
  }
  if (std::abs(x - window.right()) <= kCrossedEdgeDistance) {
    return WindowEdge::kRight;
  }
  return WindowEdge::kNone;
}

bool IsAlongElement(const gfx::Rect& element,
                    WindowEdge edge,
                    const gfx::Point& point) {
  switch (edge) {
    case WindowEdge::kLeft:
    case WindowEdge::kRight:
      return element.y() - kAlongEdgeTolerance < point.y() &&
             point.y() < element.bottom() + kAlongEdgeTolerance;
    case WindowEdge::kTop:
    case WindowEdge::kBottom:
      return element.x() - kAlongEdgeTolerance < point.x() &&
             point.x() < element.right() + kAlongEdgeTolerance;
    case WindowEdge::kNone:
      return false;
  }
  return false;
}

bool IsWithinOutsideTolerance(const gfx::Rect& window,
                              WindowEdge edge,
                              int max_offset,
                              const gfx::Point& point) {
  const int x = point.x();
  const int y = point.y();
  const bool along_x = x >= window.x() && x <= window.right();
  const bool along_y = y >= window.y() && y <= window.bottom();
  if (along_x && along_y) {
    // Back over the window: the hover inside it decides.
    return true;
  }
  switch (edge) {
    case WindowEdge::kLeft:
      return x >= window.x() - max_offset && x < window.x() && along_y;
    case WindowEdge::kRight:
      return x > window.right() && x <= window.right() + max_offset && along_y;
    case WindowEdge::kTop:
      return y >= window.y() - max_offset && y < window.y() && along_x;
    case WindowEdge::kBottom:
      return y > window.bottom() && y <= window.bottom() + max_offset &&
             along_x;
    case WindowEdge::kNone:
      return false;
  }
  return false;
}

}  // namespace koan
