// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_capture.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/numerics/safe_conversions.h"

namespace koan::agent {

namespace {

// The limits on a clip (as the kScreenshot filter's for CDP's clip).
constexpr double kMaxClipOrigin = 1e6;
constexpr double kMaxClipSide = 16384;

base::unexpected<CaptureError> Error(std::string code, std::string message) {
  return base::unexpected(CaptureError{std::move(code), std::move(message)});
}

// A finite number in [min, max], or nothing.
std::optional<double> Number(const base::DictValue& dict,
                             std::string_view key,
                             double min,
                             double max) {
  const std::optional<double> number = dict.FindDouble(key);
  if (!number || !std::isfinite(*number) || *number < min || *number > max) {
    return std::nullopt;
  }
  return number;
}

// At least one pixel.
int Pixels(double css, double pixels_per_css) {
  return std::max(1, base::ClampRound(css * pixels_per_css));
}

}  // namespace

int EstimateImageTokens(int width, int height) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  const double pixels = static_cast<double>(width) * height;
  return std::min(kMaxImageTokens,
                  std::max(1, base::ClampCeil(pixels / kPixelsPerToken)));
}

std::string_view ImageFormatName(ImageFormat format) {
  switch (format) {
    case ImageFormat::kPng:
      return "png";
    case ImageFormat::kJpeg:
      return "jpeg";
    case ImageFormat::kWebp:
      return "webp";
  }
  return "png";
}

base::expected<CaptureRequest, CaptureError> ParseCaptureRequest(
    const base::DictValue& params) {
  CaptureRequest request;
  if (params.contains("longEdge")) {
    const std::optional<double> edge =
        Number(params, "longEdge", kMinImageLongEdge, kMaxImageLongEdge);
    if (!edge || *edge != std::floor(*edge)) {
      return Error("Koan.invalidParams",
                   "longEdge must be an integer 16 to 2576 (image pixels)");
    }
    request.long_edge = static_cast<int>(*edge);
  }
  if (params.contains("scale")) {
    const std::optional<double> scale =
        Number(params, "scale", 0, kMaxCaptureScale);
    if (!scale || *scale <= 0) {
      return Error("Koan.invalidParams",
                   "scale must be a number in (0, 4]: image pixels per CSS "
                   "pixel (1: CSS pixels; 4: as sharp as the display)");
    }
    request.scale = *scale;
  }
  if (const base::Value* clip = params.Find("clip")) {
    const base::DictValue* dict = clip->GetIfDict();
    const auto x = dict ? Number(*dict, "x", -kMaxClipOrigin, kMaxClipOrigin)
                        : std::nullopt;
    const auto y = dict ? Number(*dict, "y", -kMaxClipOrigin, kMaxClipOrigin)
                        : std::nullopt;
    // CDP's name for the sides, and the short one the CLI's x,y,w,h uses.
    auto side = [&](std::string_view name, std::string_view short_name) {
      if (!dict) {
        return std::optional<double>();
      }
      return dict->contains(name) ? Number(*dict, name, 0, kMaxClipSide)
                                  : Number(*dict, short_name, 0, kMaxClipSide);
    };
    const auto width = side("width", "w");
    const auto height = side("height", "h");
    if (!x || !y || !width || !height) {
      return Error("Koan.invalidParams",
                   "clip must be {x, y, width, height} in CSS pixels from the "
                   "viewport's top left: finite numbers, x and y within 1e6, "
                   "width and height 0 to 16384");
    }
    request.clip = ViewportRect{*x, *y, *width, *height};
  }
  if (const base::Value* format = params.Find("format")) {
    const std::string* name = format->GetIfString();
    if (name && *name == "png") {
      request.format = ImageFormat::kPng;
    } else if (name && *name == "jpeg") {
      request.format = ImageFormat::kJpeg;
    } else if (name && *name == "webp") {
      request.format = ImageFormat::kWebp;
    } else {
      return Error("Koan.invalidParams", "format must be png, jpeg or webp");
    }
  }
  if (params.contains("quality")) {
    const std::optional<double> quality = Number(params, "quality", 0, 100);
    if (!quality || *quality != std::floor(*quality)) {
      return Error("Koan.invalidParams", "quality must be an integer 0 to 100");
    }
    request.quality = static_cast<int>(*quality);
  }
  return request;
}

std::optional<PageViewport> ReadPageViewport(
    const base::DictValue& layout_metrics) {
  const base::DictValue* css = layout_metrics.FindDict("cssVisualViewport");
  if (!css) {
    return std::nullopt;
  }
  PageViewport viewport;
  viewport.page_x = css->FindDouble("pageX").value_or(0);
  viewport.page_y = css->FindDouble("pageY").value_or(0);
  viewport.width = css->FindDouble("clientWidth").value_or(0);
  viewport.height = css->FindDouble("clientHeight").value_or(0);
  if (!(viewport.width > 0 && viewport.height > 0) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      !std::isfinite(viewport.page_x) || !std::isfinite(viewport.page_y)) {
    return std::nullopt;
  }
  // Blink's own: `zoom` is the browser's zoom (CSS px -> DIPs), `scale` the
  // pinch zoom (inspector_page_agent.cc, getLayoutMetrics).
  const double zoom = css->FindDouble("zoom").value_or(1);
  viewport.zoom = std::isfinite(zoom) && zoom > 0 ? zoom : 1;
  const double pinch = css->FindDouble("scale").value_or(1);
  viewport.pinch = std::isfinite(pinch) && pinch > 0 ? pinch : 1;
  return viewport;
}

base::expected<CapturePlan, CaptureError> PlanCapture(
    const PageViewport& viewport,
    const SurfaceGeometry& surface,
    const CaptureRequest& request) {
  if (surface.width <= 0 || surface.height <= 0 ||
      !(surface.device_scale > 0) || !std::isfinite(surface.device_scale)) {
    return Error("Koan.noSurface",
                 "the tab has nothing drawn to capture (no size); retry once "
                 "it shows a page");
  }
  // DIPs and device pixels per CSS pixel.
  const double css_to_dip = viewport.zoom * viewport.pinch;
  const double device = css_to_dip * surface.device_scale;

  CapturePlan plan;
  plan.source = {0, 0, surface.width, surface.height};
  plan.whole = true;
  if (request.clip) {
    const ViewportRect& clip = *request.clip;
    if (!(clip.width > 0 && clip.height > 0)) {
      return Error("Koan.emptyElement",
                   "the clip has no size (a hidden, collapsed or empty "
                   "element); clip what holds it, or capture the tab");
    }
    // Less than a CSS pixel of it in the view: nothing of it shows.
    const double view_width = surface.width / css_to_dip;
    const double view_height = surface.height / css_to_dip;
    if (std::min(view_width, clip.x + clip.width) - std::max(0.0, clip.x) < 1 ||
        std::min(view_height, clip.y + clip.height) - std::max(0.0, clip.y) <
            1) {
      return Error("Koan.offScreen",
                   "the clip is outside the viewport; scroll to it (koan act "
                   "<tab> scroll <pixels>) and try again");
    }
    // Cut to the view, in whole DIPs outwards (CopyFromSurface's rectangle
    // is in DIPs).
    const int left = std::max(0, base::ClampFloor(clip.x * css_to_dip));
    const int top = std::max(0, base::ClampFloor(clip.y * css_to_dip));
    const int right = std::min(
        surface.width, base::ClampCeil((clip.x + clip.width) * css_to_dip));
    const int bottom = std::min(
        surface.height, base::ClampCeil((clip.y + clip.height) * css_to_dip));
    plan.source = {left, top, right - left, bottom - top};
    plan.whole =
        plan.source == CaptureRect{0, 0, surface.width, surface.height};
  }
  plan.shows = {plan.source.x / css_to_dip, plan.source.y / css_to_dip,
                plan.source.width / css_to_dip,
                plan.source.height / css_to_dip};

  // Image pixels per CSS pixel: as asked, never more than the display draws
  // (a page zoomed out is not drawn larger than it shows), fitted to the
  // long edge and then to the token cap.
  const double natural = std::min(request.scale, device);
  const double long_css = std::max(plan.shows.width, plan.shows.height);
  double fit = std::min(1.0, request.long_edge / (long_css * natural));
  const double pixels =
      plan.shows.width * plan.shows.height * (natural * fit) * (natural * fit);
  const double max_pixels =
      static_cast<double>(kMaxImageTokens) * kPixelsPerToken;
  if (pixels > max_pixels) {
    fit *= std::sqrt(max_pixels / pixels);
  }
  const double per_css = natural * fit;
  plan.image_width = Pixels(plan.shows.width, per_css);
  plan.image_height = Pixels(plan.shows.height, per_css);
  // Never more pixels than the copy has, whatever the rounding.
  plan.image_width = std::min(
      plan.image_width,
      std::max(1, base::ClampRound(plan.source.width * surface.device_scale)));
  plan.image_height = std::min(
      plan.image_height,
      std::max(1, base::ClampRound(plan.source.height * surface.device_scale)));
  plan.pixels_per_css = plan.image_width / plan.shows.width;
  plan.css_per_pixel = plan.shows.width / plan.image_width;
  return plan;
}

std::string_view SurfaceStatusName(SurfaceStatus status) {
  switch (status) {
    case SurfaceStatus::kOk:
      return "ok";
    case SurfaceStatus::kNoView:
      return "noView";
    case SurfaceStatus::kNoSurface:
      return "noSurface";
    case SurfaceStatus::kCopyFailed:
      return "copyFailed";
    case SurfaceStatus::kEncodeFailed:
      return "encodeFailed";
  }
  return "copyFailed";
}

CaptureFallback ChooseFallback(SurfaceStatus status, bool agent_owned) {
  switch (status) {
    case SurfaceStatus::kOk:
      return CaptureFallback::kNone;
    case SurfaceStatus::kNoView:
    case SurfaceStatus::kNoSurface:
    case SurfaceStatus::kCopyFailed:
      return agent_owned ? CaptureFallback::kCdp : CaptureFallback::kFail;
    case SurfaceStatus::kEncodeFailed:
      // CDP would encode the same bitmap the same way.
      return CaptureFallback::kFail;
  }
  return CaptureFallback::kFail;
}

CaptureError SurfaceError(SurfaceStatus status) {
  switch (status) {
    case SurfaceStatus::kNoView:
      return {"Koan.noSurface",
              "the tab has no page to capture (closing or crashed)"};
    case SurfaceStatus::kNoSurface:
    case SurfaceStatus::kOk:
      return {"Koan.noSurface",
              "the tab did not draw a frame in time (it is hidden or "
              "covered); retry, or bring its window forward"};
    case SurfaceStatus::kCopyFailed:
      return {"Koan.noSurface", "the tab's frame could not be copied; retry"};
    case SurfaceStatus::kEncodeFailed:
      return {"Koan.unavailable", "the image could not be encoded"};
  }
  return {"Koan.unavailable", "the capture failed"};
}

base::DictValue CdpCaptureParams(const PageViewport& viewport,
                                 const SurfaceGeometry& surface,
                                 const CapturePlan& plan,
                                 const CaptureRequest& request) {
  base::DictValue params;
  params.Set("format", ImageFormatName(request.format));
  if (request.format != ImageFormat::kPng) {
    params.Set("quality", request.quality);
  }
  // The clip is in DIPs from the document's top left, drawn at `scale`
  // device pixels per DIP (#409's mapping).
  const double device = viewport.zoom * surface.device_scale;
  params.Set("clip",
             base::DictValue()
                 .Set("x", (viewport.page_x + plan.shows.x) * viewport.zoom)
                 .Set("y", (viewport.page_y + plan.shows.y) * viewport.zoom)
                 .Set("width", plan.shows.width * viewport.zoom)
                 .Set("height", plan.shows.height * viewport.zoom)
                 .Set("scale", std::clamp(plan.pixels_per_css / device, 1e-3,
                                          kMaxCaptureScale)));
  return params;
}

std::vector<CaptureRect> MaskPixels(const CapturePlan& plan,
                                    base::span<const ViewportRect> boxes,
                                    double padding) {
  std::vector<CaptureRect> rects;
  for (const ViewportRect& box : boxes) {
    if (!std::isfinite(box.x) || !std::isfinite(box.y) ||
        !std::isfinite(box.width) || !std::isfinite(box.height) ||
        box.width < 0 || box.height < 0) {
      continue;
    }
    // CSS px from the image's left (top) edge -> image pixels, multiplied
    // first so that whole numbers stay whole.
    auto to_x = [&](double css) {
      return (css - plan.shows.x) * plan.image_width / plan.shows.width;
    };
    auto to_y = [&](double css) {
      return (css - plan.shows.y) * plan.image_height / plan.shows.height;
    };
    const int left = std::max(0, base::ClampFloor(to_x(box.x - padding)));
    const int top = std::max(0, base::ClampFloor(to_y(box.y - padding)));
    const int right = std::min(
        plan.image_width, base::ClampCeil(to_x(box.x + box.width + padding)));
    const int bottom = std::min(
        plan.image_height, base::ClampCeil(to_y(box.y + box.height + padding)));
    if (right > left && bottom > top) {
      rects.push_back({left, top, right - left, bottom - top});
    }
  }
  return rects;
}

base::DictValue CaptureAnswerDict(CaptureAnswer answer) {
  base::DictValue dict;
  dict.Set("data", std::move(answer.data));
  dict.Set("width", answer.width);
  dict.Set("height", answer.height);
  dict.Set("cssPerPixel", std::round(answer.css_per_pixel * 1000) / 1000);
  dict.Set("format", ImageFormatName(answer.format));
  dict.Set("imageTokens", EstimateImageTokens(answer.width, answer.height));
  if (answer.fallback) {
    dict.Set("via", "cdp");
    dict.Set("fallbackReason", SurfaceStatusName(*answer.fallback));
  } else {
    dict.Set("via", "surface");
  }
  if (answer.masked > 0) {
    dict.Set("masked", answer.masked);
  }
  return dict;
}

}  // namespace koan::agent
