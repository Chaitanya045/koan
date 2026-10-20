// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_library_icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "base/functional/bind.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"

namespace koan::library {

namespace {

// Section icons are drawn on a 128-unit grid, glyphs on a 24-unit one.
constexpr float kIconGrid = 128.f;
constexpr float kStroke = 7.1f;

cc::PaintFlags StrokeFlags(SkColor color, float width) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(width);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setColor(color);
  return flags;
}

cc::PaintFlags FillFlags(SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  return flags;
}

// A shape both filled and outlined, like the sprites' bg + border layers.
void FillAndStroke(gfx::Canvas* canvas,
                   const SkRRect& shape,
                   SkColor stroke,
                   SkColor fill) {
  if (SkColorGetA(fill)) {
    canvas->sk_canvas()->drawRRect(shape, FillFlags(fill));
  }
  canvas->sk_canvas()->drawRRect(shape, StrokeFlags(stroke, kStroke));
}

void PaintDownloads(gfx::Canvas* canvas,
                    SkColor stroke,
                    SkColor fill,
                    float progress) {
  const SkRRect circle =
      SkRRect::MakeOval(SkRect::MakeXYWH(16.5f, 16.5f, 95.f, 95.f));
  FillAndStroke(canvas, circle, stroke, fill);
  if (progress >= 0.f) {
    // While anything downloads, a ring fills around the circle (Zen draws
    // it as a conic gradient on the tab).
    cc::PaintFlags ring = StrokeFlags(stroke, 12.f);
    ring.setStrokeCap(cc::PaintFlags::kButt_Cap);
    canvas->sk_canvas()->drawArc(SkRect::MakeXYWH(4.f, 4.f, 120.f, 120.f),
                                 -90.f, 360.f * std::clamp(progress, 0.f, 1.f),
                                 ring);
  }
  const cc::PaintFlags arrow = StrokeFlags(stroke, kStroke);
  canvas->sk_canvas()->drawLine(64.f, 45.f, 64.f, 83.f, arrow);
  canvas->sk_canvas()->drawLine(50.f, 69.f, 64.f, 83.f, arrow);
  canvas->sk_canvas()->drawLine(78.f, 69.f, 64.f, 83.f, arrow);
}

void PaintHistory(gfx::Canvas* canvas, SkColor stroke, SkColor fill) {
  // A box with a lid and a handle.
  SkRRect body;
  const SkVector body_radii[4] = {{0, 0}, {0, 0}, {10.f, 10.f}, {10.f, 10.f}};
  body.setRectRadii(SkRect::MakeLTRB(28.f, 49.f, 100.f, 106.f), body_radii);
  FillAndStroke(canvas, body, stroke, fill);
  FillAndStroke(
      canvas,
      SkRRect::MakeRectXY(SkRect::MakeXYWH(20.5f, 28.f, 87.f, 18.5f), 6.f, 6.f),
      stroke, fill);
  canvas->sk_canvas()->drawLine(50.f, 65.f, 78.f, 65.f,
                                StrokeFlags(stroke, 8.f));
}

void PaintSpaces(gfx::Canvas* canvas, SkColor stroke, SkColor fill) {
  // Two cards: one in front, one tilted behind it and cut away around it.
  const SkRRect front = SkRRect::MakeRectXY(
      SkRect::MakeXYWH(45.5f, 35.f, 63.f, 82.f), 10.5f, 10.5f);
  cc::PaintCanvas* sk_canvas = canvas->sk_canvas();
  sk_canvas->save();
  sk_canvas->clipRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(42.f, 31.f, 70.f, 89.5f), 14.f,
                          14.f),
      SkClipOp::kDifference, true);
  sk_canvas->translate(98.5f, 0.5f);
  sk_canvas->rotate(-17.5f);
  sk_canvas->translate(-98.5f, -0.5f);
  FillAndStroke(canvas,
                SkRRect::MakeRectXY(SkRect::MakeXYWH(3.5f, 3.5f, 63.f, 82.f),
                                    10.5f, 10.5f),
                stroke, fill);
  sk_canvas->restore();
  FillAndStroke(canvas, front, stroke, fill);
}

enum class Glyph { kLibrary, kMove };

class GlyphSource : public gfx::CanvasImageSource {
 public:
  GlyphSource(Glyph glyph, int size, SkColor color)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        glyph_(glyph),
        color_(color) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    const float scale = size().width() / 24.f;
    cc::PaintCanvas* sk_canvas = canvas->sk_canvas();
    sk_canvas->save();
    sk_canvas->scale(scale, scale);
    if (glyph_ == Glyph::kLibrary) {
      DrawLibrary(sk_canvas);
    } else {
      DrawMove(sk_canvas);
    }
    sk_canvas->restore();
  }

 private:
  void DrawLibrary(cc::PaintCanvas* canvas) {
    // The box, open at the top, with a slot cut into its front.
    SkRRect box;
    const SkVector box_radii[4] = {
        {1.f, 1.f}, {1.f, 1.f}, {3.3f, 3.3f}, {3.3f, 3.3f}};
    box.setRectRadii(SkRect::MakeLTRB(3.2f, 8.5f, 20.8f, 16.7f), box_radii);
    SkPathBuilder builder(SkPathFillType::kEvenOdd);
    builder.addRRect(box);
    builder.addRRect(SkRRect::MakeRectXY(
        SkRect::MakeLTRB(6.6f, 9.7f, 13.4f, 11.35f), 0.83f, 0.83f));
    canvas->drawPath(builder.detach(), FillFlags(color_));

    // A star rising out of it, a dot and a play triangle.
    canvas->save();
    canvas->clipRect(SkRect::MakeLTRB(0.f, 0.f, 24.f, 7.6f));
    std::array<SkPoint, 10> star;
    for (size_t i = 0; i < star.size(); ++i) {
      const float radius = i % 2 ? 1.25f : 2.9f;
      const float angle =
          -std::numbers::pi_v<float> / 2 + i * std::numbers::pi_v<float> / 5;
      star[i] = {6.f + radius * std::cos(angle),
                 6.4f + radius * std::sin(angle)};
    }
    SkPathBuilder star_path;
    star_path.addPolygon(star, /*close=*/true);
    canvas->drawPath(star_path.detach(), FillFlags(color_));
    canvas->restore();
    canvas->drawOval(SkRect::MakeXYWH(9.f, 1.2f, 2.f, 2.f), FillFlags(color_));
    SkPathBuilder triangle;
    triangle.moveTo(11.9f, 3.4f).lineTo(15.4f, 5.2f).lineTo(11.9f, 7.f).close();
    canvas->drawPath(triangle.detach(), FillFlags(color_));
  }

  void DrawMove(cc::PaintCanvas* canvas) {
    const cc::PaintFlags flags = StrokeFlags(color_, 1.8f);
    canvas->drawLine(12.f, 3.f, 12.f, 21.f, flags);
    canvas->drawLine(3.f, 12.f, 21.f, 12.f, flags);
    // Arrowheads at each end.
    const std::array<std::array<float, 6>, 4> heads = {{
        {9.f, 6.f, 12.f, 3.f, 15.f, 6.f},
        {9.f, 18.f, 12.f, 21.f, 15.f, 18.f},
        {6.f, 9.f, 3.f, 12.f, 6.f, 15.f},
        {18.f, 9.f, 21.f, 12.f, 18.f, 15.f},
    }};
    for (const auto& head : heads) {
      SkPathBuilder path;
      path.moveTo(head[0], head[1])
          .lineTo(head[2], head[3])
          .lineTo(head[4], head[5]);
      canvas->drawPath(path.detach(), flags);
    }
  }

  const Glyph glyph_;
  const SkColor color_;
};

ui::ImageModel GlyphModel(Glyph glyph, int size) {
  return ui::ImageModel::FromImageGenerator(
      base::BindRepeating(
          [](Glyph glyph, int size, const ui::ColorProvider* provider) {
            return gfx::CanvasImageSource::MakeImageSkia<GlyphSource>(
                glyph, size, TextColor(provider));
          },
          glyph, size),
      gfx::Size(size, size));
}

}  // namespace

void PaintSectionIcon(gfx::Canvas* canvas,
                      const gfx::RectF& bounds,
                      LibrarySection section,
                      SkColor stroke,
                      SkColor fill,
                      float progress) {
  cc::PaintCanvas* sk_canvas = canvas->sk_canvas();
  sk_canvas->save();
  sk_canvas->translate(bounds.x(), bounds.y());
  const float scale = bounds.width() / kIconGrid;
  sk_canvas->scale(scale, scale);
  switch (section) {
    case LibrarySection::kDownloads:
      PaintDownloads(canvas, stroke, fill, progress);
      break;
    case LibrarySection::kSpaces:
      PaintSpaces(canvas, stroke, fill);
      break;
    case LibrarySection::kHistory:
      PaintHistory(canvas, stroke, fill);
      break;
  }
  sk_canvas->restore();
}

SkColor SectionIconStroke(const ui::ColorProvider* provider) {
  return provider->GetColor(ui::kColorSysPrimary);
}

SkColor SectionIconFill(const ui::ColorProvider* provider) {
  // color-mix(in srgb, stroke, light-dark(white, black) 60%).
  const bool dark = color_utils::IsDark(provider->GetColor(kColorToolbar));
  return color_utils::AlphaBlend(dark ? SK_ColorBLACK : SK_ColorWHITE,
                                 SectionIconStroke(provider), 0.6f);
}

ui::ImageModel LibraryGlyph(int size) {
  return GlyphModel(Glyph::kLibrary, size);
}

ui::ImageModel MoveGlyph(int size) {
  return GlyphModel(Glyph::kMove, size);
}

ui::ImageModel ColorSwatch(ui::ColorId color, int size) {
  return ui::ImageModel::FromImageGenerator(
      base::BindRepeating(
          [](ui::ColorId color, int size, const ui::ColorProvider* provider) {
            gfx::Canvas canvas(gfx::Size(size, size), 1.f, /*is_opaque=*/false);
            canvas.DrawCircle(gfx::PointF(size / 2.f, size / 2.f), size / 2.f,
                              FillFlags(provider->GetColor(color)));
            return gfx::ImageSkia::CreateFrom1xBitmap(canvas.GetBitmap());
          },
          color, size),
      gfx::Size(size, size));
}

}  // namespace koan::library
