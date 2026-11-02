// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/color/koan_color_mixer.h"

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"

namespace koan {

namespace {

constexpr SkColor kToolbarText = SkColorSetRGB(0x12, 0x34, 0x56);
constexpr SkColor kFrame = SkColorSetRGB(0xAA, 0xBB, 0xCC);

// A provider with Koan's mixer on top of stand-ins for the Chrome colours it
// reads.
class KoanColorMixerTest : public testing::TestWithParam<bool> {
 public:
  bool dark() const { return GetParam(); }

  // Fills `provider_`.
  void Build() {
    ui::ColorMixer& chrome = provider_.AddMixer();
    chrome[kColorToolbarText] = {kToolbarText};
    chrome[kColorToolbar] = {SK_ColorYELLOW};
    chrome[kColorToolbarContentAreaSeparator] = {SK_ColorRED};
    chrome[kColorTabBackgroundActiveFrameActive] = {SK_ColorMAGENTA};
    chrome[ui::kColorFrameActive] = {kFrame};
    chrome[ui::kColorSysPrimary] = {SK_ColorWHITE};
    chrome[ui::kColorSysOnPrimary] = {SK_ColorBLACK};
    ui::ColorProviderKey key;
    key.color_mode = dark() ? ui::ColorProviderKey::ColorMode::kDark
                            : ui::ColorProviderKey::ColorMode::kLight;
    AddKoanColorMixer(&provider_, key);
    provider_.GenerateColorMapForTesting();
  }

  ui::ColorProvider provider_;
};

// SkAlpha of `opacity`, the way the mixer rounds it.
constexpr SkAlpha Alpha(float opacity) {
  return static_cast<SkAlpha>(opacity * SK_AlphaOPAQUE);
}

void ExpectRgb(SkColor expected, SkColor actual) {
  EXPECT_EQ(SkColorGetR(expected), SkColorGetR(actual));
  EXPECT_EQ(SkColorGetG(expected), SkColorGetG(actual));
  EXPECT_EQ(SkColorGetB(expected), SkColorGetB(actual));
}

}  // namespace

TEST_P(KoanColorMixerTest, SidebarTabs) {
  Build();

  // Selected: white at 80% (18% in dark mode). Hovered: the text colour at
  // 11% in both.
  const SkColor selected = provider_.GetColor(kColorKoanSidebarTabSelected);
  ExpectRgb(SK_ColorWHITE, selected);
  EXPECT_EQ(Alpha(dark() ? 0.18f : 0.8f), SkColorGetA(selected));

  const SkColor hovered = provider_.GetColor(kColorKoanSidebarTabHovered);
  ExpectRgb(kToolbarText, hovered);
  EXPECT_EQ(Alpha(0.11f), SkColorGetA(hovered));
}

TEST_P(KoanColorMixerTest, ColorsDerivedFromTheToolbarTextKeepItsHue) {
  Build();
  const struct {
    ui::ColorId id;
    float light_opacity;
    float dark_opacity;
  } kCases[] = {
      {kColorKoanSidebarRowHovered, 0.07f, 0.07f},
      {kColorKoanSidebarRowPressed, 0.1f, 0.1f},
      {kColorKoanSidebarNewTabForeground, 0.7f, 0.7f},
      {kColorKoanSidebarSeparator, 0.1f, 0.1f},
      {kColorKoanSidebarClearForeground, 0.5f, 0.5f},
      {kColorKoanPinnedTabResetHovered, 0.1f, 0.1f},
      {kColorKoanPinnedTabResetPressed, 0.2f, 0.2f},
      {kColorKoanPinnedTabSublabel, 0.6f, 0.6f},
      {kColorKoanEssentialsTile, 0.08f, 0.15f},
      {kColorKoanSpaceButtonHovered, 0.08f, 0.15f},
      {kColorKoanSpaceDot, 0.4f, 0.4f},
      {kColorKoanSpaceFormField, 0.05f, 0.05f},
      {kColorKoanSpaceFormFieldHovered, 0.1f, 0.1f},
      {kColorKoanSpaceFormDescription, 0.4f, 0.4f},
      {kColorKoanSpaceIconBorder, 0.5f, 0.5f},
  };
  for (const auto& test_case : kCases) {
    SCOPED_TRACE(test_case.id);
    const SkColor color = provider_.GetColor(test_case.id);
    ExpectRgb(kToolbarText, color);
    EXPECT_EQ(Alpha(dark() ? test_case.dark_opacity : test_case.light_opacity),
              SkColorGetA(color));
  }
}

TEST_P(KoanColorMixerTest, PlainAliases) {
  Build();
  EXPECT_EQ(kToolbarText,
            provider_.GetColor(kColorKoanSidebarClearForegroundHovered));
  EXPECT_EQ(kToolbarText, provider_.GetColor(kColorKoanSpaceForeground));
  EXPECT_EQ(SK_ColorMAGENTA, provider_.GetColor(kColorKoanUrlPill));
  EXPECT_EQ(provider_.GetColor(kColorKoanSidebarTabHovered),
            provider_.GetColor(kColorKoanSpaceHeaderHovered));
}

TEST_P(KoanColorMixerTest, PinnedTabSlash) {
  Build();
  const SkColor slash = provider_.GetColor(kColorKoanPinnedTabSlash);
  // Zen's light-dark(rgba(66, 61, 61, 0.3), rgba(255, 255, 255, 0.3)).
  ExpectRgb(dark() ? SK_ColorWHITE : SkColorSetRGB(66, 61, 61), slash);
  EXPECT_EQ(Alpha(0.3f), SkColorGetA(slash));
}

TEST_P(KoanColorMixerTest, EssentialsTiles) {
  Build();
  const SkColor selected = provider_.GetColor(kColorKoanEssentialsTileSelected);
  ExpectRgb(SK_ColorWHITE, selected);
  EXPECT_EQ(Alpha(dark() ? 0.2f : 0.85f), SkColorGetA(selected));

  // Hovered: black at 10%; in dark mode the selected colour.
  const SkColor hovered = provider_.GetColor(kColorKoanEssentialsTileHovered);
  if (dark()) {
    EXPECT_EQ(selected, hovered);
  } else {
    ExpectRgb(SK_ColorBLACK, hovered);
    EXPECT_EQ(Alpha(0.1f), SkColorGetA(hovered));
  }
}

TEST_P(KoanColorMixerTest, ToastsFadeTowardsNearBlack) {
  Build();
  const SkColor top = provider_.GetColor(kColorKoanToastBackgroundTop);
  const SkColor bottom = provider_.GetColor(kColorKoanToastBackgroundBottom);
  EXPECT_EQ(SK_ColorWHITE, top);
  // 20% of #0f0f0f over white: (15 * 51 + 255 * 204) / 255 = 207.
  EXPECT_NEAR(207, SkColorGetR(bottom), 1);
  EXPECT_NEAR(207, SkColorGetG(bottom), 1);
  EXPECT_NEAR(207, SkColorGetB(bottom), 1);
  EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(bottom));

  EXPECT_EQ(SK_ColorBLACK, provider_.GetColor(kColorKoanToastForeground));
  const SkColor key = provider_.GetColor(kColorKoanToastKeyBackground);
  ExpectRgb(SK_ColorBLACK, key);
  EXPECT_EQ(Alpha(dark() ? 0.1f : 0.4f), SkColorGetA(key));
}

TEST_P(KoanColorMixerTest, GlanceConfirmButton) {
  Build();
  EXPECT_EQ(SkColorSetRGB(220, 53, 69),
            provider_.GetColor(kColorKoanGlanceButtonConfirm));
  EXPECT_EQ(SK_ColorWHITE,
            provider_.GetColor(kColorKoanGlanceButtonConfirmIcon));
}

TEST_P(KoanColorMixerTest, SidebarPaintsOnTheFrameColour) {
  Build();
  EXPECT_EQ(kFrame, provider_.GetColor(kColorToolbar));
  EXPECT_EQ(SK_ColorTRANSPARENT,
            provider_.GetColor(kColorToolbarContentAreaSeparator));
  // The Glance backdrop is the toolbar colour at 70%.
  const SkColor backdrop = provider_.GetColor(kColorKoanGlanceBackdrop);
  ExpectRgb(kFrame, backdrop);
  EXPECT_EQ(Alpha(0.7f), SkColorGetA(backdrop));
}

TEST_P(KoanColorMixerTest, SidebarOffLeavesChromesSurfaces) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(features::kKoanSidebar);
  Build();
  EXPECT_EQ(SK_ColorYELLOW, provider_.GetColor(kColorToolbar));
  EXPECT_EQ(SK_ColorRED, provider_.GetColor(kColorToolbarContentAreaSeparator));
  // Koan's own colours stay.
  EXPECT_EQ(kToolbarText, provider_.GetColor(kColorKoanSpaceForeground));
}

INSTANTIATE_TEST_SUITE_P(All,
                         KoanColorMixerTest,
                         testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           return info.param ? "Dark" : "Light";
                         });

}  // namespace koan
