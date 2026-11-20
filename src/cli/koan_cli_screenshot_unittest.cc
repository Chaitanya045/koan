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

TEST(KoanCliScreenshotTest, SizeNames) {
  for (ShotSize size : {ShotSize::kMicro, ShotSize::kMini, ShotSize::kNormal,
                        ShotSize::kFull}) {
    EXPECT_EQ(ParseShotSize(ShotSizeName(size)), size);
  }
  EXPECT_FALSE(ParseShotSize("large"));
  EXPECT_FALSE(ParseShotSize(""));
}

// What koan screenshot asks the browser for, at each size.
TEST(KoanCliScreenshotTest, CaptureParams) {
  base::DictValue normal = CaptureParams("T1", std::nullopt, std::nullopt, 0);
  EXPECT_EQ(*normal.FindString("targetId"), "T1");
  EXPECT_EQ(normal.FindInt("longEdge"), kNormalLongEdge);
  EXPECT_EQ(normal.FindDouble("scale"), 1);
  EXPECT_EQ(*normal.FindString("format"), "png");
  EXPECT_FALSE(normal.contains("clip"));
  EXPECT_EQ(CaptureParams("T1", ShotSize::kMicro, std::nullopt, 0)
                .FindInt("longEdge"),
            kMicroLongEdge);
  EXPECT_EQ(
      CaptureParams("T1", ShotSize::kMini, std::nullopt, 0).FindDouble("scale"),
      1);
  // Full: as sharp as the display draws (the browser caps the scale).
  base::DictValue full = CaptureParams("T1", ShotSize::kFull, std::nullopt, 0);
  EXPECT_EQ(full.FindInt("longEdge"), kFullLongEdge);
  EXPECT_EQ(full.FindDouble("scale"), agent::kMaxCaptureScale);
  // A clip is sharp by default, with the margin around an element.
  base::DictValue clip =
      CaptureParams("T1", std::nullopt, CssRect{100, 200, 50, 20}, kClipMargin);
  EXPECT_EQ(clip.FindInt("longEdge"), kFullLongEdge);
  EXPECT_EQ(clip.FindDouble("scale"), agent::kMaxCaptureScale);
  const base::DictValue* box = clip.FindDict("clip");
  ASSERT_TRUE(box);
  EXPECT_EQ(box->FindDouble("x"), 92);
  EXPECT_EQ(box->FindDouble("y"), 192);
  EXPECT_EQ(box->FindDouble("width"), 66);
  EXPECT_EQ(box->FindDouble("height"), 36);
  // With a size, that size's.
  EXPECT_EQ(CaptureParams("T1", ShotSize::kMini, CssRect{0, 0, 10, 10}, 0)
                .FindInt("longEdge"),
            kMiniLongEdge);
}

TEST(KoanCliScreenshotTest, ClipRefusals) {
  EXPECT_TRUE(ClipRefusal("Koan.offScreen", "@12")
                  .starts_with("Koan.offScreen: @12 is outside the viewport"));
  EXPECT_TRUE(ClipRefusal("Koan.emptyElement", "@12")
                  .starts_with("Koan.emptyElement: @12 has no size"));
  EXPECT_EQ(ClipRefusal("Koan.noSurface", "@12"), "");
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
