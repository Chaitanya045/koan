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
#include "koan/browser/agent/koan_agent_capture.h"

// How big `koan screenshot` (and `koan act --screenshot`) captures a tab, as
// pure functions (.agent/notes/agent-control.md section 12, "Screenshot
// sizes"). A model pays for an image by its area, about one token per 28x28
// pixels (agent::EstimateImageTokens()), so each size fits the viewport's
// long edge to a number of pixels. The browser copies the frame it already
// drew, scaled on the GPU to that size (Koan.captureScaled,
// koan_agent_capture.h), on every tab: the agent's own and the user's.
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
inline constexpr int kFullLongEdge = agent::kMaxImageLongEdge;
// Around a clipped element, in CSS pixels.
inline constexpr double kClipMargin = 8;

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

// The box around DOM.getContentQuads's quads (viewport CSS pixels); nullopt
// if there are none. An element with no size gives a box of no size.
std::optional<CssRect> QuadsBounds(const base::DictValue& quads_result);

// Koan.captureScaled's params for a capture of the tab `target` at `size`
// (nullopt: normal, or for a clip the region at device pixels, as full):
// the long edge, and at most CSS pixels (micro, mini, normal) or the
// display's (full). `region` (viewport CSS pixels) is grown by `margin`; the
// browser cuts it to the viewport.
base::DictValue CaptureParams(std::string_view target,
                              std::optional<ShotSize> size,
                              std::optional<CssRect> region,
                              double margin);

// The error for a clip the browser refused (Koan.offScreen,
// Koan.emptyElement), naming `what` ("@12", "the rectangle"); empty for
// another error.
std::string ClipRefusal(std::string_view code, std::string_view what);

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
