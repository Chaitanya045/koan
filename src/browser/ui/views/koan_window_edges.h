// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_EDGES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_EDGES_H_

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace koan {

// An edge of a window.
enum class WindowEdge { kNone, kTop, kBottom, kLeft, kRight };

// How close to an edge of the window the pointer must be, as it leaves it, to
// have left through that edge (Zen's _getCrossedEdge(), maxDistance).
inline constexpr int kCrossedEdgeDistance = 10;

// How far along an edge, past the ends of the revealed element, a pointer that
// left through that edge still counts as leaving from the element (Zen's
// _positionInBounds(), error).
inline constexpr int kAlongEdgeTolerance = 7;

// How far outside the window the pointer may go, after leaving through the
// sidebar's edge (left or right) or the toolbar's (top), before what it
// revealed hides (Zen's zen.view.compact.outside-window-edge-offset
// .horizontal and .vertical, the shipped values).
inline constexpr int kOutsideWindowHorizontalOffset = 200;
inline constexpr int kOutsideWindowVerticalOffset = 100;

// Which edge of `window` a pointer at `point`, which has just left it, left
// through: `point` is clamped to `window`, and the first edge within
// `kCrossedEdgeDistance` of it counts, in the order top, bottom, left, right
// (Zen's order: a pointer leaving at a corner left through the top or the
// bottom). kNone if none is that close. All in the same (screen) coordinates.
WindowEdge CrossedWindowEdge(const gfx::Rect& window, const gfx::Point& point);

// Whether `point`, which left through `edge`, is along `element` on that edge:
// within its vertical extent for the left and right edges, its horizontal one
// for the top and bottom, give or take `kAlongEdgeTolerance`.
bool IsAlongElement(const gfx::Rect& element,
                    WindowEdge edge,
                    const gfx::Point& point);

// Whether a pointer tracked after leaving `window` through `edge` is still
// where what it revealed stays: over the window, or outside `edge` by at most
// `max_offset` and within the window's extent along that edge (Zen's
// ZenMouseTracker::IsPointerWithinBounds()).
bool IsWithinOutsideTolerance(const gfx::Rect& window,
                              WindowEdge edge,
                              int max_offset,
                              const gfx::Point& point);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_EDGES_H_
