// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

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
  // The button: white at 50% (10% in dark mode), its icon the text at 60%.
  mixer[kColorKoanToastButton] = {
      SkColorSetA(SK_ColorWHITE, Alpha(dark ? 0.1f : 0.5f))};
  mixer[kColorKoanToastButtonIcon] =
      ui::SetAlpha(kColorKoanToastForeground, Alpha(0.6f));

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

  // Media cards (zen-theme.css --zen-sidebar-notification-*): the accent at
  // 5% on white (black in dark mode), a 6px shadow black at 10% (30%), an
  // outline only in dark mode (white at 10%); the progress track in the
  // text colour at 15%, the played part the accent mixed 30% towards black
  // (white).
  mixer[kColorKoanMediaCard] = ui::AlphaBlend(
      ui::kColorSysPrimary, dark ? SK_ColorBLACK : SK_ColorWHITE, Alpha(0.05f));
  mixer[kColorKoanMediaCardShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(dark ? 0.3f : 0.1f))};
  mixer[kColorKoanMediaCardOutline] = {
      dark ? SkColorSetA(SK_ColorWHITE, Alpha(0.1f)) : SK_ColorTRANSPARENT};
  mixer[kColorKoanMediaProgressTrack] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.15f));
  mixer[kColorKoanMediaProgress] = ui::AlphaBlend(
      ui::kColorSysPrimary, dark ? SK_ColorWHITE : SK_ColorBLACK, Alpha(0.7f));

  // The download animation (zen-download-*-animation.css): a ring in the
  // hover colour around a circle in the toolbar's text colour, the arrow in
  // the accent, Zen's big shadow (black at 24%); the tile in the accent
  // button colour with a light shadow (black at 10%).
  mixer[kColorKoanDownloadArcRing] =
      ui::AlphaBlend(kColorToolbarText, kColorToolbar, Alpha(0.15f));
  mixer[kColorKoanDownloadArcCircle] = {kColorToolbarText};
  mixer[kColorKoanDownloadArcIcon] = {ui::kColorSysInversePrimary};
  mixer[kColorKoanDownloadArcShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.24f))};
  mixer[kColorKoanDownloadTile] = {ui::kColorSysPrimaryContainer};
  mixer[kColorKoanDownloadTileIcon] = {ui::kColorSysOnPrimaryContainer};
  mixer[kColorKoanDownloadTileShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.1f))};

  // The welcome screen (zen-welcome.css): Koan's calm green gradient where
  // Zen plays a video, white title, Zen's green start button; the pages in
  // Zen's light-dark() greys, the accent buttons in the browser's accent.
  mixer[kColorKoanWelcomeBackdropTop] = {SkColorSetRGB(0x2f, 0x4f, 0x3c)};
  mixer[kColorKoanWelcomeBackdropBottom] = {SkColorSetRGB(0x10, 0x1b, 0x16)};
  mixer[kColorKoanWelcomeTitle] = {SK_ColorWHITE};
  mixer[kColorKoanWelcomeStartButton] = {SkColorSetRGB(62, 115, 62)};
  mixer[kColorKoanWelcomeStartButtonHovered] =
      ui::AlphaBlend(SK_ColorWHITE, kColorKoanWelcomeStartButton, Alpha(0.1f));
  mixer[kColorKoanWelcomeStartButtonIcon] = {SK_ColorWHITE};
  mixer[kColorKoanWelcomeSidebar] = {dark ? SkColorSetRGB(0x1c, 0x1c, 0x1e)
                                          : SK_ColorWHITE};
  mixer[kColorKoanWelcomeContent] = {
      dark ? SkColorSetARGB(0xed, 0x2b, 0x2b, 0x30)
           : SkColorSetARGB(0xf5, 0xf6, 0xf6, 0xf8)};
  mixer[kColorKoanWelcomeSurface] = {kColorKoanWelcomeSidebar};
  mixer[kColorKoanWelcomeTile] = {dark ? SkColorSetRGB(0x33, 0x33, 0x38)
                                       : SkColorSetRGB(0xec, 0xec, 0xee)};
  mixer[kColorKoanWelcomeMockWindow] = {dark ? SkColorSetRGB(0x23, 0x23, 0x26)
                                             : SkColorSetRGB(0xe3, 0xe3, 0xe5)};
  mixer[kColorKoanWelcomeBorder] = {SkColorSetA(
      dark ? SK_ColorWHITE : SK_ColorBLACK, Alpha(dark ? 0.16f : 0.14f))};
  mixer[kColorKoanWelcomeShadow] = {SkColorSetA(SK_ColorBLACK, Alpha(0.05f))};
  mixer[kColorKoanWelcomeText] = {dark ? SK_ColorWHITE
                                       : SkColorSetRGB(0x11, 0x11, 0x11)};
  mixer[kColorKoanWelcomeTextSecondary] =
      ui::SetAlpha(kColorKoanWelcomeText, Alpha(0.6f));
  mixer[kColorKoanWelcomeRadio] =
      ui::SetAlpha(kColorKoanWelcomeText, Alpha(dark ? 0.5f : 0.45f));
  mixer[kColorKoanWelcomeAccent] = {ui::kColorSysPrimary};
  mixer[kColorKoanWelcomeAccentHovered] =
      ui::AlphaBlend(SK_ColorWHITE, kColorKoanWelcomeAccent, Alpha(0.1f));
  mixer[kColorKoanWelcomeOnAccent] = {ui::kColorSysOnPrimary};
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
