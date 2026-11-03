// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_omnibox_rows.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace koan {

int OmniboxRowHeight(int default_height) {
  return look::IsShadcnStyle() ? default_height : kOmniboxRowHeight;
}

void AdjustOmniboxRowRadii(gfx::RoundedCornersF& radii) {
  if (!look::IsShadcnStyle()) {
    radii = gfx::RoundedCornersF(kOmniboxRowCornerRadius);
  }
}

gfx::FontList AdjustOmniboxRowFont(const gfx::FontList& font) {
  if (look::IsShadcnStyle()) {
    return font;
  }
  return font.DeriveWithSizeDelta(kOmniboxRowFontSize - font.GetFontSize())
      .DeriveWithWeight(gfx::Font::Weight::MEDIUM);
}

ui::ColorId AdjustOmniboxRowTextColorId(ui::ColorId id) {
  return !look::IsShadcnStyle() && id == kColorOmniboxText
             ? kColorKoanOmniboxRowTitle
             : id;
}

}  // namespace koan
