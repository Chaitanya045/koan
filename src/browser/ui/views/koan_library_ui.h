// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_UI_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_UI_H_

#include <memory>
#include <optional>
#include <string>

#include "koan/browser/ui/color/koan_color_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"

namespace gfx {
struct VectorIcon;
}

namespace koan {

// The Library's sections, in Zen's order (Zen also has Media and Boosts).
enum class LibrarySection { kDownloads, kSpaces, kHistory };

}  // namespace koan

// Sizes of the Library (Zen's zen-library.css), in one place, and building
// blocks shared by its sections. Colours are in
// koan/browser/ui/color/koan_color_id.h.
namespace koan::library {

// The side column with the section tabs, and the width of a list section
// (Zen: max-width calc(418px - 110px)); together the library's least width.
inline constexpr int kSideWidth = 110;
inline constexpr int kSectionWidth = 308;
inline constexpr int kMinWidth = kSideWidth + kSectionWidth;

// Corner radii: pills (search box, Filter, chips and the row buttons;
// --zen-library-radius), rows, side tabs, Space cards and the small things
// (header and footer buttons, a Space's icon, picture previews).
inline constexpr int kPillRadius = 12;
inline constexpr int kRowRadius = 16;
inline constexpr int kTabRadius = 10;
inline constexpr int kSpaceCardRadius = 14;
inline constexpr int kSmallRadius = 6;

// Row action buttons: an 18px icon with 8px around it.
inline constexpr int kRowButtonIconSize = 18;
inline constexpr int kRowButtonPadding = 8;
// Header and footer buttons: a 16px icon with 6px around it.
inline constexpr int kSmallButtonIconSize = 16;
inline constexpr int kSmallButtonPadding = 6;

// The Library button in the sidebar's footer, the size of the workspace
// buttons beside it.
inline constexpr int kLibraryButtonSize = 24;
inline constexpr int kLibraryButtonIconSize = 18;

// How much of a Space's colour tints its card.
inline constexpr float kSpaceCardTint = 0.22f;
// Rows whose file is gone, and chips that don't apply, are faded.
inline constexpr float kMissingOpacity = 0.6f;
inline constexpr float kUnavailableOpacity = 0.4f;

// The default UI font at `pixel_size`.
gfx::FontList Font(int pixel_size,
                   gfx::Font::Weight weight = gfx::Font::Weight::NORMAL);

// A rounded rect in `normal` (none if unset); on a button, in `hovered`
// while it is hovered or pressed.
std::unique_ptr<views::Background> CreateStateBackground(
    std::optional<ui::ColorId> normal,
    std::optional<ui::ColorId> hovered,
    int radius);

// A label in one of the Library's colours.
class LibraryLabel : public views::Label {
  METADATA_HEADER(LibraryLabel, views::Label)

 public:
  LibraryLabel(std::u16string text,
               int pixel_size,
               gfx::Font::Weight weight,
               ui::ColorId color = kColorKoanLibraryForeground);
};

// An icon button as in Zen's rows and footers: an icon of `icon_size` in the
// text colour, with `padding` around it and a pill behind it while hovered.
std::unique_ptr<views::ImageButton> CreateIconButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string tooltip,
    int icon_size = kSmallButtonIconSize,
    int padding = kSmallButtonPadding,
    int radius = kSmallRadius);

}  // namespace koan::library

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_UI_H_
