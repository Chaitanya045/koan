// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Builds a ColorProvider the way the browser does, for every look: Zen's and
// shadcn's, light and dark, with a pasted theme, a Space's accent colour and
// high contrast. Building it resolves every colour that Koan's mixers define,
// so a colour recipe cycle (the Mac launch crash of 6de98f7e: two mixers whose
// recipes named each other's ids, an endless recursion in
// chrome://theme/colors.css) is a crash here, without a browser. Chromium's
// own Chrome mixers (//chrome/browser/ui/color, which link the whole theme
// service) are not in this provider: a stand-in defines every Chrome colour
// Koan's mixers can read, so scripts/e2e/colors.mjs checks the cycles that go
// through those in the real browser.

#include <string>
#include <tuple>

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/color/color_id.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_color_mixer.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/color/koan_tokens.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_utils.h"
#include "ui/color/color_recipe.h"
#include "ui/gfx/color_palette.h"

namespace koan {

namespace {

enum class Variant {
  kDefault,
  // A theme pasted into the settings page.
  kPastedTheme,
  // A Space's accent colour, which reaches a window through its colour key.
  kSpaceAccent,
  kHighContrast,
};

using Params = std::tuple<look::Style, bool /*dark*/, Variant>;

constexpr SkColor kStandIn = SkColorSetRGB(0x80, 0x80, 0x80);

class KoanLookProvidersTest : public testing::TestWithParam<Params> {
 public:
  void SetUp() override {
    ui::SetCoreColorMixerOverride(&AddKoanCoreColorMixer);
    look::Look look;
    look.style = std::get<0>(GetParam());
    look.mode = dark() ? look::Mode::kDark : look::Mode::kLight;
    if (std::get<2>(GetParam()) == Variant::kPastedTheme) {
      look.custom = look::ParseThemeCss(
          ":root { --background: #fdf6e3; --primary: oklch(0.6 0.2 30); "
          "--radius: 0.25rem; } "
          ".dark { --background: #002b36; --card: hsl(200 30% 15%); }");
      ASSERT_TRUE(look.custom.has_value());
    }
    look::SetLook(look);
  }

  void TearDown() override {
    ui::SetCoreColorMixerOverride(nullptr);
    look::SetLook(look::Look());
    look::SetZenAccent(std::nullopt);
  }

  bool dark() const { return std::get<1>(GetParam()); }

  ui::ColorProviderKey Key() const {
    ui::ColorProviderKey key;
    key.color_mode = dark() ? ui::ColorProviderKey::ColorMode::kDark
                            : ui::ColorProviderKey::ColorMode::kLight;
    switch (std::get<2>(GetParam())) {
      case Variant::kSpaceAccent:
        key.user_color = SkColorSetRGB(0xc8, 0x50, 0x28);
        key.user_color_source = ui::ColorProviderKey::UserColorSource::kAccent;
        break;
      case Variant::kHighContrast:
        key.contrast_mode = ui::ColorProviderKey::ContrastMode::kHigh;
        break;
      case Variant::kDefault:
      case Variant::kPastedTheme:
        break;
    }
    return key;
  }

  // The pipeline of ChromeColorProviderManager: the ui mixers (Koan's early
  // mixer among them), Chrome's, then Koan's.
  void Build() {
    const ui::ColorProviderKey key = Key();
    ui::AddColorMixers(&provider_, key);
    ui::ColorMixer& chrome = provider_.AddMixer();
    for (int id = color::kComponentsColorsStart; id < kChromeColorsEnd; ++id) {
      chrome[id] = {kStandIn};
    }
    AddKoanColorMixer(&provider_, key);
    // Resolves every colour a mixer defines; a cycle never returns.
    provider_.GenerateColorMapForTesting();
  }

  ui::ColorProvider provider_;
};

}  // namespace

TEST_P(KoanLookProvidersTest, BuildsWithoutACycle) {
  Build();
  EXPECT_FALSE(provider_.color_map_for_testing().empty());
}

TEST_P(KoanLookProvidersTest, EveryKoanColourIsDefined) {
  Build();
  const ui::ColorProvider::ColorMap& colors = provider_.color_map_for_testing();
  for (int id = kKoanColorsStart; id < kKoanColorsEnd; ++id) {
    EXPECT_TRUE(colors.contains(id)) << ui::ColorIdName(id);
  }
}

TEST_P(KoanLookProvidersTest, NoKoanColourIsThePlaceholder) {
  Build();
  // A colour no mixer can build is gfx::kPlaceholderColor, pure red: the
  // Library's Space cards in the shadcn look, once.
  for (int id = kKoanColorsStart; id < kKoanColorsEnd; ++id) {
    EXPECT_NE(gfx::kPlaceholderColor, provider_.GetColor(id))
        << ui::ColorIdName(id);
  }
}

TEST_P(KoanLookProvidersTest, TheLookRestylesTheSystemColours) {
  Build();
  // The early mixer sets Chromium's system colours from the look's tokens,
  // so what Chromium derives from them (menus, bubbles, dialogs, WebUI pages)
  // follows the look.
  const look::TokenSet& tokens = look::CurrentTokens(dark());
  EXPECT_EQ(tokens[look::Token::kBackground],
            provider_.GetColor(ui::kColorSysSurface));
  EXPECT_EQ(tokens[look::Token::kForeground],
            provider_.GetColor(ui::kColorSysOnSurface));
  EXPECT_EQ(tokens[look::Token::kMutedForeground],
            provider_.GetColor(ui::kColorSysOnSurfaceVariant));
}

TEST_P(KoanLookProvidersTest, SidebarOffBuildsToo) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(features::kKoanSidebar);
  Build();
  EXPECT_FALSE(provider_.color_map_for_testing().empty());
}

std::string ParamName(const testing::TestParamInfo<Params>& info) {
  static const char* const kVariants[] = {"Default", "PastedTheme",
                                          "SpaceAccent", "HighContrast"};
  return std::string(look::StyleToString(std::get<0>(info.param))) + "_" +
         (std::get<1>(info.param) ? "Dark" : "Light") + "_" +
         kVariants[static_cast<int>(std::get<2>(info.param))];
}

INSTANTIATE_TEST_SUITE_P(
    All,
    KoanLookProvidersTest,
    testing::Combine(testing::Values(look::Style::kZen, look::Style::kShadcn),
                     testing::Bool(),
                     testing::Values(Variant::kDefault,
                                     Variant::kPastedTheme,
                                     Variant::kSpaceAccent,
                                     Variant::kHighContrast)),
    ParamName);

}  // namespace koan
