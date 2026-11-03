// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/color/koan_tokens.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>

#include "koan/browser/ui/color/koan_oklch.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

namespace koan::look {

namespace {

constexpr SkColor Rgb(uint8_t r, uint8_t g, uint8_t b) {
  return SkColorSetRGB(r, g, b);
}

// Within one step per channel: the CSS the values come from rounds
// differently in places (a browser's oklch() against ours).
void ExpectNear(SkColor expected, SkColor actual) {
  EXPECT_NEAR(SkColorGetR(expected), SkColorGetR(actual), 1);
  EXPECT_NEAR(SkColorGetG(expected), SkColorGetG(actual), 1);
  EXPECT_NEAR(SkColorGetB(expected), SkColorGetB(actual), 1);
  EXPECT_NEAR(SkColorGetA(expected), SkColorGetA(actual), 1);
}

}  // namespace

// --- OKLCH ---

TEST(KoanOklchTest, GreysAndExtremes) {
  EXPECT_EQ(SK_ColorWHITE, OklchToSkColor({1, 0, 0, 1}));
  EXPECT_EQ(SK_ColorBLACK, OklchToSkColor({0, 0, 0, 1}));
  // shadcn's neutral palette (ui.shadcn.com/themes).
  ExpectNear(Rgb(0x0a, 0x0a, 0x0a), OklchToSkColor({0.145f, 0, 0, 1}));
  ExpectNear(Rgb(0x17, 0x17, 0x17), OklchToSkColor({0.205f, 0, 0, 1}));
  ExpectNear(Rgb(0xfa, 0xfa, 0xfa), OklchToSkColor({0.985f, 0, 0, 1}));
}

TEST(KoanOklchTest, OutOfGamutIsMappedNotClipped) {
  // shadcn's destructive colour, oklch(0.577 0.245 27.325), is outside sRGB;
  // browsers map it to Tailwind's red-600, #e7000b.
  ExpectNear(Rgb(0xe7, 0x00, 0x0b), OklchToSkColor({0.577f, 0.245f, 27.325f, 1}));
}

TEST(KoanOklchTest, Alpha) {
  const SkColor color = OklchToSkColor({1, 0, 0, 0.1f});
  EXPECT_EQ(SK_ColorWHITE, SkColorSetA(color, 0xff));
  EXPECT_EQ(26u, SkColorGetA(color));
}

TEST(KoanOklchTest, RoundTripsSrgb) {
  for (SkColor color : {Rgb(2, 41, 84), Rgb(22, 131, 255), Rgb(0x18, 0x17, 0x17),
                        Rgb(0xe5, 0xe5, 0xe5), Rgb(255, 0, 0), Rgb(0, 255, 0),
                        Rgb(0x27, 0x27, 0x2a), Rgb(0xfb, 0xfb, 0xfe)}) {
    SCOPED_TRACE(color);
    ExpectNear(color, OklchToSkColor(SkColorToOklch(color)));
  }
}

TEST(KoanOklchTest, GreyHasNoHue) {
  const OklchColor grey = SkColorToOklch(Rgb(0x80, 0x80, 0x80));
  EXPECT_NEAR(0, grey.c, 1e-3);
  EXPECT_EQ(0, grey.h);
}

TEST(KoanOklchTest, Hsl) {
  // shadcn's pre-v4 dark background, hsl(222.2 84% 4.9%).
  ExpectNear(Rgb(0x02, 0x08, 0x17), HslToSkColor(222.2f, 0.84f, 0.049f, 1));
  EXPECT_EQ(Rgb(255, 0, 0), HslToSkColor(0, 1, 0.5f, 1));
  EXPECT_EQ(Rgb(0, 0, 255), HslToSkColor(240, 1, 0.5f, 1));
  // The hue wraps.
  EXPECT_EQ(HslToSkColor(120, 1, 0.5f, 1), HslToSkColor(480, 1, 0.5f, 1));
  EXPECT_EQ(HslToSkColor(300, 1, 0.5f, 1), HslToSkColor(-60, 1, 0.5f, 1));
}

// --- ParseThemeCss ---

TEST(KoanParseThemeCssTest, ShadcnV4Theme) {
  const std::optional<CustomTheme> theme = ParseThemeCss(R"css(
    :root {
      --radius: 0.5rem;
      --background: oklch(1 0 0);
      --foreground: oklch(0.145 0 0);
      --primary: #ff0000;
      --border: rgb(0 128 255);
      --unknown-token: oklch(0.5 0 0);
    }
    .dark {
      --background: oklch(0.145 0 0);
      --primary: hsl(120, 100%, 50%);
    }
  )css");
  ASSERT_TRUE(theme.has_value());
  auto light = [&](Token token) { return theme->light[static_cast<int>(token)]; };
  auto dark = [&](Token token) { return theme->dark[static_cast<int>(token)]; };

  ASSERT_TRUE(light(Token::kBackground));
  EXPECT_EQ(SK_ColorWHITE, *light(Token::kBackground));
  ASSERT_TRUE(light(Token::kForeground));
  ExpectNear(Rgb(0x0a, 0x0a, 0x0a), *light(Token::kForeground));
  EXPECT_EQ(Rgb(255, 0, 0), light(Token::kPrimary));
  EXPECT_EQ(Rgb(0, 128, 255), light(Token::kBorder));
  // Not set: left to the base colour.
  EXPECT_FALSE(light(Token::kCard));

  ASSERT_TRUE(dark(Token::kBackground));
  ExpectNear(Rgb(0x0a, 0x0a, 0x0a), *dark(Token::kBackground));
  EXPECT_EQ(Rgb(0, 255, 0), dark(Token::kPrimary));
  EXPECT_FALSE(dark(Token::kBorder));

  ASSERT_TRUE(theme->radius.has_value());
  EXPECT_FLOAT_EQ(8, *theme->radius);
}

TEST(KoanParseThemeCssTest, PreV4BareHsl) {
  const std::optional<CustomTheme> theme = ParseThemeCss(
      ":root { --background: 222.2 84% 4.9%; --primary: 210 40% 98%; }");
  ASSERT_TRUE(theme.has_value());
  const auto background = theme->light[static_cast<int>(Token::kBackground)];
  ASSERT_TRUE(background);
  ExpectNear(Rgb(0x02, 0x08, 0x17), *background);
  EXPECT_TRUE(theme->light[static_cast<int>(Token::kPrimary)]);
}

TEST(KoanParseThemeCssTest, HexFormsAndAlpha) {
  const std::optional<CustomTheme> theme = ParseThemeCss(
      ":root { --background: #abc; --foreground: #112233; --border: "
      "#11223380; --input: oklch(1 0 0 / 15%); }");
  ASSERT_TRUE(theme.has_value());
  auto light = [&](Token token) { return theme->light[static_cast<int>(token)]; };
  EXPECT_EQ(Rgb(0xaa, 0xbb, 0xcc), light(Token::kBackground));
  EXPECT_EQ(Rgb(0x11, 0x22, 0x33), light(Token::kForeground));
  ASSERT_TRUE(light(Token::kBorder));
  EXPECT_EQ(0x80u, SkColorGetA(*light(Token::kBorder)));
  ASSERT_TRUE(light(Token::kInput));
  EXPECT_NEAR(38, SkColorGetA(*light(Token::kInput)), 1);
}

TEST(KoanParseThemeCssTest, CommentsImportantAndNesting) {
  const std::optional<CustomTheme> theme = ParseThemeCss(R"css(
    /* a theme */
    @layer base {
      :root { --background: #101010 !important; /* dark */ }
      .dark { --background: #202020; }
    }
  )css");
  ASSERT_TRUE(theme.has_value());
  EXPECT_EQ(Rgb(0x10, 0x10, 0x10),
            theme->light[static_cast<int>(Token::kBackground)]);
  EXPECT_EQ(Rgb(0x20, 0x20, 0x20),
            theme->dark[static_cast<int>(Token::kBackground)]);
}

TEST(KoanParseThemeCssTest, RefusesWhatIsNotATheme) {
  EXPECT_FALSE(ParseThemeCss("").has_value());
  EXPECT_FALSE(ParseThemeCss("hello world").has_value());
  EXPECT_FALSE(ParseThemeCss(":root { color: red; }").has_value());
  EXPECT_FALSE(ParseThemeCss(":root { --unknown: #fff; }").has_value());
  // var() and unreadable values are ignored; with nothing else, no theme.
  EXPECT_FALSE(
      ParseThemeCss(":root { --background: var(--other); }").has_value());
  EXPECT_FALSE(ParseThemeCss(":root { --background: not-a-colour; }")
                   .has_value());
  // Declarations outside a block are not tokens.
  EXPECT_FALSE(ParseThemeCss("--background: #fff;").has_value());
}

TEST(KoanParseThemeCssTest, SizeCap) {
  std::string css = ":root { --background: #fff; }";
  EXPECT_TRUE(ParseThemeCss(css).has_value());
  css += std::string(64 * 1024, ' ');
  EXPECT_FALSE(ParseThemeCss(css).has_value());
}

TEST(KoanParseThemeCssTest, RadiusUnitsAndLimit) {
  auto radius = [](const char* value) {
    const auto theme = ParseThemeCss(std::string(":root { --radius: ") + value +
                                     "; --background: #fff; }");
    return theme ? theme->radius : std::nullopt;
  };
  ASSERT_TRUE(radius("0.625rem"));
  EXPECT_FLOAT_EQ(10, *radius("0.625rem"));
  ASSERT_TRUE(radius("12px"));
  EXPECT_FLOAT_EQ(12, *radius("12px"));
  ASSERT_TRUE(radius("0"));
  EXPECT_FLOAT_EQ(0, *radius("0"));
  // A huge radius is capped.
  ASSERT_TRUE(radius("500px"));
  EXPECT_LE(*radius("500px"), 64);
}

// --- Zen's colours ---

TEST(KoanZenTest, PrimaryFromTheSystemAccent) {
  // macOS's default blue gives the navy measured on Zen 1.24b in dark mode,
  // and a blue in light mode.
  const SkColor blue = Rgb(0, 122, 255);
  EXPECT_EQ(Rgb(2, 41, 84), ZenDefaultPrimary(blue, /*dark=*/true));
  EXPECT_EQ(Rgb(22, 131, 255), ZenDefaultPrimary(blue, /*dark=*/false));
  // Without an accent, the same default blue.
  EXPECT_EQ(ZenDefaultPrimary(blue, true), ZenDefaultPrimary(std::nullopt, true));
  EXPECT_EQ(ZenDefaultPrimary(blue, false),
            ZenDefaultPrimary(std::nullopt, false));
  // Another accent gives another primary colour.
  EXPECT_NE(ZenDefaultPrimary(Rgb(255, 59, 48), true),
            ZenDefaultPrimary(blue, true));
}

TEST(KoanZenTest, ThemePrimaryIsUnchangedInDarkMode) {
  const SkColor dominant = Rgb(200, 80, 40);
  EXPECT_EQ(dominant, ZenPrimaryForTheme(dominant, /*dark=*/true));
  EXPECT_NE(dominant, ZenPrimaryForTheme(dominant, /*dark=*/false));
}

TEST(KoanZenTest, AccentClampsOklchLightness) {
  const SkColor dark_primary = ZenDefaultPrimary(std::nullopt, true);
  const ZenAccent dark = ComputeZenAccent(dark_primary, true);
  EXPECT_EQ(Rgb(0x4c, 0x74, 0xa5), dark.button_background);
  EXPECT_EQ(Rgb(0x78, 0xa1, 0xd5), dark.text);

  const SkColor light_primary = ZenDefaultPrimary(std::nullopt, false);
  const ZenAccent light = ComputeZenAccent(light_primary, false);
  EXPECT_EQ(Rgb(0x04, 0x7b, 0xf7), light.button_background);
  EXPECT_EQ(Rgb(0x04, 0x7b, 0xf7), light.text);

  // The button's lightness is clamped to 0.55-0.7 in dark mode and its text's
  // to 0.7-0.85, so the text is never darker than the button.
  EXPECT_GE(SkColorToOklch(dark.text).l, SkColorToOklch(dark.button_background).l);
  EXPECT_GE(SkColorToOklch(dark.button_background).l, 0.55f - 0.01f);
  EXPECT_GE(SkColorToOklch(dark.text).l, 0.7f - 0.01f);
}

TEST(KoanZenTest, TabColors) {
  {
    const ZenTabColors dark = ComputeZenTabColors(Rgb(2, 41, 84), true);
    // The primary blended 20% into white, at 90%.
    EXPECT_EQ(0xe6u, SkColorGetA(dark.text));
    EXPECT_EQ(Rgb(0xcc, 0xd4, 0xdd), SkColorSetA(dark.text, 0xff));
    // color-mix(in srgb, rgba(255 255 255 / 0.18) 95%, primary): the alpha is
    // 0.18 * 0.95 + 0.05 = 0.221.
    EXPECT_EQ(56u, SkColorGetA(dark.selected));
    EXPECT_EQ(Rgb(0xc6, 0xcf, 0xd8), SkColorSetA(dark.selected, 0xff));
    // The branding colour #e2e2e2 at 7% and 10%.
    EXPECT_EQ(Rgb(0xe2, 0xe2, 0xe2), SkColorSetA(dark.hovered, 0xff));
    EXPECT_EQ(18u, SkColorGetA(dark.hovered));
    EXPECT_EQ(26u, SkColorGetA(dark.pressed));
  }
  {
    const ZenTabColors light = ComputeZenTabColors(Rgb(22, 131, 255), false);
    EXPECT_EQ(0xe6u, SkColorGetA(light.text));
    // 0.8 * 0.98 + 0.02 = 0.804.
    EXPECT_EQ(205u, SkColorGetA(light.selected));
    EXPECT_EQ(Rgb(0x10, 0x10, 0x10), SkColorSetA(light.hovered, 0xff));
    EXPECT_EQ(18u, SkColorGetA(light.hovered));
    EXPECT_EQ(26u, SkColorGetA(light.pressed));
  }
}

TEST(KoanZenTest, TokensIgnoreTheShadcnChoices) {
  Look zen;
  zen.style = Style::kZen;
  Look decorated = zen;
  decorated.base = BaseColor::kSlate;
  decorated.accent = Accent::kRose;
  decorated.custom = ParseThemeCss(":root { --background: #ff00ff; }");
  ASSERT_TRUE(decorated.custom.has_value());
  for (bool dark : {true, false}) {
    const TokenSet a = ResolveTokens(zen, dark);
    const TokenSet b = ResolveTokens(decorated, dark);
    EXPECT_EQ(a.colors, b.colors);
    EXPECT_EQ(a.radius, b.radius);
  }
}

TEST(KoanZenTest, FrameAndSurfaces) {
  Look zen;
  zen.style = Style::kZen;
  const TokenSet dark = ResolveTokens(zen, true);
  EXPECT_EQ(Rgb(0x18, 0x17, 0x17), dark[Token::kSidebar]);
  EXPECT_EQ(Rgb(0x10, 0x10, 0x10), dark[Token::kBackground]);
  EXPECT_EQ(Rgb(0xfb, 0xfb, 0xfe), dark[Token::kForeground]);
  const TokenSet light = ResolveTokens(zen, false);
  EXPECT_EQ(Rgb(0xe5, 0xe5, 0xe5), light[Token::kSidebar]);
  EXPECT_EQ(Rgb(0x15, 0x14, 0x1a), light[Token::kForeground]);
}

TEST(KoanZenTest, TheSystemAccentReachesThePrimaryColour) {
  Look blue;
  blue.style = Style::kZen;
  blue.zen_accent = Rgb(0, 122, 255);
  Look red = blue;
  red.zen_accent = Rgb(255, 59, 48);
  EXPECT_NE(ResolveTokens(blue, true)[Token::kPrimary],
            ResolveTokens(red, true)[Token::kPrimary]);
  // The default accent is macOS's blue.
  Look none;
  none.style = Style::kZen;
  EXPECT_EQ(ResolveTokens(blue, true).colors, ResolveTokens(none, true).colors);
}

// --- shadcn's tokens ---

TEST(KoanShadcnTest, BlackBase) {
  Look look;
  look.style = Style::kShadcn;
  look.base = BaseColor::kBlack;
  const TokenSet dark = ResolveTokens(look, true);
  ExpectNear(Rgb(0x09, 0x09, 0x0b), dark[Token::kBackground]);
  ExpectNear(Rgb(0x18, 0x18, 0x1b), dark[Token::kCard]);
  ExpectNear(Rgb(0x18, 0x18, 0x1b), dark[Token::kPopover]);
  ExpectNear(Rgb(0x27, 0x27, 0x2a), dark[Token::kBorder]);
  EXPECT_FLOAT_EQ(kDefaultRadius, dark.radius);
}

TEST(KoanShadcnTest, EveryBaseResolvesOpaqueSurfacesInBothModes) {
  for (BaseColor base :
       {BaseColor::kBlack, BaseColor::kNeutral, BaseColor::kZinc,
        BaseColor::kSlate, BaseColor::kStone, BaseColor::kGray}) {
    for (bool dark : {true, false}) {
      Look look;
      look.style = Style::kShadcn;
      look.base = base;
      const TokenSet tokens = ResolveTokens(look, dark);
      SCOPED_TRACE(BaseColorToString(base));
      for (Token token : {Token::kBackground, Token::kForeground, Token::kCard,
                          Token::kPopover, Token::kPrimary, Token::kSidebar}) {
        EXPECT_EQ(255u, SkColorGetA(tokens[token]))
            << TokenCssName(token);
      }
    }
  }
}

TEST(KoanShadcnTest, AccentChangesThePrimaryColourOnly) {
  Look plain;
  plain.style = Style::kShadcn;
  Look blue = plain;
  blue.accent = Accent::kBlue;
  for (bool dark : {true, false}) {
    const TokenSet a = ResolveTokens(plain, dark);
    const TokenSet b = ResolveTokens(blue, dark);
    EXPECT_NE(a[Token::kPrimary], b[Token::kPrimary]);
    EXPECT_EQ(a[Token::kBackground], b[Token::kBackground]);
    EXPECT_EQ(a[Token::kCard], b[Token::kCard]);
    EXPECT_EQ(a[Token::kBorder], b[Token::kBorder]);
  }
}

TEST(KoanShadcnTest, PastedThemeWinsOverBaseAndAccent) {
  Look look;
  look.style = Style::kShadcn;
  look.accent = Accent::kGreen;
  look.custom = ParseThemeCss(
      ":root { --primary: #123456; --radius: 4px; } "
      ".dark { --primary: #654321; }");
  ASSERT_TRUE(look.custom.has_value());
  EXPECT_EQ(Rgb(0x12, 0x34, 0x56), ResolveTokens(look, false)[Token::kPrimary]);
  EXPECT_EQ(Rgb(0x65, 0x43, 0x21), ResolveTokens(look, true)[Token::kPrimary]);
  EXPECT_FLOAT_EQ(4, ResolveTokens(look, true).radius);
  // What the theme leaves out stays as in the base.
  Look base = look;
  base.custom.reset();
  base.accent = Accent::kNone;
  EXPECT_EQ(ResolveTokens(base, true)[Token::kCard],
            ResolveTokens(look, true)[Token::kCard]);
}

TEST(KoanShadcnTest, RadiusScale) {
  TokenSet tokens;
  tokens.radius = 10;
  EXPECT_EQ(6, RadiusSm(tokens));
  EXPECT_EQ(8, RadiusMd(tokens));
  EXPECT_EQ(10, RadiusLg(tokens));
  EXPECT_EQ(14, RadiusXl(tokens));
  // Never below zero.
  tokens.radius = 0;
  EXPECT_EQ(0, RadiusSm(tokens));
  EXPECT_EQ(0, RadiusMd(tokens));
  EXPECT_EQ(0, RadiusLg(tokens));
  EXPECT_EQ(4, RadiusXl(tokens));
}

// --- Names ---

TEST(KoanTokensTest, TokensToCss) {
  Look look;
  look.style = Style::kShadcn;
  const std::string css = TokensToCss(ResolveTokens(look, true));
  EXPECT_NE(std::string::npos, css.find("--background: #"));
  EXPECT_NE(std::string::npos, css.find("--sidebar-ring: "));
  EXPECT_NE(std::string::npos, css.find("--radius: 10px;"));
  // One line per token and the radius.
  EXPECT_EQ(static_cast<size_t>(kTokenCount + 1),
            static_cast<size_t>(std::count(css.begin(), css.end(), '\n')));
}

TEST(KoanTokensTest, NamesRoundTrip) {
  for (Style style : {Style::kShadcn, Style::kZen}) {
    EXPECT_EQ(style, StyleFromString(StyleToString(style)));
  }
  for (Mode mode : {Mode::kAuto, Mode::kLight, Mode::kDark}) {
    EXPECT_EQ(mode, ModeFromString(ModeToString(mode)));
  }
  for (BaseColor base :
       {BaseColor::kBlack, BaseColor::kNeutral, BaseColor::kZinc,
        BaseColor::kSlate, BaseColor::kStone, BaseColor::kGray}) {
    EXPECT_EQ(base, BaseColorFromString(BaseColorToString(base)));
  }
  for (Accent accent :
       {Accent::kNone, Accent::kBlue, Accent::kGreen, Accent::kOrange,
        Accent::kRed, Accent::kRose, Accent::kViolet, Accent::kYellow}) {
    EXPECT_EQ(accent, AccentFromString(AccentToString(accent)));
  }
  EXPECT_FALSE(StyleFromString("material").has_value());
  EXPECT_FALSE(ModeFromString("").has_value());
  EXPECT_FALSE(BaseColorFromString("Black").has_value());
  EXPECT_FALSE(AccentFromString("pink").has_value());
}

TEST(KoanTokensTest, CssNamesAreUniqueAndKebabCase) {
  std::set<std::string> names;
  for (int i = 0; i < kTokenCount; ++i) {
    const std::string name(TokenCssName(static_cast<Token>(i)));
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(names.insert(name).second) << name;
    EXPECT_EQ(std::string::npos, name.find_first_not_of("abcdefghijklmnopqrstuvwxyz-"))
        << name;
  }
}

}  // namespace koan::look
