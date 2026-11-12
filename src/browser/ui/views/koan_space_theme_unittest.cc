// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A light Space theme on a translucent window in a dark system scheme
// (#291): Zen's mode decision stays, and Koan paints the theme opaque enough
// for the light mode's text to read over the dark blur.

#include "koan/browser/ui/views/koan_space_theme.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::theme {
namespace {

// Koan's dark window colour (kColorKoanThemeWindow), which the blur shows
// over a dark desktop.
constexpr Rgb kDarkWindow = {0x17, 0x17, 0x17};

// A macOS window in the dark scheme, translucent (glass).
Environment DarkGlass() {
  Environment env;
  env.dark = true;
  env.transparent = true;
  env.macos = true;
  env.min_opacity = 0.3;
  env.window = kDarkWindow;
  env.default_background = {0x13, 0x13, 0x13};
  env.tint_base = {23, 23, 26};
  return env;
}

SpaceTheme OneColor(const Rgb& rgb, double opacity) {
  SpaceTheme theme;
  ThemeColor color;
  color.c = rgb;
  color.is_primary = true;
  theme.colors.push_back(color);
  theme.opacity = opacity;
  return theme;
}

// The light green of the report.
constexpr Rgb kLightGreen = {170, 230, 160};

// The alpha of a one-colour theme's paint.
float PaintedAlpha(const Environment& env, const SpaceTheme& theme) {
  const Background background = GetBackground(env, theme);
  return background.layers.empty() || background.layers[0].stops.empty()
             ? -1
             : background.layers[0].stops[0].color.fA;
}

TEST(KoanSpaceThemeContrastTest, LightThemeStaysLightAsInZen) {
  // Zen's shouldBeDarkMode() on a transparent window: the accent is the
  // background, and a light green gives light mode (dark text).
  const Environment env = DarkGlass();
  const Appearance appearance = GetAppearance(env, OneColor(kLightGreen, 0.3));
  EXPECT_TRUE(appearance.explicit_mode);
  EXPECT_FALSE(appearance.dark);
}

TEST(KoanSpaceThemeContrastTest, LowOpacityOverTheDarkBlurIsUnreadable) {
  // What Zen paints: the light green at 30% over the dark blur, mid-grey,
  // where the dark text falls short of AA.
  EXPECT_LT(LightTextContrastOverBlur(DarkGlass(), kLightGreen, 0.3),
            kLightTextMinContrast);
}

TEST(KoanSpaceThemeContrastTest, ContrastGrowsWithOpacity) {
  const Environment env = DarkGlass();
  double last = 0;
  for (double opacity = 0; opacity <= 0.8; opacity += 0.05) {
    const double contrast =
        LightTextContrastOverBlur(env, kLightGreen, opacity);
    EXPECT_GE(contrast, last) << opacity;
    last = contrast;
  }
}

TEST(KoanSpaceThemeContrastTest, FloorIsTheLeastOpacityThatReads) {
  const Environment env = DarkGlass();
  const double floor = LightThemeOpacityFloor(env, OneColor(kLightGreen, 0.3));
  EXPECT_GT(floor, 0.3);
  EXPECT_LE(floor, 0.8);
  EXPECT_GE(LightTextContrastOverBlur(env, kLightGreen, floor),
            kLightTextMinContrast);
  EXPECT_LT(LightTextContrastOverBlur(env, kLightGreen, floor - 0.01),
            kLightTextMinContrast);
}

TEST(KoanSpaceThemeContrastTest, PaintsTheThemeAtTheFloor) {
  const Environment env = DarkGlass();
  const SpaceTheme theme = OneColor(kLightGreen, 0.3);
  const double floor = LightThemeOpacityFloor(env, theme);
  EXPECT_NEAR(PaintedAlpha(env, theme), floor, 1.0 / 255);
  const Background background = GetBackground(env, theme);
  EXPECT_NEAR(background.opacity_floor, floor, 1e-6);
  EXPECT_NE(DescribeBackground(background).find(" / floor "),
            std::string::npos);
  // At or above the floor, the slider's opacity, as in Zen.
  const SpaceTheme opaque_enough = OneColor(kLightGreen, 0.75);
  EXPECT_NEAR(PaintedAlpha(env, opaque_enough), 0.75, 1.0 / 255);
  EXPECT_EQ(GetBackground(env, opaque_enough).opacity_floor, 0);
}

TEST(KoanSpaceThemeContrastTest, NoFloorWhereZenAlreadyReads) {
  const SpaceTheme light = OneColor(kLightGreen, 0.3);
  // A light system: the blur behind is light.
  Environment light_system = DarkGlass();
  light_system.dark = false;
  light_system.window = {0xe2, 0xe2, 0xe2};
  light_system.default_background = {0xe9, 0xe9, 0xe9};
  light_system.tint_base = {240, 240, 244};
  EXPECT_EQ(LightThemeOpacityFloor(light_system, light), 0);
  // An opaque window: the theme is blended into the window's colour, and the
  // mode is decided on that.
  Environment opaque = DarkGlass();
  opaque.transparent = false;
  EXPECT_EQ(LightThemeOpacityFloor(opaque, light), 0);
  // A dark theme: light text, which the dark blur only helps.
  const SpaceTheme navy = OneColor({20, 30, 70}, 0.3);
  ASSERT_TRUE(GetAppearance(DarkGlass(), navy).dark);
  EXPECT_EQ(LightThemeOpacityFloor(DarkGlass(), navy), 0);
  // The default theme (no colours).
  EXPECT_EQ(LightThemeOpacityFloor(DarkGlass(), SpaceTheme()), 0);
}

TEST(KoanSpaceThemeContrastTest, CustomColorsArePaintedAsGiven) {
  SpaceTheme theme;
  ThemeColor custom;
  custom.is_custom = true;
  custom.custom = SkColorSetARGB(0x40, 0xf0, 0xf0, 0xf0);
  theme.colors = {custom, custom};
  theme.opacity = 0.3;
  EXPECT_EQ(LightThemeOpacityFloor(DarkGlass(), theme), 0);
}

}  // namespace
}  // namespace koan::theme
