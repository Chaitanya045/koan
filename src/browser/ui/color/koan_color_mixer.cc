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

  // The Library (zen-library.css): on the window's surface, the side column
  // lightening to white at 8%; everything else shades of the text colour
  // (Zen's color-mix(currentColor N%)): subtitles 60%, the search icon 50%,
  // pills 7% (12% hovered, 20% for an active chip), hovered rows 10%, side
  // tabs 5% hovered and 10% selected, a progress track 12%. Section icons in
  // the accent colour, the selected one filled with it washed 60% towards
  // white (black in dark mode). Space cards on the surface, outlined at 8%;
  // the add button the accent at 50% (70% hovered) over the surface.
  mixer[kColorKoanLibraryBackground] = {kColorToolbar};
  mixer[kColorKoanLibrarySideHighlight] = {
      SkColorSetA(SK_ColorWHITE, Alpha(0.08f))};
  mixer[kColorKoanLibraryForeground] = {kColorToolbarText};
  mixer[kColorKoanLibraryForegroundSecondary] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.6f));
  mixer[kColorKoanLibraryForegroundDim] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.5f));
  mixer[kColorKoanLibraryPill] = ui::SetAlpha(kColorToolbarText, Alpha(0.07f));
  mixer[kColorKoanLibraryPillHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.12f));
  mixer[kColorKoanLibraryChipActive] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.2f));
  mixer[kColorKoanLibraryRowHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.1f));
  mixer[kColorKoanLibraryTabHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.05f));
  mixer[kColorKoanLibraryTabSelected] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.1f));
  mixer[kColorKoanLibraryIcon] = {ui::kColorSysPrimary};
  mixer[kColorKoanLibraryIconFill] = ui::AlphaBlend(
      dark ? SK_ColorBLACK : SK_ColorWHITE, kColorKoanLibraryIcon, Alpha(0.6f));
  mixer[kColorKoanLibraryProgressTrack] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.12f));
  mixer[kColorKoanLibrarySpaceCard] = {kColorToolbar};
  mixer[kColorKoanLibrarySpaceCardBorder] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.08f));
  mixer[kColorKoanLibrarySpaceIconEmpty] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.08f));
  mixer[kColorKoanLibraryAddSpace] =
      ui::AlphaBlend(ui::kColorSysPrimary, kColorToolbar, Alpha(0.5f));
  mixer[kColorKoanLibraryAddSpaceHovered] =
      ui::AlphaBlend(ui::kColorSysPrimary, kColorToolbar, Alpha(0.7f));
  mixer[kColorKoanLibraryAddSpaceForeground] = {SK_ColorWHITE};
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
