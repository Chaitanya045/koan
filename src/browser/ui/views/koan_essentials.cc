// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials.h"

#include <algorithm>

#include "cc/paint/paint_flags.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"

namespace koan {

namespace {

// Zen's Essentials grid (.zen-essentials-container) fits at most four tiles
// across (each at least 23.7% of the row).
constexpr int kMaxEssentialsColumns = 4;

}  // namespace

int ClampEssentialsColumns(int columns) {
  if (!features::IsSidebarEnabled()) {
    return columns;
  }
  return std::min(columns, kMaxEssentialsColumns);
}

bool AnimatesEssentialsHeight(bool vertical) {
  return vertical && features::IsSidebarEnabled();
}

int GetEmptyEssentialsWidth(const views::SizeBounds& size_bounds) {
  if (!features::IsSidebarEnabled()) {
    return 0;
  }
  return size_bounds.width().value_or(0);
}

int GetEssentialsMinHeight(int min_height, int preferred_height) {
  if (!features::IsSidebarEnabled()) {
    return min_height;
  }
  return std::min(min_height, preferred_height);
}

void PaintEssentialsTile(gfx::Canvas* canvas,
                         const views::View& tab,
                         bool selected,
                         double hover,
                         float corner_radius) {
  const ui::ColorProvider* provider = tab.GetColorProvider();
  const SkColor idle = provider->GetColor(kColorKoanEssentialsTile);
  const SkColor hovered = provider->GetColor(kColorKoanEssentialsTileHovered);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(selected ? provider->GetColor(kColorKoanEssentialsTileSelected)
                          : color_utils::AlphaBlend(hovered, idle,
                                                    static_cast<float>(hover)));
  canvas->DrawRoundRect(gfx::RectF(tab.GetLocalBounds()), corner_radius, flags);
}

}  // namespace koan
