// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_library_ui.h"

#include <memory>
#include <optional>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"

namespace koan::library {

namespace {

class StateBackground : public views::Background {
 public:
  StateBackground(std::optional<ui::ColorId> normal,
                  std::optional<ui::ColorId> hovered,
                  int radius)
      : normal_(normal), hovered_(hovered), radius_(radius) {}

  // views::Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    std::optional<ui::ColorId> color = normal_;
    if (const views::Button* button = views::Button::AsButton(view)) {
      const views::Button::ButtonState state = button->GetState();
      if (state == views::Button::STATE_HOVERED ||
          state == views::Button::STATE_PRESSED) {
        color = hovered_;
      }
    }
    if (!color || !view->GetColorProvider()) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(view->GetColorProvider()->GetColor(*color));
    canvas->DrawRoundRect(gfx::RectF(view->GetLocalBounds()), radius_, flags);
  }

 private:
  const std::optional<ui::ColorId> normal_;
  const std::optional<ui::ColorId> hovered_;
  const int radius_;
};

}  // namespace

gfx::FontList Font(int pixel_size, gfx::Font::Weight weight) {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(pixel_size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

std::unique_ptr<views::Background> CreateStateBackground(
    std::optional<ui::ColorId> normal,
    std::optional<ui::ColorId> hovered,
    int radius) {
  return std::make_unique<StateBackground>(normal, hovered, radius);
}

LibraryLabel::LibraryLabel(std::u16string text,
                           int pixel_size,
                           gfx::Font::Weight weight,
                           ui::ColorId color)
    : views::Label(std::move(text)) {
  SetFontList(Font(pixel_size, weight));
  SetEnabledColor(color);
  SetAutoColorReadabilityEnabled(false);
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetElideBehavior(gfx::ELIDE_TAIL);
  // The library draws on translucent layers, where subpixel text renders
  // wrongly.
  SetSubpixelRenderingEnabled(false);
}

BEGIN_METADATA(LibraryLabel)
END_METADATA

std::unique_ptr<views::ImageButton> CreateIconButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string tooltip,
    int icon_size,
    int padding,
    int radius) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(
                            icon, kColorKoanLibraryForeground, icon_size));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetPreferredSize(
      gfx::Size(icon_size + 2 * padding, icon_size + 2 * padding));
  button->SetBackground(CreateStateBackground(
      std::nullopt, kColorKoanLibraryPillHovered, radius));
  button->SetTooltipText(tooltip);
  button->GetViewAccessibility().SetName(tooltip);
  return button;
}

}  // namespace koan::library
