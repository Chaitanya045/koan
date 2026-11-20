// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_screenshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_batch.h"

namespace koan::cli {

namespace {

// The facade's limit on a clip's side (koan_agent_filters.h, kScreenshot).
constexpr double kMaxClipSide = 16384;

int LongEdgeOf(ShotSize size) {
  switch (size) {
    case ShotSize::kMicro:
      return kMicroLongEdge;
    case ShotSize::kMini:
      return kMiniLongEdge;
    case ShotSize::kNormal:
      return kNormalLongEdge;
    case ShotSize::kFull:
      return kFullLongEdge;
  }
  return kNormalLongEdge;
}

// At least one pixel, as Page.captureScreenshot rounds a clip's side.
int Pixels(double css, double pixels_per_css) {
  return std::max(1, base::ClampRound(css * pixels_per_css));
}

// "2.25", "1", "0.5".
std::string ShortNumber(double value) {
  std::string text = base::StringPrintf("%.2f", value);
  while (text.ends_with('0')) {
    text.pop_back();
  }
  if (text.ends_with('.')) {
    text.pop_back();
  }
  return text;
}

ShotPlanResult PlanError(std::string text) {
  return {std::nullopt, std::move(text)};
}

}  // namespace

std::optional<ShotSize> ParseShotSize(std::string_view name) {
  for (ShotSize size : {ShotSize::kMicro, ShotSize::kMini, ShotSize::kNormal,
                        ShotSize::kFull}) {
    if (name == ShotSizeName(size)) {
      return size;
    }
  }
  return std::nullopt;
}

std::string_view ShotSizeName(ShotSize size) {
  switch (size) {
    case ShotSize::kMicro:
      return "micro";
    case ShotSize::kMini:
      return "mini";
    case ShotSize::kNormal:
      return "normal";
    case ShotSize::kFull:
      return "full";
  }
  return "normal";
}

int EstimateImageTokens(int width, int height) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  const double pixels = static_cast<double>(width) * height;
  return std::min(kMaxImageTokens,
                  std::max(1, base::ClampCeil(pixels / kPixelsPerToken)));
}

std::optional<ClipSpec> ParseClip(std::string_view text, std::string* error) {
  *error =
      "--clip takes @ref, loc:role=<role>[name=\"<name>\"], or x,y,w,h (CSS "
      "pixels from the viewport's top left; w and h above 0)";
  ClipSpec clip;
  if (text.starts_with("@")) {
    if (!ParseRef(text)) {
      return std::nullopt;
    }
    clip.target = std::string(text);
    return clip;
  }
  if (text.starts_with("loc:")) {
    if (!ParseLocator(text, error)) {
      return std::nullopt;
    }
    clip.target = std::string(text);
    return clip;
  }
  std::vector<std::string_view> parts = base::SplitStringPiece(
      text, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() != 4) {
    return std::nullopt;
  }
  std::array<double, 4> numbers;
  for (size_t i = 0; i < 4; ++i) {
    if (!base::StringToDouble(parts[i], &numbers[i]) ||
        !std::isfinite(numbers[i])) {
      return std::nullopt;
    }
  }
  CssRect rect{numbers[0], numbers[1], numbers[2], numbers[3]};
  if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
      rect.width > kMaxClipSide || rect.height > kMaxClipSide) {
    return std::nullopt;
  }
  clip.rect = rect;
  error->clear();
  return clip;
}

std::optional<ViewportMetrics> ReadViewportMetrics(
    const base::DictValue& layout_metrics) {
  const base::DictValue* css = layout_metrics.FindDict("cssVisualViewport");
  if (!css) {
    css = layout_metrics.FindDict("cssLayoutViewport");
  }
  if (!css) {
    return std::nullopt;
  }
  ViewportMetrics metrics;
  metrics.page_x = css->FindDouble("pageX").value_or(0);
  metrics.page_y = css->FindDouble("pageY").value_or(0);
  metrics.width = css->FindDouble("clientWidth").value_or(0);
  metrics.height = css->FindDouble("clientHeight").value_or(0);
  if (!(metrics.width > 0 && metrics.height > 0) ||
      !std::isfinite(metrics.width) || !std::isfinite(metrics.height) ||
      !std::isfinite(metrics.page_x) || !std::isfinite(metrics.page_y)) {
    return std::nullopt;
  }
  const double zoom = css->FindDouble("zoom").value_or(1);
  metrics.zoom = std::isfinite(zoom) && zoom > 0 ? zoom : 1;
  // visualViewport's size is in device pixels (Blink's own), the CSS one's
  // in CSS pixels: their ratio is the zoom times the device scale factor.
  const base::DictValue* device = layout_metrics.FindDict("visualViewport");
  const double device_width =
      device ? device->FindDouble("clientWidth").value_or(0) : 0;
  double scale = device_width / metrics.width / metrics.zoom;
  if (!std::isfinite(scale) || scale <= 0) {
    scale = 1;
  }
  // The ratio of a rounded size: snap to the quarter it means (1, 2, 3, 1.5).
  const double quarter = std::round(scale * 4) / 4;
  if (quarter > 0 && std::abs(scale - quarter) < 0.02) {
    scale = quarter;
  }
  metrics.device_scale = scale;
  return metrics;
}

std::optional<CssRect> QuadsBounds(const base::DictValue& quads_result) {
  const base::ListValue* quads = quads_result.FindList("quads");
  if (!quads) {
    return std::nullopt;
  }
  bool any = false;
  double left = 0, top = 0, right = 0, bottom = 0;
  for (const base::Value& quad : *quads) {
    const base::ListValue* points = quad.GetIfList();
    if (!points || points->size() != 8) {
      continue;
    }
    for (size_t i = 0; i < 8; i += 2) {
      const std::optional<double> x = (*points)[i].GetIfDouble();
      const std::optional<double> y = (*points)[i + 1].GetIfDouble();
      if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
        continue;
      }
      left = any ? std::min(left, *x) : *x;
      right = any ? std::max(right, *x) : *x;
      top = any ? std::min(top, *y) : *y;
      bottom = any ? std::max(bottom, *y) : *y;
      any = true;
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return CssRect{left, top, right - left, bottom - top};
}

ShotPlanResult PlanShot(const ViewportMetrics& viewport,
                        std::optional<ShotSize> size,
                        std::optional<CssRect> region,
                        double margin,
                        std::string_view what,
                        bool may_clip) {
  if (!(viewport.width > 0 && viewport.height > 0)) {
    return PlanError(
        "Koan.pageError: the tab did not say how big its viewport is; retry");
  }
  // Device pixels per CSS pixel.
  const double device = viewport.zoom * viewport.device_scale;
  CssRect area{0, 0, viewport.width, viewport.height};
  if (region) {
    if (!may_clip) {
      return PlanError(
          "Koan.notPermitted: --clip works on the agent's own tabs (Koan "
          "captures a tab of the user's only as a whole, at full size, so "
          "that it never resizes it); take koan screenshot <tab> without "
          "--clip");
    }
    if (!(region->width > 0 && region->height > 0)) {
      return PlanError("Koan.emptyElement: " + std::string(what) +
                       " has no size on the page (hidden, collapsed or "
                       "empty); clip what holds it, or take a screenshot of "
                       "the tab");
    }
    const double left = std::max(0.0, region->x - margin);
    const double top = std::max(0.0, region->y - margin);
    const double right =
        std::min(viewport.width, region->x + region->width + margin);
    const double bottom =
        std::min(viewport.height, region->y + region->height + margin);
    if (right - left < 1 || bottom - top < 1) {
      return PlanError("Koan.offScreen: " + std::string(what) +
                       " is outside the viewport; scroll to it (koan act "
                       "<tab> scroll <pixels>) and try again");
    }
    area = {left, top, right - left, bottom - top};
  }

  ShotPlan plan;
  if (!may_clip) {
    // What the tab shows, as it shows it.
    plan.image_width = Pixels(area.width, device);
    plan.image_height = Pixels(area.height, device);
    plan.css_per_pixel = 1 / device;
    return {plan, ""};
  }
  const ShotSize effective =
      size.value_or(region ? ShotSize::kFull : ShotSize::kNormal);
  // Image pixels per CSS pixel before fitting: never more than the display
  // has (a page zoomed out is not drawn larger than it shows).
  const double natural =
      effective == ShotSize::kFull ? device : std::min(1.0, device);
  const double long_css = std::max(area.width, area.height);
  double fit = std::min(1.0, LongEdgeOf(effective) / (long_css * natural));
  const double pixels =
      area.width * area.height * (natural * fit) * (natural * fit);
  const double max_pixels =
      static_cast<double>(kMaxImageTokens) * kPixelsPerToken;
  if (pixels > max_pixels) {
    fit *= std::sqrt(max_pixels / pixels);
  }
  const double per_css = natural * fit;
  plan.image_width = Pixels(area.width, per_css);
  plan.image_height = Pixels(area.height, per_css);
  plan.css_per_pixel = 1 / per_css;
  plan.scale = per_css / device;
  // The whole viewport at device pixels is the plain capture: no clip, so
  // the tab's view is not resized for it.
  plan.clip = region.has_value() || std::abs(plan.scale - 1) > 1e-6;
  if (plan.clip) {
    plan.x = (viewport.page_x + area.x) * viewport.zoom;
    plan.y = (viewport.page_y + area.y) * viewport.zoom;
    plan.width = area.width * viewport.zoom;
    plan.height = area.height * viewport.zoom;
  } else {
    plan.scale = 1;
  }
  return {plan, ""};
}

std::optional<std::pair<int, int>> PngSize(base::span<const uint8_t> png) {
  static constexpr uint8_t kSignature[] = {0x89, 'P',  'N',  'G',
                                           '\r', '\n', 0x1a, '\n'};
  static constexpr uint8_t kHeader[] = {'I', 'H', 'D', 'R'};
  if (png.size() < 24 ||
      !std::equal(std::begin(kSignature), std::end(kSignature), png.begin()) ||
      !std::equal(std::begin(kHeader), std::end(kHeader), png.begin() + 12)) {
    return std::nullopt;
  }
  auto read = [&](size_t at) {
    return (uint32_t{png[at]} << 24) | (uint32_t{png[at + 1]} << 16) |
           (uint32_t{png[at + 2]} << 8) | uint32_t{png[at + 3]};
  };
  const uint32_t width = read(16);
  const uint32_t height = read(20);
  if (width == 0 || height == 0 || width > 1u << 20 || height > 1u << 20) {
    return std::nullopt;
  }
  return std::make_pair(static_cast<int>(width), static_cast<int>(height));
}

std::string ShotLine(std::string_view path,
                     int width,
                     int height,
                     double css_per_pixel) {
  int tokens = EstimateImageTokens(width, height);
  // An estimate: to the nearest ten above a hundred.
  if (tokens >= 100) {
    tokens = (tokens + 5) / 10 * 10;
  }
  // "×" in UTF-8.
  return base::StringPrintf(
      "saved %.*s (%d\xc3\x97%d, ~%d image tokens, 1 px = %s CSS px)",
      static_cast<int>(path.size()), path.data(), width, height, tokens,
      ShortNumber(css_per_pixel).c_str());
}

base::DictValue ShotJson(std::string_view path,
                         int width,
                         int height,
                         double css_per_pixel) {
  base::DictValue json;
  json.Set("path", path);
  json.Set("width", width);
  json.Set("height", height);
  json.Set("imageTokens", EstimateImageTokens(width, height));
  json.Set("cssPxPerPx", std::round(css_per_pixel * 1000) / 1000);
  return json;
}

}  // namespace koan::cli
