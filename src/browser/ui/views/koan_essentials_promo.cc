// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials_promo.h"

#include <algorithm>
#include <cmath>

#include "cc/paint/paint_flags.h"
#include "cc/paint/path_effect.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_space_drag_logic.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace koan {

namespace {

constexpr char16_t kTitle[] = u"Add to Essentials";
constexpr char16_t kSublabel[] = u"Keep your favorite tabs just a click away";

// zen-essentials-promo.css, at 16px to the rem: margin 2px, padding 1.1rem
// top and bottom, an 18px heart 4px above the title, the labels at most 80%
// of the width, the sublabel "smaller" (Firefox's 5/6) at 80%.
constexpr int kMargin = 2;
constexpr int kPaddingY = 18;
constexpr int kIconSize = 18;
constexpr int kIconGap = 4;
constexpr float kLabelWidth = 0.8f;
constexpr float kSublabelScale = 5.f / 6.f;
constexpr float kSublabelOpacity = 0.8f;
// --border-radius-medium.
constexpr int kRadius = 8;

gfx::FontList TitleFont() {
  return gfx::FontList().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD);
}

gfx::FontList SublabelFont() {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(
      std::lround(base.GetFontSize() * kSublabelScale) - base.GetFontSize());
}

}  // namespace

KoanEssentialsPromoView::KoanEssentialsPromoView() {
  // The drag decides what it does; the labels take no events (Zen's
  // pointer-events: none on its children).
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(std::u16string(kTitle));
  GetViewAccessibility().SetDescription(std::u16string(kSublabel));
}

KoanEssentialsPromoView::~KoanEssentialsPromoView() = default;

void KoanEssentialsPromoView::SetDragOver(bool drag_over) {
  if (drag_over_ == drag_over) {
    return;
  }
  drag_over_ = drag_over;
  SchedulePaint();
}

gfx::Size KoanEssentialsPromoView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int height = 2 * kMargin + 2 * kPaddingY + kIconSize + kIconGap +
                     TitleFont().GetHeight() + SublabelFont().GetHeight();
  return gfx::Size(0, height);
}

void KoanEssentialsPromoView::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  gfx::RectF box(GetLocalBounds());
  box.Inset(kMargin);
  if (box.IsEmpty()) {
    return;
  }
  const float radius = look::StyleRadius(kRadius, look::Step::kMd);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  // box-shadow: 0 0 1px 1px rgba(0, 0, 0, 0.1).
  gfx::RectF shadow = box;
  shadow.Outset(1);
  flags.setColor(colors->GetColor(kColorKoanEssentialsPromoShadow));
  canvas->DrawRoundRect(shadow, radius + 1, flags);
  // The primary colour at 60%, 40% with the tab over it.
  flags.setColor(colors->GetColor(drag_over_ ? kColorKoanEssentialsPromoDragOver
                                             : kColorKoanEssentialsPromo));
  canvas->DrawRoundRect(box, radius, flags);
  // border: 1px solid the primary colour; without the tab over it also
  // outline: 1px dashed currentColor, just outside.
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  gfx::RectF border = box;
  border.Inset(0.5f);
  flags.setColor(colors->GetColor(kColorKoanEssentialsPromoBorder));
  canvas->DrawRoundRect(border, radius - 0.5f, flags);
  if (!drag_over_) {
    gfx::RectF outline = box;
    outline.Outset(0.5f);
    flags.setColor(colors->GetColor(kColorKoanEssentialsPromoForeground));
    const float dashes[] = {3, 3};
    flags.setPathEffect(cc::PathEffect::MakeDash(dashes, 2, 0));
    canvas->DrawRoundRect(outline, radius + 0.5f, flags);
  }

  // The heart and the two labels, centred.
  const SkColor text = colors->GetColor(kColorKoanEssentialsPromoForeground);
  int y = static_cast<int>(box.y()) + kPaddingY;
  const gfx::ImageSkia heart =
      gfx::CreateVectorIcon(kKoanHeartCircleFillIcon, kIconSize, text);
  canvas->DrawImageInt(heart, width() / 2 - kIconSize / 2, y);
  y += kIconSize + kIconGap;
  const int label_width = static_cast<int>(box.width() * kLabelWidth);
  const int label_x = width() / 2 - label_width / 2;
  const gfx::FontList title_font = TitleFont();
  canvas->DrawStringRectWithFlags(
      kTitle, title_font, text,
      gfx::Rect(label_x, y, label_width, title_font.GetHeight()),
      gfx::Canvas::TEXT_ALIGN_CENTER);
  y += title_font.GetHeight();
  const gfx::FontList sublabel_font = SublabelFont();
  canvas->DrawStringRectWithFlags(
      kSublabel, sublabel_font,
      SkColorSetA(text, SkColorGetA(text) * kSublabelOpacity),
      gfx::Rect(label_x, y, label_width, sublabel_font.GetHeight()),
      gfx::Canvas::TEXT_ALIGN_CENTER);
}

BEGIN_METADATA(KoanEssentialsPromoView)
ADD_READONLY_PROPERTY_METADATA(bool, DragOver)
END_METADATA

KoanTabDropIndicatorView::KoanTabDropIndicatorView(bool background)
    : style_(background ? Style::kBackground : Style::kAcross) {
  if (!background) {
    // Above the tiles and rows, which scroll on layers of their own.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  SetVisible(false);
}

KoanTabDropIndicatorView::~KoanTabDropIndicatorView() = default;

void KoanTabDropIndicatorView::SetSlot(const gfx::Rect& slot, Style style) {
  if (slot_ == slot && style_ == style) {
    return;
  }
  slot_ = slot;
  style_ = style;
  // (Zen moves it with no transition: it snaps to the new place.)
  SchedulePaint();
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void KoanTabDropIndicatorView::OnPaint(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  const gfx::RectF bounds(GetLocalBounds());
  if (style_ == Style::kBackground) {
    // The primary colour, square, behind the row.
    flags.setColor(colors->GetColor(kColorKoanTabDragOverBackground));
    canvas->DrawRect(bounds, flags);
    return;
  }
  if (style_ == Style::kSplitTab) {
    // --border-radius-medium.
    flags.setColor(colors->GetColor(kColorKoanSplitFakeTab));
    canvas->DrawRoundRect(bounds, look::StyleRadius(kRadius, look::Step::kMd),
                          flags);
    return;
  }
  flags.setColor(colors->GetColor(kColorKoanTabDropIndicator));
  if (style_ == Style::kUpright) {
    // Zen's background with border-radius 5px (a pill this thin).
    canvas->DrawRoundRect(bounds, std::min(bounds.width(), bounds.height()) / 2,
                          flags);
    return;
  }
  // The line after the ring's room, the ring (border 2px, 4px inside) centred
  // 2px before the line's start, on the line's middle.
  const gfx::RectF line(kDropLineRingRoom, kDropLineTop,
                        bounds.width() - kDropLineRingRoom, kThickness);
  canvas->DrawRoundRect(line, kThickness / 2.f, flags);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(kThickness);
  canvas->DrawCircle(gfx::PointF(kDropLineRingRoom - kThickness,
                                 kDropLineTop + kThickness / 2.f),
                     (kDropLineRingSize - kThickness) / 2.f, flags);
}

BEGIN_METADATA(KoanTabDropIndicatorView)
END_METADATA

}  // namespace koan
