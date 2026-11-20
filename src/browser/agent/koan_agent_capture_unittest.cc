// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_capture.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

base::DictValue Dict(std::string_view json) {
  return std::move(*base::JSONReader::Read(json, base::JSON_PARSE_RFC))
      .TakeDict();
}

PageViewport Viewport(double width,
                      double height,
                      double zoom = 1,
                      double pinch = 1) {
  PageViewport viewport;
  viewport.width = width;
  viewport.height = height;
  viewport.zoom = zoom;
  viewport.pinch = pinch;
  return viewport;
}

// The view of `viewport` at `device_scale` (no scrollbars).
SurfaceGeometry SurfaceOf(const PageViewport& viewport, double device_scale) {
  return SurfaceGeometry{
      static_cast<int>(viewport.width * viewport.zoom * viewport.pinch + 0.5),
      static_cast<int>(viewport.height * viewport.zoom * viewport.pinch + 0.5),
      device_scale};
}

// The CLI's sizes (koan_cli_screenshot.h): the long edge, and CSS pixels or
// the display's.
CaptureRequest Micro() {
  return {.long_edge = 320, .scale = 1};
}
CaptureRequest Mini() {
  return {.long_edge = 640, .scale = 1};
}
CaptureRequest Normal() {
  return {.long_edge = 1568, .scale = 1};
}
CaptureRequest Full() {
  return {.long_edge = kMaxImageLongEdge, .scale = kMaxCaptureScale};
}
CaptureRequest Clip(ViewportRect clip, CaptureRequest request = Full()) {
  request.clip = clip;
  return request;
}

CapturePlan Plan(const PageViewport& viewport,
                 double device_scale,
                 const CaptureRequest& request) {
  auto plan = PlanCapture(viewport, SurfaceOf(viewport, device_scale), request);
  EXPECT_TRUE(plan.has_value()) << (plan.has_value() ? "" : plan.error().code);
  return plan.value_or(CapturePlan());
}

std::string PlanError(const PageViewport& viewport,
                      double device_scale,
                      const CaptureRequest& request) {
  auto plan = PlanCapture(viewport, SurfaceOf(viewport, device_scale), request);
  EXPECT_FALSE(plan.has_value());
  return plan.has_value() ? std::string() : plan.error().code;
}

TEST(KoanAgentCaptureTest, TokenEstimate) {
  // About one token per 28x28 pixels, rounded up.
  EXPECT_EQ(EstimateImageTokens(28, 28), 1);
  EXPECT_EQ(EstimateImageTokens(1, 1), 1);
  EXPECT_EQ(EstimateImageTokens(640, 400), 327);
  EXPECT_EQ(EstimateImageTokens(320, 200), 82);
  EXPECT_EQ(EstimateImageTokens(1440, 900), 1654);
  EXPECT_EQ(EstimateImageTokens(2576, 1449), 4761);
  // A bigger image is scaled down by the model's API: it costs no more.
  EXPECT_EQ(EstimateImageTokens(2880, 1800), kMaxImageTokens);
  EXPECT_EQ(EstimateImageTokens(0, 400), 0);
}

TEST(KoanAgentCaptureTest, ParsesTheRequest) {
  auto defaults = ParseCaptureRequest(base::DictValue());
  ASSERT_TRUE(defaults.has_value());
  EXPECT_EQ(defaults->long_edge, kDefaultImageLongEdge);
  EXPECT_EQ(defaults->scale, 1);
  EXPECT_FALSE(defaults->clip);
  EXPECT_EQ(defaults->format, ImageFormat::kPng);

  auto full = ParseCaptureRequest(Dict(R"({
      "targetId": "T1", "longEdge": 2576, "scale": 4, "format": "jpeg",
      "quality": 60, "clip": {"x": -10, "y": 20.5, "width": 30, "height": 0}})"));
  ASSERT_TRUE(full.has_value());
  EXPECT_EQ(full->long_edge, 2576);
  EXPECT_EQ(full->scale, 4);
  EXPECT_EQ(full->format, ImageFormat::kJpeg);
  EXPECT_EQ(full->quality, 60);
  ASSERT_TRUE(full->clip);
  EXPECT_EQ(full->clip->x, -10);
  EXPECT_EQ(full->clip->y, 20.5);
  EXPECT_EQ(full->clip->height, 0);
  // The CLI's short names for the sides.
  auto short_names = ParseCaptureRequest(
      Dict(R"({"clip": {"x": 1, "y": 2, "w": 3, "h": 4}})"));
  ASSERT_TRUE(short_names.has_value());
  EXPECT_EQ(short_names->clip->width, 3);
  EXPECT_EQ(short_names->clip->height, 4);

  for (const char* bad : {
           R"({"longEdge": 15})",
           R"({"longEdge": 2577})",
           R"({"longEdge": 100.5})",
           R"({"longEdge": "big"})",
           R"({"scale": 0})",
           R"({"scale": 4.5})",
           R"({"scale": -1})",
           R"({"clip": [1, 2, 3, 4]})",
           R"({"clip": {"x": 1, "y": 2, "width": -3, "height": 4}})",
           R"({"clip": {"x": 1, "y": 2, "width": 3}})",
           R"({"clip": {"x": 1e7, "y": 2, "width": 3, "height": 4}})",
           R"({"clip": {"x": 1, "y": 2, "width": 20000, "height": 4}})",
           R"({"format": "gif"})",
           R"({"quality": 101})",
           R"({"quality": 50.5})",
       }) {
    auto parsed = ParseCaptureRequest(Dict(bad));
    ASSERT_FALSE(parsed.has_value()) << bad;
    EXPECT_EQ(parsed.error().code, "Koan.invalidParams") << bad;
  }
}

TEST(KoanAgentCaptureTest, ReadsTheViewport) {
  std::optional<PageViewport> zoomed = ReadPageViewport(Dict(R"({
      "cssVisualViewport": {"pageX": 0, "pageY": 50.5, "clientWidth": 1152,
                            "clientHeight": 720, "zoom": 1.25, "scale": 1},
      "visualViewport": {"clientWidth": 2880, "clientHeight": 1800}})"));
  ASSERT_TRUE(zoomed);
  EXPECT_DOUBLE_EQ(zoomed->page_y, 50.5);
  EXPECT_DOUBLE_EQ(zoomed->width, 1152);
  EXPECT_DOUBLE_EQ(zoomed->zoom, 1.25);
  EXPECT_DOUBLE_EQ(zoomed->pinch, 1);
  std::optional<PageViewport> pinched = ReadPageViewport(Dict(R"({
      "cssVisualViewport": {"clientWidth": 720, "clientHeight": 450,
                            "scale": 2}})"));
  ASSERT_TRUE(pinched);
  EXPECT_DOUBLE_EQ(pinched->zoom, 1);
  EXPECT_DOUBLE_EQ(pinched->pinch, 2);
  EXPECT_FALSE(ReadPageViewport(Dict(R"({
      "cssVisualViewport": {"clientWidth": 0, "clientHeight": 600}})")));
  EXPECT_FALSE(ReadPageViewport(Dict(R"({
      "cssLayoutViewport": {"clientWidth": 800, "clientHeight": 600}})")));
  EXPECT_FALSE(ReadPageViewport(Dict("{}")));
}

// A 1440x900 window on a Retina display: the sizes the CLI's help states.
TEST(KoanAgentCaptureTest, SizesOnARetinaDisplay) {
  const PageViewport retina = Viewport(1440, 900);
  CapturePlan normal = Plan(retina, 2, Normal());
  EXPECT_TRUE(normal.whole);
  EXPECT_EQ(normal.source, (CaptureRect{0, 0, 1440, 900}));
  EXPECT_EQ(normal.image_width, 1440);
  EXPECT_EQ(normal.image_height, 900);
  EXPECT_DOUBLE_EQ(normal.css_per_pixel, 1);

  CapturePlan mini = Plan(retina, 2, Mini());
  EXPECT_EQ(mini.image_width, 640);
  EXPECT_EQ(mini.image_height, 400);
  EXPECT_DOUBLE_EQ(mini.css_per_pixel, 2.25);

  CapturePlan micro = Plan(retina, 2, Micro());
  EXPECT_EQ(micro.image_width, 320);
  EXPECT_EQ(micro.image_height, 200);
  EXPECT_DOUBLE_EQ(micro.css_per_pixel, 4.5);

  // Device pixels would be 2880x1800: fitted to the long edge, then to what
  // kMaxImageTokens is worth.
  CapturePlan full = Plan(retina, 2, Full());
  EXPECT_LE(full.image_width, kMaxImageLongEdge);
  EXPECT_GE(full.image_width, 2400);
  EXPECT_LE(EstimateImageTokens(full.image_width, full.image_height),
            kMaxImageTokens);
  EXPECT_NEAR(static_cast<double>(full.image_width) / full.image_height, 1.6,
              0.01);
  // The default request is normal.
  auto by_default = PlanCapture(retina, SurfaceOf(retina, 2), CaptureRequest());
  ASSERT_TRUE(by_default.has_value());
  EXPECT_EQ(by_default->image_width, 1440);
}

TEST(KoanAgentCaptureTest, DeviceScaleFactors) {
  // 1x: normal and full are the view as it is.
  for (const CaptureRequest& request : {Normal(), Full()}) {
    CapturePlan plan = Plan(Viewport(1440, 900), 1, request);
    EXPECT_EQ(plan.image_width, 1440);
    EXPECT_EQ(plan.image_height, 900);
  }
  EXPECT_EQ(Plan(Viewport(1440, 900), 1, Mini()).image_width, 640);
  // 3x: normal is still CSS pixels; full is capped.
  CapturePlan normal3 = Plan(Viewport(1000, 600), 3, Normal());
  EXPECT_EQ(normal3.image_width, 1000);
  EXPECT_EQ(normal3.image_height, 600);
  CapturePlan full3 = Plan(Viewport(1000, 600), 3, Full());
  EXPECT_LE(full3.image_width, kMaxImageLongEdge);
  EXPECT_LE(EstimateImageTokens(full3.image_width, full3.image_height),
            kMaxImageTokens);
  // 2x, small enough: full is the device's pixels.
  CapturePlan full2 = Plan(Viewport(800, 500), 2, Full());
  EXPECT_EQ(full2.image_width, 1600);
  EXPECT_EQ(full2.image_height, 1000);
  EXPECT_DOUBLE_EQ(full2.css_per_pixel, 0.5);
}

TEST(KoanAgentCaptureTest, NeverEnlarges) {
  // A viewport smaller than the size: CSS pixels, not 320 or 640.
  for (const CaptureRequest& request : {Micro(), Mini(), Normal()}) {
    CapturePlan plan = Plan(Viewport(200, 100), 2, request);
    EXPECT_EQ(plan.image_width, 200);
    EXPECT_EQ(plan.image_height, 100);
    EXPECT_DOUBLE_EQ(plan.css_per_pixel, 1);
  }
  EXPECT_EQ(Plan(Viewport(200, 100), 2, Full()).image_width, 400);
  // A page zoomed out (50% on a 1x display) is not drawn larger than it
  // shows: normal is the device's pixels then.
  CapturePlan zoomed_out = Plan(Viewport(2000, 1000, 0.5), 1, Normal());
  EXPECT_EQ(zoomed_out.image_width, 1000);
  EXPECT_EQ(zoomed_out.image_height, 500);
  // Never more pixels than the copy has, whatever is asked.
  CapturePlan sharp = Plan(Viewport(300, 200), 1, Full());
  EXPECT_EQ(sharp.image_width, 300);
}

TEST(KoanAgentCaptureTest, VeryWideAndVeryTallViewports) {
  CapturePlan wide = Plan(Viewport(5000, 300), 2, Normal());
  EXPECT_EQ(wide.image_width, 1568);
  EXPECT_EQ(wide.image_height, 94);
  CapturePlan wide_micro = Plan(Viewport(5000, 300), 2, Micro());
  EXPECT_EQ(wide_micro.image_width, 320);
  EXPECT_EQ(wide_micro.image_height, 19);
  CapturePlan tall = Plan(Viewport(400, 10000), 2, Mini());
  EXPECT_EQ(tall.image_width, 26);
  EXPECT_EQ(tall.image_height, 640);
  // Never a side of no pixels.
  CapturePlan sliver = Plan(Viewport(10000, 2), 1, Micro());
  EXPECT_EQ(sliver.image_width, 320);
  EXPECT_EQ(sliver.image_height, 1);
}

// CSS px -> DIP -> device pixels, with the browser's zoom and the pinch zoom,
// at device scale factors 1, 2 and 3.
TEST(KoanAgentCaptureTest, MapsCssToDipsAndPixels) {
  for (double device_scale : {1.0, 2.0, 3.0}) {
    // 125% zoom: a CSS px is 1.25 DIPs; the clip's DIPs are whole, outwards.
    const PageViewport zoomed = Viewport(1152, 720, 1.25);
    CapturePlan plan = Plan(zoomed, device_scale, Clip({10, 10, 100, 40}));
    EXPECT_EQ(plan.source, (CaptureRect{12, 12, 126, 51})) << device_scale;
    EXPECT_FALSE(plan.whole);
    EXPECT_DOUBLE_EQ(plan.shows.x, 9.6);
    EXPECT_DOUBLE_EQ(plan.shows.width, 100.8);
    // At the display's pixels: the DIPs times the device scale.
    EXPECT_EQ(plan.image_width, static_cast<int>(126 * device_scale));
    EXPECT_EQ(plan.image_height, static_cast<int>(51 * device_scale));
    // The whole view at normal: CSS pixels, whatever the scale.
    CapturePlan normal = Plan(zoomed, device_scale, Normal());
    EXPECT_TRUE(normal.whole);
    EXPECT_EQ(normal.source, (CaptureRect{0, 0, 1440, 900}));
    EXPECT_EQ(normal.image_width, 1152);
    EXPECT_EQ(normal.image_height, 720);

    // Pinched in 2x: a CSS px is 2 DIPs.
    const PageViewport pinched = Viewport(720, 450, 1, 2);
    CapturePlan pinch_clip =
        Plan(pinched, device_scale, Clip({10, 10, 100, 50}));
    EXPECT_EQ(pinch_clip.source, (CaptureRect{20, 20, 200, 100}));
    // Up to kMaxCaptureScale image pixels per CSS pixel (2x pinch on a 3x
    // display would be 6).
    EXPECT_EQ(
        pinch_clip.image_width,
        static_cast<int>(100 * std::min(2 * device_scale, kMaxCaptureScale)));
    CapturePlan pinch_normal = Plan(pinched, device_scale, Normal());
    EXPECT_EQ(pinch_normal.image_width, 720);
  }
}

TEST(KoanAgentCaptureTest, ClipsAndTheirIntersections) {
  const PageViewport retina = Viewport(1440, 900);
  // An element with the CLI's margin: device pixels by default.
  CapturePlan sharp = Plan(retina, 2, Clip({92, 192, 66, 36}));
  EXPECT_EQ(sharp.source, (CaptureRect{92, 192, 66, 36}));
  EXPECT_EQ(sharp.image_width, 132);
  EXPECT_EQ(sharp.image_height, 72);
  EXPECT_DOUBLE_EQ(sharp.css_per_pixel, 0.5);
  // With a size, that size's rule (never enlarged).
  CapturePlan mini = Plan(retina, 2, Clip({92, 192, 66, 36}, Mini()));
  EXPECT_EQ(mini.image_width, 66);
  // Cut to the viewport at its edges.
  CapturePlan corner = Plan(retina, 2, Clip({1422, 882, 58, 58}));
  EXPECT_EQ(corner.source, (CaptureRect{1422, 882, 18, 18}));
  CapturePlan top_left = Plan(retina, 2, Clip({-38, -38, 58, 58}));
  EXPECT_EQ(top_left.source, (CaptureRect{0, 0, 20, 20}));
  // A clip over the whole view is the whole view.
  EXPECT_TRUE(Plan(retina, 2, Clip({-10, -10, 2000, 2000})).whole);
  // A big element at full size is capped like the viewport.
  EXPECT_LE(Plan(retina, 2, Clip({0, 0, 1440, 900})).image_width,
            kMaxImageLongEdge);
}

TEST(KoanAgentCaptureTest, ClipErrors) {
  const PageViewport retina = Viewport(1440, 900);
  EXPECT_EQ(PlanError(retina, 2, Clip({2000, 100, 50, 50})), "Koan.offScreen");
  EXPECT_EQ(PlanError(retina, 2, Clip({100, -500, 50, 50})), "Koan.offScreen");
  // Less than a CSS pixel of it shows.
  EXPECT_EQ(PlanError(retina, 2, Clip({1439.5, 100, 50, 50})),
            "Koan.offScreen");
  EXPECT_EQ(PlanError(retina, 2, Clip({10, 10, 0, 0})), "Koan.emptyElement");
  EXPECT_EQ(PlanError(retina, 2, Clip({10, 10, 30, 0})), "Koan.emptyElement");
  // A view of no size.
  auto none = PlanCapture(retina, SurfaceGeometry{0, 900, 2}, Normal());
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error().code, "Koan.noSurface");
}

TEST(KoanAgentCaptureTest, TheFallback) {
  // Only an agent's own tab is drawn again by CDP; a user's tab never.
  for (SurfaceStatus status :
       {SurfaceStatus::kNoView, SurfaceStatus::kNoSurface,
        SurfaceStatus::kCopyFailed}) {
    EXPECT_EQ(ChooseFallback(status, /*agent_owned=*/true),
              CaptureFallback::kCdp);
    EXPECT_EQ(ChooseFallback(status, /*agent_owned=*/false),
              CaptureFallback::kFail);
    EXPECT_EQ(SurfaceError(status).code, "Koan.noSurface");
  }
  EXPECT_EQ(ChooseFallback(SurfaceStatus::kOk, false), CaptureFallback::kNone);
  EXPECT_EQ(ChooseFallback(SurfaceStatus::kEncodeFailed, true),
            CaptureFallback::kFail);
}

TEST(KoanAgentCaptureTest, CdpFallbackParams) {
  // 125% zoom on a 2x display, scrolled 100 down: #409's mapping (DIPs from
  // the document's top).
  PageViewport viewport = Viewport(1152, 720, 1.25);
  viewport.page_y = 100;
  const SurfaceGeometry surface = SurfaceOf(viewport, 2);
  CaptureRequest request = Normal();
  auto plan = PlanCapture(viewport, surface, request);
  ASSERT_TRUE(plan.has_value());
  base::DictValue params = CdpCaptureParams(viewport, surface, *plan, request);
  EXPECT_EQ(*params.FindString("format"), "png");
  EXPECT_FALSE(params.contains("quality"));
  EXPECT_DOUBLE_EQ(*params.FindDoubleByDottedPath("clip.x"), 0);
  EXPECT_DOUBLE_EQ(*params.FindDoubleByDottedPath("clip.y"), 125);
  EXPECT_DOUBLE_EQ(*params.FindDoubleByDottedPath("clip.width"), 1440);
  EXPECT_DOUBLE_EQ(*params.FindDoubleByDottedPath("clip.height"), 900);
  EXPECT_DOUBLE_EQ(*params.FindDoubleByDottedPath("clip.scale"), 0.4);
  request.format = ImageFormat::kWebp;
  request.quality = 70;
  EXPECT_EQ(
      CdpCaptureParams(viewport, surface, *plan, request).FindInt("quality"),
      70);
}

TEST(KoanAgentCaptureTest, MasksInTheImagesPixels) {
  // Mini of a 1440x900 viewport: 1 image px = 2.25 CSS px.
  const PageViewport retina = Viewport(1440, 900);
  CapturePlan mini = Plan(retina, 2, Mini());
  const std::vector<ViewportRect> boxes = {
      {225, 90, 450, 45},      // inside
      {-100, -100, 110, 110},  // at the corner: cut
      {5000, 10, 10, 10},      // outside: left out
  };
  std::vector<CaptureRect> rects = MaskPixels(mini, boxes, 0);
  ASSERT_EQ(rects.size(), 2u);
  EXPECT_EQ(rects[0], (CaptureRect{100, 40, 200, 20}));
  EXPECT_EQ(rects[1], (CaptureRect{0, 0, 5, 5}));
  // Padded, and rounded outwards.
  std::vector<CaptureRect> padded = MaskPixels(mini, boxes, 2.25);
  EXPECT_EQ(padded[0], (CaptureRect{99, 39, 202, 22}));
  // A clip: relative to what the image shows.
  CapturePlan clip = Plan(retina, 2, Clip({100, 100, 200, 100}));
  std::vector<CaptureRect> in_clip =
      MaskPixels(clip, std::vector<ViewportRect>{{150, 120, 10, 10}}, 0);
  ASSERT_EQ(in_clip.size(), 1u);
  EXPECT_EQ(in_clip[0], (CaptureRect{100, 40, 20, 20}));
}

TEST(KoanAgentCaptureTest, TheAnswer) {
  CaptureAnswer surface;
  surface.data = "AQID";
  surface.width = 640;
  surface.height = 400;
  surface.css_per_pixel = 2.25;
  base::DictValue copied = CaptureAnswerDict(surface);
  EXPECT_EQ(*copied.FindString("data"), "AQID");
  EXPECT_EQ(copied.FindInt("width"), 640);
  EXPECT_EQ(copied.FindDouble("cssPerPixel"), 2.25);
  EXPECT_EQ(copied.FindInt("imageTokens"), 327);
  EXPECT_EQ(*copied.FindString("via"), "surface");
  EXPECT_FALSE(copied.contains("fallbackReason"));
  EXPECT_FALSE(copied.contains("masked"));
  CaptureAnswer drawn = surface;
  drawn.fallback = SurfaceStatus::kNoSurface;
  drawn.masked = 0;
  base::DictValue cdp = CaptureAnswerDict(drawn);
  EXPECT_EQ(*cdp.FindString("via"), "cdp");
  EXPECT_EQ(*cdp.FindString("fallbackReason"), "noSurface");
  surface.masked = 2;
  EXPECT_EQ(CaptureAnswerDict(surface).FindInt("masked"), 2);
}

}  // namespace
}  // namespace koan::agent
