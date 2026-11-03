// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_omnibox_rows.h"

#include <map>
#include <string>
#include <tuple>

#include "base/no_destructor.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_url_bar.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/view.h"

namespace koan {

int OmniboxRowHeight(const views::View* view, int default_height) {
  return ShowsZenCardRows(view) ? kOmniboxRowHeight : default_height;
}

void AdjustOmniboxRowRadii(const views::View* view,
                           gfx::RoundedCornersF& radii) {
  if (ShowsZenCardRows(view)) {
    radii = gfx::RoundedCornersF(kOmniboxRowCornerRadius);
  }
}

gfx::FontList AdjustOmniboxRowFont(const views::View* view,
                                   const gfx::FontList& font) {
  if (!ShowsZenCardRows(view)) {
    return font;
  }
  // A row's views set their font on every text change; Zen's variant of each
  // base font is derived once.
  using Key = std::tuple<int, int, std::string>;
  static base::NoDestructor<std::map<Key, gfx::FontList>> cache;
  const Key key(font.GetFontSize(), static_cast<int>(font.GetFontWeight()),
                font.GetPrimaryFont().GetFontName());
  auto it = cache->find(key);
  if (it == cache->end()) {
    it = cache
             ->emplace(key, font.DeriveWithSizeDelta(kOmniboxRowFontSize -
                                                     font.GetFontSize())
                                .DeriveWithWeight(gfx::Font::Weight::MEDIUM))
             .first;
  }
  return it->second;
}

ui::ColorId AdjustOmniboxRowTextColorId(const views::View* view,
                                        ui::ColorId id) {
  return id == kColorOmniboxText && ShowsZenCardRows(view)
             ? ui::ColorId(kColorKoanOmniboxRowTitle)
             : id;
}

}  // namespace koan
