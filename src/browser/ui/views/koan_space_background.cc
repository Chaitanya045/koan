// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_background.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "base/containers/adapters.h"
#include "base/no_destructor.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "koan/browser/ui/views/koan_grain_png.h"
#include "third_party/skia/include/effects/SkGradient.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace koan::theme {

namespace {

// CSS interpolates gradients with transparent stops in premultiplied space,
// so a colour fading to `transparent` keeps its hue.
SkGradient::Interpolation CssInterpolation() {
  SkGradient::Interpolation interpolation;
  interpolation.fInPremul = SkGradient::Interpolation::InPremul::kYes;
  return interpolation;
}

void PaintLayer(gfx::Canvas* canvas,
                const gfx::RectF& box,
                const GradientLayer& layer) {
  if (layer.stops.empty()) {
    return;
  }
  cc::PaintFlags flags;
  if (layer.kind == GradientLayer::Kind::kSolid) {
    flags.setColor(layer.stops[0].color);
    canvas->DrawRect(box, flags);
    return;
  }

  // Skia wants stops in [0, 1]: stretch the gradient line over the stops
  // that lie outside it (CSS extends the end colours beyond the line, as
  // SkTileMode::kClamp does).
  const float low = std::min(0.f, layer.stops.front().position);
  const float high = std::max(1.f, layer.stops.back().position);
  std::vector<SkColor4f> colors;
  std::vector<SkScalar> positions;
  for (const GradientLayer::Stop& stop : layer.stops) {
    colors.push_back(stop.color);
    positions.push_back((stop.position - low) / (high - low));
  }

  if (layer.kind == GradientLayer::Kind::kLinear) {
    // CSS: 0deg points up, angles turn clockwise, and the gradient line is
    // as long as the box seen along that direction.
    const double angle = layer.angle * std::numbers::pi / 180;
    const gfx::Vector2dF direction(std::sin(angle), -std::cos(angle));
    const float length = std::abs(box.width() * std::sin(angle)) +
                         std::abs(box.height() * std::cos(angle));
    const gfx::PointF center = box.CenterPoint();
    const gfx::PointF start =
        center - gfx::ScaleVector2d(direction, length / 2);
    const gfx::Vector2dF line = gfx::ScaleVector2d(direction, length);
    const gfx::PointF from = start + gfx::ScaleVector2d(line, low);
    const gfx::PointF to = start + gfx::ScaleVector2d(line, high);
    const SkPoint points[] = {{from.x(), from.y()}, {to.x(), to.y()}};
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        points, colors.data(), positions.data(),
        static_cast<int>(colors.size()), SkTileMode::kClamp,
        CssInterpolation()));
  } else {
    // radial-gradient(circle at x y): sized to the farthest corner.
    const gfx::PointF center(box.x() + box.width() * layer.center.x(),
                             box.y() + box.height() * layer.center.y());
    float radius = 0;
    for (const gfx::PointF& corner : {box.origin(), box.top_right(),
                                      box.bottom_left(), box.bottom_right()}) {
      radius = std::max(radius, (corner - center).Length());
    }
    if (radius == 0) {
      return;
    }
    flags.setShader(cc::PaintShader::MakeRadialGradient(
        SkPoint::Make(center.x(), center.y()), radius * high, colors.data(),
        positions.data(), static_cast<int>(colors.size()), SkTileMode::kClamp,
        CssInterpolation()));
  }
  canvas->DrawRect(box, flags);
}

}  // namespace

void PaintLayers(gfx::Canvas* canvas,
                 const gfx::RectF& box,
                 const std::vector<GradientLayer>& layers,
                 float opacity) {
  if (opacity <= 0 || box.IsEmpty()) {
    return;
  }
  // Like a CSS element's opacity, fade the layers as one group.
  const bool fade = opacity < 1;
  if (fade) {
    canvas->SaveLayerAlpha(static_cast<uint8_t>(std::round(opacity * 255)),
                           gfx::ToEnclosingRect(box));
  }
  // CSS lists the top layer first.
  for (const GradientLayer& layer : base::Reversed(layers)) {
    PaintLayer(canvas, box, layer);
  }
  if (fade) {
    canvas->Restore();
  }
}

void PaintBase(gfx::Canvas* canvas, const gfx::RectF& box, SkColor color) {
  cc::PaintFlags flags;
  flags.setColor(color);
  canvas->DrawRect(box, flags);
}

void PaintGrain(gfx::Canvas* canvas, const gfx::RectF& box, float opacity) {
  if (opacity <= 0 || box.IsEmpty()) {
    return;
  }
  const gfx::ImageSkia& grain = GrainImage();
  const gfx::Rect rect = gfx::ToEnclosingRect(box);
  cc::PaintFlags flags;
  flags.setAlphaf(std::min(opacity, 1.f));
  canvas->TileImageInt(grain, 0, 0, rect.x(), rect.y(), rect.width(),
                       rect.height(), 1.0f, SkTileMode::kRepeat,
                       SkTileMode::kRepeat, &flags);
}

const gfx::ImageSkia& GrainImage() {
  static const base::NoDestructor<gfx::ImageSkia> image(
      gfx::Image::CreateFrom1xPNGBytes(kGrainPng).AsImageSkia());
  return *image;
}

}  // namespace koan::theme
