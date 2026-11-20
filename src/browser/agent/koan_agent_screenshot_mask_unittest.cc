// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_screenshot_mask.h"

#include <vector>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

base::DictValue Dict(std::string_view json) {
  return *base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
}

// A 1280x800 viewport scrolled 300 px down a 1280x3000 page.
constexpr char kMetrics[] = R"({
  "cssVisualViewport": {"pageX": 0, "pageY": 300, "clientWidth": 1280,
                        "clientHeight": 800},
  "cssContentSize": {"x": 0, "y": 0, "width": 1280, "height": 3000}})";

TEST(KoanAgentScreenshotMaskTest, TheBoxOfTheQuads) {
  std::optional<CssRect> box =
      BoxFromQuads(Dict(R"({"quads": [[10, 20, 110, 20, 110, 50, 10, 50],
                         [10, 50, 60, 50, 60, 70, 10, 70]]})"));
  ASSERT_TRUE(box);
  EXPECT_EQ(box->x, 10);
  EXPECT_EQ(box->y, 20);
  EXPECT_EQ(box->width, 100);
  EXPECT_EQ(box->height, 50);
  EXPECT_FALSE(BoxFromQuads(Dict(R"({"quads": []})")));
  EXPECT_FALSE(BoxFromQuads(Dict(R"({})")));
  EXPECT_FALSE(BoxFromQuads(Dict(R"({"quads": [[1, 2, 3]]})")));
}

TEST(KoanAgentScreenshotMaskTest, TheViewportAtDevicePixels) {
  const base::DictValue metrics = Dict(kMetrics);
  std::optional<ScreenshotGeometry> geometry =
      GeometryFor(base::DictValue(), metrics);
  ASSERT_TRUE(geometry);
  EXPECT_EQ(geometry->y, 300);
  EXPECT_EQ(geometry->width, 1280);
  const std::vector<CssRect> boxes = {{100, 200, 300, 40}};
  const ScreenshotMask mask =
      MaskFor(boxes, ViewportOffset(metrics), *geometry);
  // A 2x display: 2560x1600 pixels, padded by 2 CSS px.
  EXPECT_EQ(MaskRects(mask, 2560, 1600),
            (std::vector<PixelRect>{{196, 396, 608, 88}}));
}

TEST(KoanAgentScreenshotMaskTest, AClipAndItsScale) {
  const base::DictValue metrics = Dict(kMetrics);
  // The field at (100, 200) in the viewport is at (100, 500) on the page; a
  // clip of the page from (50, 450), 400x200, drawn at half size.
  const base::DictValue params = Dict(
      R"({"clip": {"x": 50, "y": 450, "width": 400, "height": 200,
                   "scale": 0.5}})");
  std::optional<ScreenshotGeometry> geometry = GeometryFor(params, metrics);
  ASSERT_TRUE(geometry);
  const std::vector<CssRect> boxes = {{100, 200, 300, 40}};
  const ScreenshotMask mask =
      MaskFor(boxes, ViewportOffset(metrics), *geometry);
  EXPECT_EQ(MaskRects(mask, 200, 100),
            (std::vector<PixelRect>{{24, 24, 152, 22}}));
}

TEST(KoanAgentScreenshotMaskTest, TheWholePage) {
  const base::DictValue metrics = Dict(kMetrics);
  std::optional<ScreenshotGeometry> geometry =
      GeometryFor(Dict(R"({"captureBeyondViewport": true})"), metrics);
  ASSERT_TRUE(geometry);
  EXPECT_EQ(geometry->y, 0);
  EXPECT_EQ(geometry->height, 3000);
  const std::vector<CssRect> boxes = {{100, 200, 300, 40}};
  const ScreenshotMask mask =
      MaskFor(boxes, ViewportOffset(metrics), *geometry);
  EXPECT_EQ(MaskRects(mask, 1280, 3000),
            (std::vector<PixelRect>{{98, 498, 304, 44}}));
}

TEST(KoanAgentScreenshotMaskTest, ClampedAndDropped) {
  ScreenshotMask mask;
  mask.css_width = 100;
  mask.css_height = 100;
  mask.boxes = {{-50, -50, 70, 70}, {200, 200, 10, 10}, {90, 90, 30, 30}};
  EXPECT_EQ(MaskRects(mask, 100, 100),
            (std::vector<PixelRect>{{0, 0, 22, 22}, {88, 88, 12, 12}}));
  EXPECT_TRUE(MaskRects(mask, 0, 100).empty());
  EXPECT_FALSE(GeometryFor(base::DictValue(), base::DictValue()));
}

}  // namespace
}  // namespace koan::agent
