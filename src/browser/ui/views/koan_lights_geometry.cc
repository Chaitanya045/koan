// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_lights_geometry.h"

#include <algorithm>

namespace koan {

gfx::Rect MoveClearOf(const gfx::Rect& bounds,
                      const gfx::Rect& obstacle,
                      int container_width) {
  if (!bounds.Intersects(obstacle)) {
    return bounds;
  }
  gfx::Rect moved = bounds;
  if (container_width >= obstacle.right() + bounds.width()) {
    moved.set_x(std::max(bounds.x(), obstacle.right()));
  } else {
    moved.set_y(std::max(bounds.y(), obstacle.bottom()));
  }
  return moved;
}

}  // namespace koan
