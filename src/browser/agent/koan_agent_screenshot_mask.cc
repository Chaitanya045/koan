// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_screenshot_mask.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace koan::agent {

namespace {

double Number(const base::DictValue* dict, std::string_view key) {
  return dict ? dict->FindDouble(key).value_or(0) : 0;
}

}  // namespace

std::optional<ScreenshotGeometry> GeometryFor(
    const base::DictValue& screenshot_params,
    const base::DictValue& layout_metrics) {
  ScreenshotGeometry geometry;
  if (const base::DictValue* clip = screenshot_params.FindDict("clip")) {
    geometry.x = Number(clip, "x");
    geometry.y = Number(clip, "y");
    geometry.width = Number(clip, "width");
    geometry.height = Number(clip, "height");
  } else if (screenshot_params.FindBool("captureBeyondViewport")
                 .value_or(false)) {
    const base::DictValue* content = layout_metrics.FindDict("cssContentSize");
    geometry.width = Number(content, "width");
    geometry.height = Number(content, "height");
  } else {
    const base::DictValue* viewport =
        layout_metrics.FindDict("cssVisualViewport");
    geometry.x = Number(viewport, "pageX");
    geometry.y = Number(viewport, "pageY");
    geometry.width = Number(viewport, "clientWidth");
    geometry.height = Number(viewport, "clientHeight");
  }
  if (!(geometry.width > 0) || !(geometry.height > 0)) {
    return std::nullopt;
  }
  return geometry;
}

CssRect ViewportOffset(const base::DictValue& layout_metrics) {
  const base::DictValue* viewport =
      layout_metrics.FindDict("cssVisualViewport");
  return CssRect{Number(viewport, "pageX"), Number(viewport, "pageY"), 0, 0};
}

std::optional<CssRect> BoxFromQuads(const base::DictValue& quads_answer) {
  const base::ListValue* quads = quads_answer.FindList("quads");
  if (!quads) {
    return std::nullopt;
  }
  double left = std::numeric_limits<double>::infinity();
  double top = left;
  double right = -left;
  double bottom = -left;
  for (const base::Value& quad : *quads) {
    if (!quad.is_list() || quad.GetList().size() != 8) {
      continue;
    }
    const base::ListValue& points = quad.GetList();
    for (size_t i = 0; i < 8; i += 2) {
      if (!points[i].is_double() && !points[i].is_int()) {
        continue;
      }
      const double x = points[i].GetDouble();
      const double y = points[i + 1].is_double() || points[i + 1].is_int()
                           ? points[i + 1].GetDouble()
                           : 0;
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  if (!(right > left) || !(bottom > top)) {
    return std::nullopt;
  }
  return CssRect{left, top, right - left, bottom - top};
}

ScreenshotMask MaskFor(base::span<const CssRect> boxes,
                       const CssRect& viewport,
                       const ScreenshotGeometry& geometry) {
  ScreenshotMask mask;
  mask.css_width = geometry.width;
  mask.css_height = geometry.height;
  for (const CssRect& box : boxes) {
    mask.boxes.push_back(CssRect{box.x + viewport.x - geometry.x,
                                 box.y + viewport.y - geometry.y, box.width,
                                 box.height});
  }
  return mask;
}

std::vector<PixelRect> MaskRects(const ScreenshotMask& mask,
                                 int image_width,
                                 int image_height) {
  std::vector<PixelRect> rects;
  if (image_width <= 0 || image_height <= 0 || !(mask.css_width > 0) ||
      !(mask.css_height > 0)) {
    return rects;
  }
  const double scale_x = image_width / mask.css_width;
  const double scale_y = image_height / mask.css_height;
  for (const CssRect& box : mask.boxes) {
    const double left = std::floor((box.x - kMaskPadding) * scale_x);
    const double top = std::floor((box.y - kMaskPadding) * scale_y);
    const double right =
        std::ceil((box.x + box.width + kMaskPadding) * scale_x);
    const double bottom =
        std::ceil((box.y + box.height + kMaskPadding) * scale_y);
    const int x0 = static_cast<int>(std::clamp<double>(left, 0, image_width));
    const int y0 = static_cast<int>(std::clamp<double>(top, 0, image_height));
    const int x1 = static_cast<int>(std::clamp<double>(right, 0, image_width));
    const int y1 =
        static_cast<int>(std::clamp<double>(bottom, 0, image_height));
    if (x1 > x0 && y1 > y0) {
      rects.push_back(PixelRect{x0, y0, x1 - x0, y1 - y0});
    }
  }
  return rects;
}

}  // namespace koan::agent
