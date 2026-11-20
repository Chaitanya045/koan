// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_screenshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

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

base::DictValue CaptureParams(std::string_view target,
                              std::optional<ShotSize> size,
                              std::optional<CssRect> region,
                              double margin) {
  const ShotSize effective =
      size.value_or(region ? ShotSize::kFull : ShotSize::kNormal);
  base::DictValue params;
  params.Set("targetId", target);
  params.Set("longEdge", LongEdgeOf(effective));
  // Image pixels per CSS pixel at most: CSS pixels, or as sharp as the
  // display draws (the browser caps it there).
  params.Set("scale",
             effective == ShotSize::kFull ? agent::kMaxCaptureScale : 1.0);
  params.Set("format", "png");
  if (region) {
    params.Set("clip", base::DictValue()
                           .Set("x", region->x - margin)
                           .Set("y", region->y - margin)
                           .Set("width", region->width + 2 * margin)
                           .Set("height", region->height + 2 * margin));
  }
  return params;
}

std::string ClipRefusal(std::string_view code, std::string_view what) {
  if (code == "Koan.offScreen") {
    return "Koan.offScreen: " + std::string(what) +
           " is outside the viewport; scroll to it (koan act <tab> scroll "
           "<pixels>) and try again";
  }
  if (code == "Koan.emptyElement") {
    return "Koan.emptyElement: " + std::string(what) +
           " has no size on the page (hidden, collapsed or empty); clip what "
           "holds it, or take a screenshot of the tab";
  }
  return std::string();
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
  int tokens = agent::EstimateImageTokens(width, height);
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
  json.Set("imageTokens", agent::EstimateImageTokens(width, height));
  json.Set("cssPxPerPx", std::round(css_per_pixel * 1000) / 1000);
  return json;
}

}  // namespace koan::cli
