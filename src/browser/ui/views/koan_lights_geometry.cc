// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_lights_geometry.h"

#include "ui/gfx/color_utils.h"

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

SkColor MonoTrafficLightColor(SkColor toolbox_text) {
  // Dark text: a light theme.
  const float alpha = color_utils::IsDark(toolbox_text) ? 0.08f : 0.15f;
  return SkColorSetA(toolbox_text, static_cast<U8CPU>(alpha * 255 + 0.5f));
}

MonoLightsState GetMonoLightsState(bool pointer_over_buttons,
                                   bool has_sheet,
                                   bool buttons_hidden) {
  const bool buttons = pointer_over_buttons || has_sheet;
  return {.buttons_opaque = buttons, .dots_drawn = !buttons && !buttons_hidden};
}

}  // namespace koan
