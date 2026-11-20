// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_screenshot.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_skill.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

base::DictValue Dict(std::string_view json) {
  return std::move(*base::JSONReader::Read(json, base::JSON_PARSE_RFC))
      .TakeDict();
}

ViewportMetrics Viewport(double width,
                         double height,
                         double device_scale,
                         double zoom = 1) {
  ViewportMetrics viewport;
  viewport.width = width;
  viewport.height = height;
  viewport.device_scale = device_scale;
  viewport.zoom = zoom;
  return viewport;
}

ShotPlan Plan(const ViewportMetrics& viewport,
              std::optional<ShotSize> size,
              std::optional<CssRect> region = std::nullopt,
              double margin = 0) {
  ShotPlanResult result =
      PlanShot(viewport, size, region, margin, "@12", /*may_clip=*/true);
  EXPECT_TRUE(result.plan) << result.error;
  return result.plan.value_or(ShotPlan());
}

std::string PlanErrorOf(const ViewportMetrics& viewport,
                        std::optional<CssRect> region,
                        bool may_clip = true) {
  ShotPlanResult result =
      PlanShot(viewport, std::nullopt, region, kClipMargin, "@12", may_clip);
  EXPECT_FALSE(result.plan);
  return result.error;
}

TEST(KoanCliScreenshotTest, SizeNames) {
  for (ShotSize size : {ShotSize::kMicro, ShotSize::kMini, ShotSize::kNormal,
                        ShotSize::kFull}) {
    EXPECT_EQ(ParseShotSize(ShotSizeName(size)), size);
  }
  EXPECT_FALSE(ParseShotSize("large"));
  EXPECT_FALSE(ParseShotSize(""));
}

TEST(KoanCliScreenshotTest, TokenEstimate) {
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

// A 1440x900 window on a Retina display, the sizes as the help states them.
TEST(KoanCliScreenshotTest, SizesOnARetinaDisplay) {
  const ViewportMetrics retina = Viewport(1440, 900, 2);

  ShotPlan normal = Plan(retina, ShotSize::kNormal);
  EXPECT_EQ(normal.image_width, 1440);
  EXPECT_EQ(normal.image_height, 900);
  EXPECT_DOUBLE_EQ(normal.css_per_pixel, 1);
  // Drawn at half the device's scale: the clip is the viewport.
  EXPECT_TRUE(normal.clip);
  EXPECT_DOUBLE_EQ(normal.scale, 0.5);
  EXPECT_DOUBLE_EQ(normal.width, 1440);
  EXPECT_DOUBLE_EQ(normal.height, 900);
  // The default is normal.
  ShotPlan by_default = Plan(retina, std::nullopt);
  EXPECT_EQ(by_default.image_width, 1440);
  EXPECT_DOUBLE_EQ(by_default.scale, 0.5);

  ShotPlan mini = Plan(retina, ShotSize::kMini);
  EXPECT_EQ(mini.image_width, 640);
  EXPECT_EQ(mini.image_height, 400);
  EXPECT_DOUBLE_EQ(mini.css_per_pixel, 2.25);

  ShotPlan micro = Plan(retina, ShotSize::kMicro);
  EXPECT_EQ(micro.image_width, 320);
  EXPECT_EQ(micro.image_height, 200);
  EXPECT_DOUBLE_EQ(micro.css_per_pixel, 4.5);

  // Device pixels would be 2880x1800: fitted to the long edge, then to what
  // kMaxImageTokens is worth.
  ShotPlan full = Plan(retina, ShotSize::kFull);
  EXPECT_LE(full.image_width, kFullLongEdge);
  EXPECT_GE(full.image_width, 2400);
  EXPECT_LE(EstimateImageTokens(full.image_width, full.image_height),
            kMaxImageTokens);
  EXPECT_NEAR(static_cast<double>(full.image_width) / full.image_height, 1.6,
              0.01);
  EXPECT_TRUE(full.clip);
  EXPECT_LT(full.scale, 1);
}

TEST(KoanCliScreenshotTest, DeviceScaleFactors) {
  // 1x: normal and full are the viewport as it is, the plain capture (no
  // clip: the tab's view is not resized).
  for (ShotSize size : {ShotSize::kNormal, ShotSize::kFull}) {
    ShotPlan plan = Plan(Viewport(1440, 900, 1), size);
    EXPECT_EQ(plan.image_width, 1440);
    EXPECT_EQ(plan.image_height, 900);
    EXPECT_FALSE(plan.clip);
    EXPECT_DOUBLE_EQ(plan.scale, 1);
  }
  ShotPlan mini1 = Plan(Viewport(1440, 900, 1), ShotSize::kMini);
  EXPECT_EQ(mini1.image_width, 640);
  EXPECT_TRUE(mini1.clip);

  // 3x: normal is still CSS pixels.
  ShotPlan normal3 = Plan(Viewport(1000, 600, 3), ShotSize::kNormal);
  EXPECT_EQ(normal3.image_width, 1000);
  EXPECT_EQ(normal3.image_height, 600);
  EXPECT_DOUBLE_EQ(normal3.scale, 1.0 / 3);
  ShotPlan full3 = Plan(Viewport(1000, 600, 3), ShotSize::kFull);
  EXPECT_LE(full3.image_width, kFullLongEdge);
  EXPECT_LE(EstimateImageTokens(full3.image_width, full3.image_height),
            kMaxImageTokens);
  // 2x, small enough: full is the device's pixels, the plain capture.
  ShotPlan full2 = Plan(Viewport(800, 500, 2), ShotSize::kFull);
  EXPECT_EQ(full2.image_width, 1600);
  EXPECT_EQ(full2.image_height, 1000);
  EXPECT_FALSE(full2.clip);
  EXPECT_DOUBLE_EQ(full2.css_per_pixel, 0.5);
}

TEST(KoanCliScreenshotTest, NeverEnlarges) {
  // A viewport smaller than the size: CSS pixels, not 320 or 640.
  const ViewportMetrics tiny = Viewport(200, 100, 2);
  for (ShotSize size : {ShotSize::kMicro, ShotSize::kMini, ShotSize::kNormal}) {
    ShotPlan plan = Plan(tiny, size);
    EXPECT_EQ(plan.image_width, 200);
    EXPECT_EQ(plan.image_height, 100);
    EXPECT_DOUBLE_EQ(plan.css_per_pixel, 1);
  }
  ShotPlan full = Plan(tiny, ShotSize::kFull);
  EXPECT_EQ(full.image_width, 400);
  EXPECT_FALSE(full.clip);
  // A page zoomed out (50% on a 1x display) is not drawn larger than it
  // shows: normal is the device's pixels then.
  ShotPlan zoomed_out = Plan(Viewport(2000, 1000, 1, 0.5), ShotSize::kNormal);
  EXPECT_EQ(zoomed_out.image_width, 1000);
  EXPECT_LE(zoomed_out.scale, 1);
}

TEST(KoanCliScreenshotTest, VeryWideAndVeryTallViewports) {
  ShotPlan wide = Plan(Viewport(5000, 300, 2), ShotSize::kNormal);
  EXPECT_EQ(wide.image_width, kNormalLongEdge);
  EXPECT_EQ(wide.image_height, 94);
  ShotPlan wide_micro = Plan(Viewport(5000, 300, 2), ShotSize::kMicro);
  EXPECT_EQ(wide_micro.image_width, 320);
  EXPECT_EQ(wide_micro.image_height, 19);
  ShotPlan tall = Plan(Viewport(400, 10000, 2), ShotSize::kMini);
  EXPECT_EQ(tall.image_width, 26);
  EXPECT_EQ(tall.image_height, 640);
  // Never a side of no pixels.
  ShotPlan sliver = Plan(Viewport(10000, 2, 1), ShotSize::kMicro);
  EXPECT_EQ(sliver.image_width, 320);
  EXPECT_EQ(sliver.image_height, 1);
}

TEST(KoanCliScreenshotTest, ZoomAndScroll) {
  // 125% zoom on a 2x display: 1152x720 CSS pixels, scrolled 100 down. The
  // clip is in DIPs from the document's top.
  ViewportMetrics viewport = Viewport(1152, 720, 2, 1.25);
  viewport.page_y = 100;
  ShotPlan normal = Plan(viewport, ShotSize::kNormal);
  EXPECT_EQ(normal.image_width, 1152);
  EXPECT_EQ(normal.image_height, 720);
  EXPECT_DOUBLE_EQ(normal.scale, 0.4);
  EXPECT_DOUBLE_EQ(normal.x, 0);
  EXPECT_DOUBLE_EQ(normal.y, 125);
  EXPECT_DOUBLE_EQ(normal.width, 1440);
  EXPECT_DOUBLE_EQ(normal.height, 900);
}

TEST(KoanCliScreenshotTest, ClipAnElement) {
  ViewportMetrics viewport = Viewport(1440, 900, 2);
  viewport.page_y = 1000;
  // Sharp by default: device pixels, with the margin around it.
  ShotPlan sharp =
      Plan(viewport, std::nullopt, CssRect{100, 200, 50, 20}, kClipMargin);
  EXPECT_TRUE(sharp.clip);
  EXPECT_DOUBLE_EQ(sharp.x, 92);
  EXPECT_DOUBLE_EQ(sharp.y, 1192);
  EXPECT_DOUBLE_EQ(sharp.width, 66);
  EXPECT_DOUBLE_EQ(sharp.height, 36);
  EXPECT_DOUBLE_EQ(sharp.scale, 1);
  EXPECT_EQ(sharp.image_width, 132);
  EXPECT_EQ(sharp.image_height, 72);
  EXPECT_DOUBLE_EQ(sharp.css_per_pixel, 0.5);
  // With a size, that size's rule (never enlarged).
  ShotPlan mini =
      Plan(viewport, ShotSize::kMini, CssRect{100, 200, 50, 20}, kClipMargin);
  EXPECT_EQ(mini.image_width, 66);
  EXPECT_DOUBLE_EQ(mini.scale, 0.5);
  // A big element at full size is capped like the viewport.
  ShotPlan big =
      Plan(viewport, std::nullopt, CssRect{0, 0, 1440, 900}, kClipMargin);
  EXPECT_LE(big.image_width, kFullLongEdge);
}

TEST(KoanCliScreenshotTest, ClipAtTheViewportsEdge) {
  const ViewportMetrics viewport = Viewport(1440, 900, 2);
  // Cut to the viewport.
  ShotPlan corner =
      Plan(viewport, std::nullopt, CssRect{1430, 890, 50, 50}, kClipMargin);
  EXPECT_DOUBLE_EQ(corner.x, 1422);
  EXPECT_DOUBLE_EQ(corner.y, 882);
  EXPECT_DOUBLE_EQ(corner.width, 18);
  EXPECT_DOUBLE_EQ(corner.height, 18);
  ShotPlan top_left =
      Plan(viewport, std::nullopt, CssRect{-30, -30, 50, 50}, kClipMargin);
  EXPECT_DOUBLE_EQ(top_left.x, 0);
  EXPECT_DOUBLE_EQ(top_left.width, 28);
  // A rectangle the agent gave: no margin.
  ShotPlan rect = Plan(viewport, ShotSize::kNormal, CssRect{10, 20, 300, 40});
  EXPECT_DOUBLE_EQ(rect.x, 10);
  EXPECT_EQ(rect.image_width, 300);
  EXPECT_EQ(rect.image_height, 40);
}

TEST(KoanCliScreenshotTest, ClipErrors) {
  const ViewportMetrics viewport = Viewport(1440, 900, 2);
  EXPECT_TRUE(PlanErrorOf(viewport, CssRect{2000, 100, 50, 50})
                  .starts_with("Koan.offScreen: @12 is outside the viewport"));
  EXPECT_TRUE(PlanErrorOf(viewport, CssRect{100, -500, 50, 50})
                  .starts_with("Koan.offScreen"));
  EXPECT_TRUE(PlanErrorOf(viewport, CssRect{10, 10, 0, 0})
                  .starts_with("Koan.emptyElement: @12 has no size"));
  EXPECT_TRUE(PlanErrorOf(viewport, CssRect{10, 10, 30, 0})
                  .starts_with("Koan.emptyElement"));
  // A tab of the user's: no clip (it would resize their view).
  EXPECT_TRUE(PlanErrorOf(viewport, CssRect{10, 10, 30, 30}, false)
                  .starts_with("Koan.notPermitted"));
  EXPECT_TRUE(
      PlanErrorOf(Viewport(0, 900, 2), std::nullopt).starts_with("Koan."));
}

TEST(KoanCliScreenshotTest, UsersTabIsTheWholeViewport) {
  ShotPlanResult result = PlanShot(Viewport(1440, 900, 2), ShotSize::kMicro,
                                   std::nullopt, 0, "", /*may_clip=*/false);
  ASSERT_TRUE(result.plan);
  EXPECT_FALSE(result.plan->clip);
  EXPECT_EQ(result.plan->image_width, 2880);
  EXPECT_EQ(result.plan->image_height, 1800);
  EXPECT_DOUBLE_EQ(result.plan->css_per_pixel, 0.5);
}

TEST(KoanCliScreenshotTest, ReadsTheViewport) {
  std::optional<ViewportMetrics> retina = ReadViewportMetrics(Dict(R"({
      "cssVisualViewport": {"pageX": 0, "pageY": 50.5, "clientWidth": 1440,
                            "clientHeight": 900, "zoom": 1, "scale": 1},
      "visualViewport": {"clientWidth": 2880, "clientHeight": 1800}})"));
  ASSERT_TRUE(retina);
  EXPECT_DOUBLE_EQ(retina->page_y, 50.5);
  EXPECT_DOUBLE_EQ(retina->width, 1440);
  EXPECT_DOUBLE_EQ(retina->device_scale, 2);
  EXPECT_DOUBLE_EQ(retina->zoom, 1);
  // A rounded size: the ratio snaps to the scale it means.
  std::optional<ViewportMetrics> rounded = ReadViewportMetrics(Dict(R"({
      "cssVisualViewport": {"clientWidth": 1439.5, "clientHeight": 900},
      "visualViewport": {"clientWidth": 2879}})"));
  ASSERT_TRUE(rounded);
  EXPECT_DOUBLE_EQ(rounded->device_scale, 2);
  // Zoomed: the device scale without the zoom.
  std::optional<ViewportMetrics> zoomed = ReadViewportMetrics(Dict(R"({
      "cssVisualViewport": {"clientWidth": 1152, "clientHeight": 720,
                            "zoom": 1.25},
      "visualViewport": {"clientWidth": 2880}})"));
  ASSERT_TRUE(zoomed);
  EXPECT_DOUBLE_EQ(zoomed->device_scale, 2);
  EXPECT_DOUBLE_EQ(zoomed->zoom, 1.25);
  // No device size: 1x.
  std::optional<ViewportMetrics> plain = ReadViewportMetrics(Dict(R"({
      "cssLayoutViewport": {"clientWidth": 800, "clientHeight": 600}})"));
  ASSERT_TRUE(plain);
  EXPECT_DOUBLE_EQ(plain->device_scale, 1);
  EXPECT_FALSE(ReadViewportMetrics(Dict(R"({
      "cssVisualViewport": {"clientWidth": 0, "clientHeight": 600}})")));
  EXPECT_FALSE(ReadViewportMetrics(Dict("{}")));
}

TEST(KoanCliScreenshotTest, QuadsBounds) {
  std::optional<CssRect> box = QuadsBounds(Dict(R"({"quads": [
      [10, 20, 110, 20, 110, 70, 10, 70],
      [5, 80, 50, 80, 50, 90.5, 5, 90.5]]})"));
  ASSERT_TRUE(box);
  EXPECT_DOUBLE_EQ(box->x, 5);
  EXPECT_DOUBLE_EQ(box->y, 20);
  EXPECT_DOUBLE_EQ(box->width, 105);
  EXPECT_DOUBLE_EQ(box->height, 70.5);
  EXPECT_FALSE(QuadsBounds(Dict(R"({"quads": []})")));
  EXPECT_FALSE(QuadsBounds(Dict("{}")));
}

TEST(KoanCliScreenshotTest, PngSize) {
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G',  '\r', '\n', 0x1a, '\n',
                              0,    0,   0,   13,   'I',  'H',  'D',  'R',
                              0,    0,   2,   0x80, 0,    0,    1,    0x90};
  std::optional<std::pair<int, int>> size = PngSize(png);
  ASSERT_TRUE(size);
  EXPECT_EQ(size->first, 640);
  EXPECT_EQ(size->second, 400);
  png[1] = 'J';
  EXPECT_FALSE(PngSize(png));
  const std::vector<uint8_t> short_file(10, 0);
  EXPECT_FALSE(PngSize(short_file));
}

TEST(KoanCliScreenshotTest, TheLine) {
  EXPECT_EQ(ShotLine("/tmp/x.png", 640, 400, 2.25),
            "saved /tmp/x.png (640\xc3\x97"
            "400, ~330 image tokens, 1 px = 2.25 CSS px)");
  EXPECT_EQ(ShotLine("a.png", 320, 200, 4.5),
            "saved a.png (320\xc3\x97"
            "200, ~82 image tokens, 1 px = 4.5 CSS px)");
  EXPECT_EQ(ShotLine("a.png", 2880, 1800, 0.5),
            "saved a.png (2880\xc3\x97"
            "1800, ~4780 image tokens, 1 px = 0.5 CSS px)");
  EXPECT_EQ(ShotLine("a.png", 1440, 900, 1),
            "saved a.png (1440\xc3\x97"
            "900, ~1650 image tokens, 1 px = 1 CSS px)");
  base::DictValue json = ShotJson("a.png", 640, 400, 2.25);
  EXPECT_EQ(*json.FindString("path"), "a.png");
  EXPECT_EQ(json.FindInt("width"), 640);
  EXPECT_EQ(json.FindInt("height"), 400);
  EXPECT_EQ(json.FindInt("imageTokens"), 327);
  EXPECT_EQ(json.FindDouble("cssPxPerPx"), 2.25);
}

TEST(KoanCliScreenshotTest, ParseClip) {
  std::string error;
  std::optional<ClipSpec> ref = ParseClip("@12", &error);
  ASSERT_TRUE(ref);
  EXPECT_EQ(ref->target, "@12");
  EXPECT_FALSE(ref->rect);
  std::optional<ClipSpec> locator =
      ParseClip("loc:role=button[name=\"Go\"]", &error);
  ASSERT_TRUE(locator);
  EXPECT_EQ(locator->target, "loc:role=button[name=\"Go\"]");
  std::optional<ClipSpec> rect = ParseClip("10, 20.5,300,40", &error);
  ASSERT_TRUE(rect);
  ASSERT_TRUE(rect->rect);
  EXPECT_DOUBLE_EQ(rect->rect->y, 20.5);
  EXPECT_DOUBLE_EQ(rect->rect->width, 300);
  for (const char* bad :
       {"@x", "@0", "10,20,0,40", "10,20,30", "a,b,c,d", "-1,0,10,10",
        "1,2,3,4,5", "", "button", "0,0,20000,10"}) {
    EXPECT_FALSE(ParseClip(bad, &error)) << bad;
    EXPECT_FALSE(error.empty()) << bad;
  }
}

// --- Arguments ---------------------------------------------------------------

Command Parse(std::vector<std::string> args) {
  ParseResult result = ParseArgs(args);
  EXPECT_TRUE(result.command) << result.error;
  return result.command.value_or(Command());
}

std::string ErrorOf(std::vector<std::string> args) {
  ParseResult result = ParseArgs(args);
  EXPECT_FALSE(result.command);
  return result.error;
}

TEST(KoanCliScreenshotTest, ScreenshotArguments) {
  Command plain = Parse({"screenshot", "abcd"});
  EXPECT_FALSE(plain.shot_size);
  EXPECT_EQ(plain.clip, "");
  Command mini = Parse({"screenshot", "abcd", "--size", "mini", "-o", "x.png"});
  EXPECT_EQ(mini.shot_size, ShotSize::kMini);
  EXPECT_EQ(mini.output, "x.png");
  EXPECT_EQ(Parse({"screenshot", "abcd", "--size=full"}).shot_size,
            ShotSize::kFull);
  Command clip = Parse({"screenshot", "abcd", "--clip", "@12"});
  EXPECT_EQ(clip.clip, "@12");
  EXPECT_FALSE(clip.shot_size);
  EXPECT_EQ(
      Parse({"screenshot", "abcd", "--clip=0,0,100,50", "--size", "micro"})
          .clip,
      "0,0,100,50");

  EXPECT_EQ(ErrorOf({"screenshot", "abcd", "--size", "huge"}),
            "--size takes micro, mini, normal or full");
  EXPECT_EQ(ErrorOf({"screenshot", "abcd", "--size"}),
            "--size takes micro, mini, normal or full");
  EXPECT_TRUE(ErrorOf({"screenshot", "abcd", "--clip", "here"})
                  .starts_with("--clip takes @ref"));
  EXPECT_EQ(ErrorOf({"snapshot", "abcd", "--size", "mini"}),
            "--size and --clip are koan screenshot's");
  EXPECT_TRUE(ErrorOf({"act", "abcd", "click", "@1", "--clip", "@2"})
                  .starts_with("--size and --clip are koan screenshot's; "
                               "koan act takes --screenshot"));
  EXPECT_EQ(ErrorOf({"screenshot", "abcd", "--screenshot", "mini"}),
            "--screenshot is koan act's; koan screenshot takes --size");
}

TEST(KoanCliScreenshotTest, ActScreenshotArguments) {
  Command act = Parse({"act", "abcd", "click", "@4", "--screenshot", "micro"});
  EXPECT_EQ(act.kind, CommandKind::kAct);
  EXPECT_TRUE(act.screenshot_after);
  EXPECT_EQ(act.shot_size, ShotSize::kMicro);
  EXPECT_FALSE(act.snapshot_after);
  EXPECT_EQ(act.steps.size(), 1u);

  // With the snapshot and its diff; -o names the file, after the steps or
  // before them.
  Command both = Parse({"act", "abcd", "fill", "@3", "Ada", "--snapshot",
                        "--diff", "--screenshot=mini", "-o", "after.png"});
  EXPECT_TRUE(both.snapshot_after);
  EXPECT_TRUE(both.diff);
  EXPECT_EQ(both.shot_size, ShotSize::kMini);
  EXPECT_EQ(both.output, "after.png");
  EXPECT_EQ(both.steps.size(), 1u);
  Command first = Parse({"act", "abcd", "-o", "a.png", "press", "Enter",
                         "--screenshot", "normal"});
  EXPECT_EQ(first.output, "a.png");
  EXPECT_EQ(first.steps.size(), 1u);
  EXPECT_EQ(first.shot_size, ShotSize::kNormal);
  // A fill's text that is "-o" is the text.
  Command text = Parse({"act", "abcd", "fill", "@3", "-o"});
  EXPECT_EQ(text.steps.front().text, "-o");

  const std::string sizes =
      "--screenshot takes micro, mini or normal (koan screenshot <tab> "
      "--size full for a full one)";
  EXPECT_EQ(ErrorOf({"act", "abcd", "click", "@4", "--screenshot", "full"}),
            sizes);
  EXPECT_EQ(ErrorOf({"act", "abcd", "click", "@4", "--screenshot", "big"}),
            sizes);
  EXPECT_EQ(ErrorOf({"act", "abcd", "click", "@4", "--screenshot"}), sizes);
  EXPECT_EQ(ErrorOf({"act", "abcd", "click", "@4", "--screenshot", "micro",
                     "--screenshot", "mini"}),
            "--screenshot is given once");
  EXPECT_EQ(ErrorOf({"snapshot", "abcd", "--screenshot", "micro"}),
            "--screenshot is koan act's; koan screenshot takes --size");
}

TEST(KoanCliScreenshotTest, HelpSaysWhatItCosts) {
  std::optional<std::string> help = HelpText("screenshot");
  ASSERT_TRUE(help);
  for (const char* said :
       {"--size normal", "--size mini", "--size micro", "--size full", "--clip",
        "image tokens", "JPEG or\nWebP would save bytes, not tokens"}) {
    EXPECT_NE(help->find(said), std::string::npos) << said;
  }
  EXPECT_NE(HelpText("act")->find("--screenshot micro|mini|normal"),
            std::string::npos);
}

// The guide's rules are what keep small screenshots from costing accuracy.
TEST(KoanCliScreenshotTest, TheGuideSaysWhenToUseEachSize) {
  const std::string guide = SkillGuide("");
  for (const char* needed : {
           "Read and act from the snapshot",
           "never from text\n     you read in a micro or mini image",
           "Use micro or mini only to check state",
           "Use --clip for detail",
           "Use a normal screenshot before anything that depends on where",
           "If a small image is unclear, take a clip or a normal one",
           "--size micro   320x200, about 80 tokens",
           "--size mini    640x400, about 330 tokens",
           "about 1,650 tokens",
           "up to about 4,800",
           "--screenshot micro",
           "JPEG or WebP save bytes, not tokens",
       }) {
    EXPECT_NE(guide.find(needed), std::string::npos) << needed;
  }
}

}  // namespace
}  // namespace koan::cli
