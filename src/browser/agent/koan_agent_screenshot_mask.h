// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SCREENSHOT_MASK_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SCREENSHOT_MASK_H_

#include <optional>
#include <vector>

#include "base/containers/span.h"
#include "base/values.h"

// Where a screenshot shows the card fields Koan filled for an agent
// (.agent/notes/agent-control.md section 4.5, "Secrets Koan filled"): the
// boxes the facade paints over, in the image's pixels, from what
// DOM.getContentQuads, Page.getLayoutMetrics and the screenshot's own
// parameters say. Pure logic; the browser decodes, paints and encodes.
namespace koan::agent {

struct CssRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

// What part of the page an image shows, in CSS px of the document.
struct ScreenshotGeometry {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

// The part of the page Page.captureScreenshot with `params` draws, from
// Page.getLayoutMetrics' answer: its `clip`, else the whole page with
// captureBeyondViewport, else the visual viewport. nullopt if the answers do
// not say (or say a zero size).
std::optional<ScreenshotGeometry> GeometryFor(
    const base::DictValue& screenshot_params,
    const base::DictValue& layout_metrics);

// The visual viewport's place in the document (cssVisualViewport's pageX
// and pageY), (0, 0) if the answer does not say.
CssRect ViewportOffset(const base::DictValue& layout_metrics);

// The bounding box of DOM.getContentQuads' quads, in CSS px of the visual
// viewport; nullopt if it has none.
std::optional<CssRect> BoxFromQuads(const base::DictValue& quads_answer);

// The boxes to paint over, relative to the image's top-left corner in CSS
// px, and the CSS size the image shows.
struct ScreenshotMask {
  std::vector<CssRect> boxes;
  double css_width = 0;
  double css_height = 0;
};

// `boxes` (viewport CSS px, the viewport at `viewport` in the document) for
// an image that shows `geometry`.
ScreenshotMask MaskFor(base::span<const CssRect> boxes,
                       const CssRect& viewport,
                       const ScreenshotGeometry& geometry);

struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  friend bool operator==(const PixelRect&, const PixelRect&) = default;
};

// How far past a field's box the paint goes, in CSS px (a focus ring, a
// border the box leaves out).
inline constexpr double kMaskPadding = 2;

// The mask's boxes in the pixels of an image of `image_width` by
// `image_height`, padded by kMaskPadding, rounded outwards, clamped to the
// image; boxes wholly outside it are left out.
std::vector<PixelRect> MaskRects(const ScreenshotMask& mask,
                                 int image_width,
                                 int image_height);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SCREENSHOT_MASK_H_
