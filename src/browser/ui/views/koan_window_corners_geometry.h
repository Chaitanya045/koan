// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CORNERS_GEOMETRY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CORNERS_GEOMETRY_H_

#include <algorithm>

#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace koan {

// The corner radii of a restored window on Linux, where the client-side frame
// draws the window's shape (BrowserFrameViewLinux): `radius` at the top, as
// upstream has it, and at the bottom too where `round_bottom`, so that all
// four corners match, as on the Mac.
inline gfx::RoundedCornersF RestoredCorners(float radius, bool round_bottom) {
  return round_bottom ? gfx::RoundedCornersF(radius)
                      : gfx::RoundedCornersF(radius, radius, 0, 0);
}

// The radii a view with `bounds` needs so that it doesn't paint over the
// rounded corners of a window with `window` bounds and `window_corners`
// radii (both rects in the same coordinates): a corner's radius where that
// corner of the view reaches the window's corner, 0 elsewhere. Half a DIP of
// slack absorbs rounding between the frame's float clip and view bounds.
inline gfx::RoundedCornersF CornersAtWindowCorners(
    const gfx::RectF& bounds,
    const gfx::RectF& window,
    const gfx::RoundedCornersF& window_corners) {
  constexpr float kSlack = 0.5f;
  const bool left = bounds.x() <= window.x() + kSlack;
  const bool top = bounds.y() <= window.y() + kSlack;
  const bool right = bounds.right() >= window.right() - kSlack;
  const bool bottom = bounds.bottom() >= window.bottom() - kSlack;
  return gfx::RoundedCornersF(
      top && left ? window_corners.upper_left() : 0,
      top && right ? window_corners.upper_right() : 0,
      bottom && right ? window_corners.lower_right() : 0,
      bottom && left ? window_corners.lower_left() : 0);
}

// Each corner's larger radius of `a` and `b`.
inline gfx::RoundedCornersF LargerCorners(const gfx::RoundedCornersF& a,
                                          const gfx::RoundedCornersF& b) {
  return gfx::RoundedCornersF(std::max(a.upper_left(), b.upper_left()),
                              std::max(a.upper_right(), b.upper_right()),
                              std::max(a.lower_right(), b.lower_right()),
                              std::max(a.lower_left(), b.lower_left()));
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CORNERS_GEOMETRY_H_
