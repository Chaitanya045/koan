// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   Zen's theme stylesheets (colour values)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/color/koan_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace koan {

namespace {

constexpr SkAlpha Alpha(float opacity) {
  return static_cast<SkAlpha>(opacity * SK_AlphaOPAQUE);
}

// Koan's own colours (koan_color_id.h), Zen's for now: shades of the toolbar
// text on the sidebar (zen-tabs/vertical-tabs.css, zen-theme.css), the URL
// bar's (zen-omnibox.css) and the accent colour for toasts (zen-popup.css).
void AddKoanColors(ui::ColorMixer& mixer, const ui::ColorProviderKey& key) {
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;

  // Tab rows (Zen's --tab-background-color-selected, Firefox's
  // --tab-hover-background-color): selected white at 80% (18% in dark mode),
  // hovered the text colour at 11%.
  mixer[kColorKoanSidebarTabSelected] = {
      SkColorSetA(SK_ColorWHITE, Alpha(dark ? 0.18f : 0.8f))};
  mixer[kColorKoanSidebarTabHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.11f));

  // Zen's toolbar button hover and press backgrounds, New Tab at 70%.
  mixer[kColorKoanSidebarRowHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.07f));
  mixer[kColorKoanSidebarRowPressed] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.1f));
  mixer[kColorKoanSidebarNewTabForeground] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.7f));
  mixer[kColorKoanSidebarSeparator] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.1f));
  mixer[kColorKoanSidebarClearForeground] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.5f));
  mixer[kColorKoanSidebarClearForegroundHovered] = {kColorToolbarText};

  // A pinned tab's reset button: the slash in Zen's light-dark(rgba(66, 61,
  // 61, 0.3), rgba(255, 255, 255, 0.3)), the button's background the text
  // colour at 10% (20% pressed), the sublabel the text at 60%.
  mixer[kColorKoanPinnedTabSlash] = {
      dark ? SkColorSetA(SK_ColorWHITE, Alpha(0.3f))
           : SkColorSetARGB(Alpha(0.3f), 66, 61, 61)};
  mixer[kColorKoanPinnedTabResetHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.1f));
  mixer[kColorKoanPinnedTabResetPressed] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.2f));
  mixer[kColorKoanPinnedTabSublabel] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.6f));

  // Essentials tiles: the text colour at 8% (15% in dark mode), on hover
  // black at 10% (the selected colour in dark mode), selected white at 85%
  // (20% in dark mode).
  mixer[kColorKoanEssentialsTile] =
      ui::SetAlpha(kColorToolbarText, Alpha(dark ? 0.15f : 0.08f));
  mixer[kColorKoanEssentialsTileSelected] = {
      SkColorSetA(SK_ColorWHITE, Alpha(dark ? 0.2f : 0.85f))};
  mixer[kColorKoanEssentialsTileHovered] =
      dark ? ui::ColorTransform(kColorKoanEssentialsTileSelected)
           : ui::ColorTransform(SkColorSetA(SK_ColorBLACK, Alpha(0.1f)));

  // The URL pill on the tab colour; the floating URL bar's long shadow
  // (box-shadow 0 30px 140px -15px) black at 80% (60% in dark mode).
  mixer[kColorKoanUrlPill] = {kColorTabBackgroundActiveFrameActive};
  mixer[kColorKoanFloatingUrlBarShadow] =
      ui::SetAlpha(ui::kColorShadowBase, Alpha(dark ? 0.6f : 0.8f));

  // Toasts: the accent colour fading to 20% darker (towards Zen's #0f0f0f),
  // the text in the accent's contrast colour, the key cap that colour at 40%
  // (10% in dark mode).
  mixer[kColorKoanToastBackgroundTop] = {ui::kColorSysPrimary};
  mixer[kColorKoanToastBackgroundBottom] =
      ui::AlphaBlend(SkColorSetRGB(0x0f, 0x0f, 0x0f),
                     kColorKoanToastBackgroundTop, Alpha(0.2f));
  mixer[kColorKoanToastForeground] = {ui::kColorSysOnPrimary};
  mixer[kColorKoanToastKeyBackground] =
      ui::SetAlpha(kColorKoanToastForeground, Alpha(dark ? 0.1f : 0.4f));

  // Glance (zen-glance.css): Zen fades the page behind the card to 30% over
  // the window background, i.e. that background at 70% over the page; the
  // card has --zen-big-shadow (black at 24%); the buttons are near-black
  // (near-white in dark mode) with accent-coloured icons; the close button
  // asking to confirm is rgb(220, 53, 69) with white.
  mixer[kColorKoanGlanceBackdrop] = ui::SetAlpha(kColorToolbar, Alpha(0.7f));
  mixer[kColorKoanGlanceCardShadow] = {
      ui::kColorShadowValueAmbientShadowElevationFour};
  mixer[kColorKoanGlanceButton] = {ui::kColorSysInverseSurface};
  mixer[kColorKoanGlanceButtonIcon] = {ui::kColorSysInversePrimary};
  mixer[kColorKoanGlanceButtonConfirm] = {SkColorSetRGB(220, 53, 69)};
  mixer[kColorKoanGlanceButtonConfirmIcon] = {SK_ColorWHITE};

  // Space themes: Zen's --zen-themed-toolbar-bg-transparent, its background
  // without a theme (getGradient()) and getToolbarModifiedBaseRaw().
  mixer[kColorKoanThemeWindow] = {dark ? SkColorSetRGB(0x17, 0x17, 0x17)
                                       : SkColorSetRGB(0xe2, 0xe2, 0xe2)};
  mixer[kColorKoanThemeDefaultBackground] = {
      dark ? SkColorSetRGB(0x13, 0x13, 0x13) : SkColorSetRGB(0xe9, 0xe9, 0xe9)};
  mixer[kColorKoanThemeTintBase] = {dark ? SkColorSetRGB(23, 23, 26)
                                         : SkColorSetRGB(240, 240, 244)};

  // The theme picker, with zen-gradient-generator.css's light-dark() values;
  // the panel is Firefox's --arrowpanel-background.
  const auto light_dark = [dark](SkColor light, SkColor dark_color) {
    return ui::ColorTransform(dark ? dark_color : light);
  };
  mixer[kColorKoanThemePickerBackground] =
      light_dark(SkColorSetRGB(244, 244, 244), SkColorSetRGB(31, 31, 31));
  mixer[kColorKoanThemePickerForeground] =
      light_dark(SkColorSetRGB(50, 50, 55), SK_ColorWHITE);
  mixer[kColorKoanThemePickerField] =
      light_dark(SkColorSetA(SK_ColorBLACK, Alpha(0.05f)),
                 SkColorSetA(SK_ColorWHITE, Alpha(0.03f)));
  mixer[kColorKoanThemePickerFieldGrid] =
      light_dark(SkColorSetA(SK_ColorBLACK, Alpha(0.1f)),
                 SkColorSetA(SK_ColorWHITE, Alpha(0.05f)));
  mixer[kColorKoanThemePickerIcon] =
      light_dark(SkColorSetA(SK_ColorBLACK, Alpha(0.7f)),
                 SkColorSetA(SK_ColorWHITE, Alpha(0.9f)));
  mixer[kColorKoanThemePickerButtonHovered] =
      light_dark(SkColorSetA(SK_ColorBLACK, Alpha(0.1f)),
                 SkColorSetA(SK_ColorWHITE, Alpha(0.1f)));
  mixer[kColorKoanThemePickerDotBorder] = {SK_ColorWHITE};
  mixer[kColorKoanThemePickerWave] =
      light_dark(SkColorSetRGB(90, 90, 90), SkColorSetRGB(161, 161, 161));
  mixer[kColorKoanThemePickerWaveDim] =
      light_dark(SkColorSetARGB(Alpha(0.5f), 77, 77, 77),
                 SkColorSetARGB(Alpha(0.5f), 161, 161, 161));
  mixer[kColorKoanThemePickerTrack] = {kColorKoanThemePickerButtonHovered};
  mixer[kColorKoanThemePickerThumb] = light_dark(SK_ColorBLACK, SK_ColorWHITE);
  mixer[kColorKoanThemePickerDialDot] =
      light_dark(SkColorSetA(SK_ColorBLACK, Alpha(0.3f)),
                 SkColorSetA(SK_ColorWHITE, Alpha(0.3f)));
  mixer[kColorKoanThemePickerDialHandle] = light_dark(
      SkColorSetRGB(0x75, 0x75, 0x75), SkColorSetRGB(0xd1, 0xd1, 0xd1));
  // Zen's --zen-colors-border at 50%.
  mixer[kColorKoanThemePickerDialBorder] =
      light_dark(SkColorSetARGB(Alpha(0.25f), 128, 128, 128),
                 SkColorSetARGB(Alpha(0.5f), 79, 79, 79));
  mixer[kColorKoanThemePickerDialShine] = {kColorKoanThemePickerButtonHovered};
  mixer[kColorKoanThemePickerSwatchShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.1f))};
}

}  // namespace

void AddKoanColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();
  AddKoanColors(mixer, key);

  if (!features::IsSidebarEnabled()) {
    return;
  }

  // Zen draws the sidebar, toolbar and the gutter around the page on one
  // surface: the window frame colour. Only the page itself (a floating card)
  // and the selected tab stand out from it.
  mixer[kColorToolbar] = {ui::kColorFrameActive};

  // No lines between the surfaces.
  mixer[kColorToolbarContentAreaSeparator] = {SK_ColorTRANSPARENT};
  mixer[kColorVerticalTabStripShadow] = {SK_ColorTRANSPARENT};

  // Essentials tiles are filled, not outlined (koan_essentials.cc).
  mixer[kColorVerticalTabPinnedOutline] = {SK_ColorTRANSPARENT};
}

}  // namespace koan
