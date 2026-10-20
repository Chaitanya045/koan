// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_UI_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_UI_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}

namespace ui {
class ColorProvider;
}

namespace koan {

// The Library's sections, in Zen's order (Zen also has Media and Boosts).
enum class LibrarySection { kDownloads, kSpaces, kHistory };

}  // namespace koan

// Building blocks shared by the Library's sections, sized like Zen's
// zen-library.css.
namespace koan::library {

// The side column with the section tabs, and the width of a list section
// (Zen: max-width calc(418px - 110px)); together the library's least width.
inline constexpr int kSideWidth = 110;
inline constexpr int kSectionWidth = 308;
inline constexpr int kMinWidth = kSideWidth + kSectionWidth;
// --zen-library-radius.
inline constexpr int kRadius = 12;
// Zen's pills: color-mix(currentColor 7%), 12% when hovered.
inline constexpr float kPillAlpha = 0.07f;
inline constexpr float kPillHoverAlpha = 0.12f;

// The colour the library draws in (Zen's currentColor): the sidebar's text.
ui::ColorId TextColorId();
// The text colour at `alpha`, as Zen's color-mix(currentColor N%).
SkColor TextColor(const ui::ColorProvider* provider, float alpha = 1.f);

// The default UI font at `pixel_size`.
gfx::FontList Font(int pixel_size,
                   gfx::Font::Weight weight = gfx::Font::Weight::NORMAL);

// A rounded rect in the text colour at `alpha`. On a button, `hover_alpha`
// while it is hovered or pressed (nothing is drawn at alpha 0).
std::unique_ptr<views::Background> CreateTintBackground(float alpha,
                                                        int radius,
                                                        float hover_alpha);

// A label in the text colour at `alpha`.
class TintLabel : public views::Label {
  METADATA_HEADER(TintLabel, views::Label)

 public:
  TintLabel(std::u16string text,
            int pixel_size,
            gfx::Font::Weight weight,
            float alpha = 1.f);

  // views::Label:
  void OnThemeChanged() override;

 private:
  const float alpha_;
};

// An icon button as in Zen's rows and footers: an icon of `icon_size` in the
// text colour, with `padding` around it and a tinted rounded rect on hover.
std::unique_ptr<views::ImageButton> CreateIconButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string tooltip,
    int icon_size = 16,
    int padding = 6,
    int radius = 6);

}  // namespace koan::library

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_UI_H_
