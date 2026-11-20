// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_SCREENSHOT_H_
#define KOAN_CLI_KOAN_CLI_SCREENSHOT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/values.h"

// How big `koan screenshot` (and `koan act --screenshot`) captures a tab, as
// pure functions (.agent/notes/agent-control.md section 12, "Screenshot
// sizes"). A model pays for an image by its area, about one token per 28x28
// pixels, so each size fits the viewport's long edge to a number of pixels.
// The browser renders the page at that scale (Page.captureScreenshot's clip
// and scale), so text stays as sharp as the size allows; nothing is resized
// after the capture.
namespace koan::cli {

enum class ShotSize {
  // Long edge 320 px: did the page load, is a dialog or an error showing.
  kMicro,
  // Long edge 640 px: the layout, which state the page is in.
  kMini,
  // CSS pixels (1x), long edge at most 1568: the default.
  kNormal,
  // Device pixels (2x on a Retina display), long edge at most 2576.
  kFull,
};
std::optional<ShotSize> ParseShotSize(std::string_view name);
std::string_view ShotSizeName(ShotSize size);

inline constexpr int kMicroLongEdge = 320;
inline constexpr int kMiniLongEdge = 640;
inline constexpr int kNormalLongEdge = 1568;
inline constexpr int kFullLongEdge = 2576;
// The most an image costs: a larger one is scaled down by the model's API to
// about this many tokens, so pixels beyond it are paid for in bytes only.
inline constexpr int kMaxImageTokens = 4784;
inline constexpr int kPixelsPerToken = 28 * 28;
// Around a clipped element, in CSS pixels.
inline constexpr double kClipMargin = 8;

// About what a model is charged for a `width` x `height` image.
int EstimateImageTokens(int width, int height);

// A rectangle in CSS pixels.
struct CssRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

// --clip: an element (a snapshot's @ref, or a locator, as act takes them) or
// "x,y,w,h", a rectangle in CSS pixels from the viewport's top left (as in a
// normal screenshot, scaled by its "1 px = N CSS px").
struct ClipSpec {
  std::string target;
  std::optional<CssRect> rect;
};
// nullopt (and `error`) if `text` is neither.
std::optional<ClipSpec> ParseClip(std::string_view text, std::string* error);

// The visual viewport, from Page.getLayoutMetrics.
struct ViewportMetrics {
  // Where it is in the document, and its size, in CSS pixels.
  double page_x = 0;
  double page_y = 0;
  double width = 0;
  double height = 0;
  // DIPs per CSS pixel: the browser's zoom.
  double zoom = 1;
  // Device pixels per DIP: 2 on a Retina display.
  double device_scale = 1;
};
std::optional<ViewportMetrics> ReadViewportMetrics(
    const base::DictValue& layout_metrics);

// The box around DOM.getContentQuads's quads (viewport CSS pixels); nullopt
// if there are none. An element with no size gives a box of no size.
std::optional<CssRect> QuadsBounds(const base::DictValue& quads_result);

// What to ask Page.captureScreenshot for.
struct ShotPlan {
  // A clip and scale (CDP's units: DIPs, from the document's top left);
  // false: a plain capture of the viewport, at device pixels.
  bool clip = false;
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  double scale = 1;
  // The image this gives.
  int image_width = 0;
  int image_height = 0;
  // CSS pixels per image pixel, along each axis.
  double css_per_pixel = 1;
};
struct ShotPlanResult {
  std::optional<ShotPlan> plan;
  // Why not: a Koan error, for the agent.
  std::string error;
};

// What to capture. `size` nullopt: kNormal, or for a clip the region at
// device pixels (as kFull). `region` (viewport CSS pixels; nullopt: the
// whole viewport) is cut to the viewport, after `margin` around it; `what`
// names it in errors ("@12", "the rectangle"). `may_clip`: the facade
// allows clip and scale on the tab (only on the agent's own tabs, since they
// resize the tab's view): without it, a plain capture at device pixels.
// Never larger than the size the tab shows (CSS pixels for micro, mini and
// normal; device pixels for full), and never more pixels than
// kMaxImageTokens is worth.
ShotPlanResult PlanShot(const ViewportMetrics& viewport,
                        std::optional<ShotSize> size,
                        std::optional<CssRect> region,
                        double margin,
                        std::string_view what,
                        bool may_clip);

// A PNG's width and height, from its header; nullopt if it is not a PNG.
std::optional<std::pair<int, int>> PngSize(base::span<const uint8_t> png);

// "saved /tmp/x.png (640×400, ~330 image tokens, 1 px = 2.25 CSS px)".
std::string ShotLine(std::string_view path,
                     int width,
                     int height,
                     double css_per_pixel);
// The same, for --json: path, width, height, imageTokens, cssPxPerPx.
base::DictValue ShotJson(std::string_view path,
                         int width,
                         int height,
                         double css_per_pixel);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_SCREENSHOT_H_
