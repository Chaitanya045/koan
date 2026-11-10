// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_status_bubble.h"

#include <algorithm>
#include <cmath>

#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/style/typography.h"

namespace koan {

int StatusBubbleTextStyle() {
  return views::style::STYLE_EMPHASIZED;
}

bool PaintStatusBubble(gfx::Canvas* canvas,
                       const gfx::Size& size,
                       const ui::ColorProvider* colors,
                       bool active) {
  if (!colors) {
    return false;
  }
  // In pixels, so that the 1 DIP border stays sharp at any scale.
  gfx::ScopedCanvas scoped(canvas);
  const float scale = canvas->UndoDeviceScaleFactor();
  const float border = std::max(1.0f, std::floor(scale));
  gfx::RectF pill{gfx::SizeF(gfx::ScaleToRoundedSize(size, scale))};
  pill.Inset(border / 2);
  const float radius =
      std::min(kStatusBubbleCornerRadius * scale, pill.height() / 2);

  // CSS paints the background under the border (background-clip:
  // border-box), so the translucent border is over the fill.
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(
      colors->GetColor(active ? kColorStatusBubbleBackgroundFrameActive
                              : kColorStatusBubbleBackgroundFrameInactive));
  canvas->DrawRoundRect(pill, radius, flags);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(border);
  flags.setColor(colors->GetColor(kColorStatusBubbleShadow));
  canvas->DrawRoundRect(pill, radius, flags);
  return true;
}

}  // namespace koan
