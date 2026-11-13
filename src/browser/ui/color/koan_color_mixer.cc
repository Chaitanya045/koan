// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   Zen's theme stylesheets (colour values)
//   zen-workspaces.css (the Spaces' colour values)
//   zen-library.css (the Library's colour values)
//   zen-omnibox.css, zen-popup.css (the floating URL bar card's colours)
//   zen-single-components.css (the status bubble's colours)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/color/koan_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "ui/gfx/color_utils.h"

namespace koan {

namespace {

constexpr SkAlpha Alpha(float opacity) {
  return static_cast<SkAlpha>(opacity * SK_AlphaOPAQUE);
}

// Koan's own colours (koan_color_id.h) in the Zen style: shades of the toolbar
// text on the sidebar (zen-tabs/vertical-tabs.css, zen-theme.css), the URL
// bar's (zen-omnibox.css) and the accent colour for toasts (zen-popup.css).
void AddZenColors(ui::ColorMixer& mixer, const ui::ColorProviderKey& key) {
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;

  // Tab rows, the toolbar text and the hover and press backgrounds derive
  // from the primary colour (koan_shadcn_color_mixer.cc); New Tab is at 70%.
  AddZenTabColors(mixer, key);
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

  // Essentials tiles (and the URL pill): --zen-toolbar-element-bg,
  // color-mix(in oklch, var(--toolbox-textcolor) 8%, transparent) (15% in
  // dark mode). The mix keeps the text colour's own alpha (90%), so the tint
  // is 13.5% in dark mode (zen-mac-spec.md 2A: oklch(.866 .015 251 / .135)).
  // On hover black at 10% (the selected colour in dark mode), selected white
  // at 85% (20% in dark mode).
  mixer[kColorKoanEssentialsTile] = ui::AlphaBlend(
      kColorToolbarText, SK_ColorTRANSPARENT, Alpha(dark ? 0.15f : 0.08f));
  mixer[kColorKoanEssentialsTileSelected] = {
      SkColorSetA(SK_ColorWHITE, Alpha(dark ? 0.2f : 0.85f))};
  mixer[kColorKoanEssentialsTileHovered] =
      dark ? ui::ColorTransform(kColorKoanEssentialsTileSelected)
           : ui::ColorTransform(SkColorSetA(SK_ColorBLACK, Alpha(0.1f)));

  // The audio badge (Firefox's .tab-icon-overlay, tabs.css): opaque, the
  // toolbox's background (the selected tab's background over it on the
  // selected tile), the toolbox text; hovered and pressed, Firefox's
  // --button-background-color-{hover,active} over it: rgba(207, 207, 216,
  // 0.66 / 1) (0.2 / 0.4 in dark mode). Its outline is --button-border-color,
  // transparent.
  mixer[kColorKoanEssentialsAudioBadge] = {kColorToolbar};
  mixer[kColorKoanEssentialsAudioBadgeSelected] =
      ui::AlphaBlend(SK_ColorWHITE, kColorToolbar, Alpha(dark ? 0.2f : 0.85f));
  mixer[kColorKoanEssentialsAudioBadgeHovered] = {
      SkColorSetARGB(Alpha(dark ? 0.2f : 0.66f), 207, 207, 216)};
  mixer[kColorKoanEssentialsAudioBadgePressed] = {
      SkColorSetARGB(Alpha(dark ? 0.4f : 1.0f), 207, 207, 216)};
  mixer[kColorKoanEssentialsAudioBadgeForeground] = {kColorToolbarText};
  mixer[kColorKoanEssentialsAudioBadgeBorder] = {SK_ColorTRANSPARENT};

  // The Essentials promo (zen-essentials-promo.css): --zen-primary-color at
  // 60% (40% with a tab over it), bordered with it, the sidebar's text, a
  // shadow of black at 10%.
  mixer[kColorKoanEssentialsPromo] = {
      SkColorSetA(ZenPrimaryColor(key), Alpha(0.6f))};
  mixer[kColorKoanEssentialsPromoDragOver] = {
      SkColorSetA(ZenPrimaryColor(key), Alpha(0.4f))};
  mixer[kColorKoanEssentialsPromoBorder] = {ZenPrimaryColor(key)};
  mixer[kColorKoanEssentialsPromoForeground] = {kColorToolbarText};
  mixer[kColorKoanEssentialsPromoShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.1f))};
  // The tab drop indicator (#zen-drag-indicator, vertical-tabs.css):
  // --zen-primary-color mixed half and half with black at 85% (white at 95%
  // in dark mode).
  mixer[kColorKoanTabDropIndicator] = {
      color_utils::AlphaBlend(ZenPrimaryColor(key),
                              dark ? SkColorSetA(SK_ColorWHITE, Alpha(0.95f))
                                   : SkColorSetA(SK_ColorBLACK, Alpha(0.85f)),
                              0.5f)};

  // Koan's tab drag (ZenDragAndDrop.js): #zen-dragover-background is
  // --zen-primary-color; the drag image is the selected row, opaque over the
  // toolbar, with its text; the count dot red with white text.
  mixer[kColorKoanTabDragOverBackground] = {ZenPrimaryColor(key)};
  mixer[kColorKoanTabDragImage] = {
      ui::GetResultingPaintColor(kColorKoanSidebarTabSelected, kColorToolbar)};
  mixer[kColorKoanTabDragImageForeground] = {kColorToolbarText};
  mixer[kColorKoanTabDragCount] = {SK_ColorRED};
  mixer[kColorKoanTabDragCountForeground] = {SK_ColorWHITE};
  // Split by drag (zen-split-view.css): the fake tab the primary button's
  // colour at 60%; the card white (black in dark mode) with the other for its
  // text; the fake pane white at 10% in a --zen-primary-color border.
  mixer[kColorKoanSplitFakeTab] = {
      SkColorSetA(ZenPrimaryColor(key), Alpha(0.6f))};
  mixer[kColorKoanTabDragCard] = {dark ? SK_ColorBLACK : SK_ColorWHITE};
  mixer[kColorKoanTabDragCardForeground] = {dark ? SK_ColorWHITE
                                                 : SK_ColorBLACK};
  mixer[kColorKoanSplitFakePane] = {SkColorSetA(SK_ColorWHITE, Alpha(0.1f))};
  mixer[kColorKoanSplitFakePaneBorder] = {ZenPrimaryColor(key)};

  // The URL pill (zen-omnibox.css .urlbar-background): the Essentials tiles'
  // tint (--zen-toolbar-element-bg), on hover --zen-toolbar-element-bg-hover
  // (black at 8%, white at 10% in dark mode); its text the toolbar text at
  // 59% (measured on Zen 1.24b, zen-mac-spec.md 2.4). The floating URL bar's
  // long shadow (box-shadow 0 30px 140px -15px) black at 80% (60% in dark
  // mode).
  mixer[kColorKoanUrlPill] = {kColorKoanEssentialsTile};
  mixer[kColorKoanUrlPillHovered] = {
      dark ? SkColorSetA(SK_ColorWHITE, Alpha(0.1f))
           : SkColorSetA(SK_ColorBLACK, Alpha(0.08f))};
  mixer[kColorKoanUrlPillForeground] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.59f));
  // The floating URL bar (zen-omnibox.css, #urlbar[breakout-extend]): white
  // or rgb(24, 24, 24), a glass pane (zen.theme.acrylic-urlbar) at 93% / 92%
  // of white or rgb(20, 20, 20) with a 40px blur, and a 0.5px outline,
  // rgba(0, 0, 0, 0.2) or rgb(75, 75, 75). Its urls and the title separator
  // (.urlbarView-url) are #4f4f4f or #aaa.
  mixer[kColorKoanFloatingUrlBarBackground] = {FloatingUrlBarCardColor(key)};
  mixer[kColorKoanFloatingUrlBarGlass] = {
      dark ? SkColorSetARGB(Alpha(0.92f), 20, 20, 20)
           : SkColorSetA(SK_ColorWHITE, Alpha(0.93f))};
  mixer[kColorKoanFloatingUrlBarBorder] = {
      dark ? SkColorSetRGB(75, 75, 75)
           : SkColorSetA(SK_ColorBLACK, Alpha(0.2f))};
  // The input row (zen-omnibox.css): no focus ring, a thin divider (10%), the
  // placeholder at 54% of the text colour.
  mixer[kColorKoanFloatingUrlBarFocusRing] = {SK_ColorTRANSPARENT};
  mixer[kColorKoanFloatingUrlBarDivider] = {
      SkColorSetA(dark ? SK_ColorWHITE : SK_ColorBLACK, Alpha(0.1f))};
  mixer[kColorKoanFloatingUrlBarPlaceholder] = {
      dark ? SkColorSetA(SkColorSetRGB(0xfb, 0xfb, 0xfe), Alpha(0.54f))
           : SkColorSetA(SK_ColorBLACK, Alpha(0.54f))};
  mixer[kColorOmniboxResultsUrl] = {dark ? SkColorSetRGB(0xaa, 0xaa, 0xaa)
                                         : SkColorSetRGB(0x4f, 0x4f, 0x4f)};
  mixer[kColorOmniboxResultsUrlSelected] = {kColorOmniboxResultsUrl};
  AddZenSuggestionColors(mixer, key);
  mixer[kColorKoanFloatingUrlBarShadow] =
      ui::SetAlpha(ui::kColorShadowBase, Alpha(dark ? 0.6f : 0.8f));

  // Spaces: the header in the toolbar's text colour with the tabs' hover;
  // the bottom bar's buttons with --zen-toolbar-element-bg (the text colour
  // at 8%, 15% in dark mode) and a dot for no icon at 40%; the form's fields
  // at 5% (10% hovered), the description at 40% and the icon's dashed box
  // at 50%; the emoji picker's hover at 10%.
  mixer[kColorKoanSpaceForeground] = {kColorToolbarText};
  mixer[kColorKoanSpaceHeaderHovered] = {kColorKoanSidebarTabHovered};
  mixer[kColorKoanSpaceButtonHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(dark ? 0.15f : 0.08f));
  mixer[kColorKoanSpaceDot] = ui::SetAlpha(kColorToolbarText, Alpha(0.4f));
  mixer[kColorKoanSpaceFormField] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.05f));
  mixer[kColorKoanSpaceFormFieldHovered] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.1f));
  mixer[kColorKoanSpaceFormDescription] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.4f));
  mixer[kColorKoanSpaceIconBorder] =
      ui::SetAlpha(kColorToolbarText, Alpha(0.5f));
  mixer[kColorKoanEmojiHovered] =
      ui::SetAlpha(ui::kColorLabelForeground, Alpha(0.1f));

  // The add-Space swipe (zen-single-components.css
  // .zen-swipe-add-space-*): the badge color-mix(--zen-primary-color, white
  // 15%) with a shadow of black at 20%, its ring and plus --toolbox-textcolor,
  // the plus --zen-primary-color once ready; the glow --zen-accent-button-color
  // at 70%.
  mixer[kColorKoanSpaceAddSwipeBadge] = {
      color_utils::AlphaBlend(SK_ColorWHITE, ZenPrimaryColor(key), 0.15f)};
  mixer[kColorKoanSpaceAddSwipeRing] = {kColorToolbarText};
  mixer[kColorKoanSpaceAddSwipeIcon] = {kColorToolbarText};
  mixer[kColorKoanSpaceAddSwipeIconReady] = {ZenPrimaryColor(key)};
  mixer[kColorKoanSpaceAddSwipeShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.2f))};
  mixer[kColorKoanSpaceAddSwipeGlow] = {SkColorSetA(
      look::ComputeZenAccent(ZenPrimaryColor(key), dark).button_background,
      Alpha(0.7f))};

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
  // card has --zen-big-shadow (black at 24%); the buttons are
  // color-mix(light-dark(rgb(24, 24, 24), rgb(231, 231, 231)) 96%,
  // --zen-primary-color) with the icon in --zen-primary-color, hovered
  // light-dark(rgb(41, 41, 41), rgb(204, 204, 204)); the close
  // button asking to confirm is rgb(220, 53, 69) with white. (Not the
  // kColorSysInverse* pair: in the Zen look's dark mode both are its text
  // colour, which drew blank discs, #138.)
  const SkColor primary = ZenPrimaryColor(key);
  mixer[kColorKoanGlanceBackdrop] = ui::SetAlpha(kColorToolbar, Alpha(0.7f));
  mixer[kColorKoanGlanceCardShadow] = {
      ui::kColorShadowValueAmbientShadowElevationFour};
  mixer[kColorKoanGlanceButton] = {color_utils::AlphaBlend(
      primary, dark ? SkColorSetRGB(231, 231, 231) : SkColorSetRGB(24, 24, 24),
      0.04f)};
  mixer[kColorKoanGlanceButtonHover] = {dark ? SkColorSetRGB(204, 204, 204)
                                             : SkColorSetRGB(41, 41, 41)};
  mixer[kColorKoanGlanceButtonIcon] = {primary};
  mixer[kColorKoanGlanceButtonConfirm] = {SkColorSetRGB(220, 53, 69)};
  mixer[kColorKoanGlanceButtonConfirmIcon] = {SK_ColorWHITE};

  // The status bubble (zen-single-components.css, Zen's styled status
  // panel): the accent mixed 70% towards white (black in dark mode), a
  // hairline in black at 15% (rgb(225, 225, 225) at 15%), the text in black
  // (white) at 80%. Zen doesn't dim it in an inactive window.
  const SkColor status_bubble = color_utils::AlphaBlend(
      dark ? SK_ColorBLACK : SK_ColorWHITE, primary, 0.7f);
  const SkColor status_text = color_utils::AlphaBlend(
      dark ? SK_ColorWHITE : SK_ColorBLACK, status_bubble, 0.8f);
  mixer[kColorStatusBubbleBackgroundFrameActive] = {status_bubble};
  mixer[kColorStatusBubbleBackgroundFrameInactive] = {status_bubble};
  mixer[kColorStatusBubbleForegroundFrameActive] = {status_text};
  mixer[kColorStatusBubbleForegroundFrameInactive] = {status_text};
  mixer[kColorStatusBubbleShadow] = {
      dark ? SkColorSetARGB(Alpha(0.15f), 225, 225, 225)
           : SkColorSetA(SK_ColorBLACK, Alpha(0.15f))};

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
  mixer[kColorKoanDownloadArcIcon] = {primary};
  mixer[kColorKoanDownloadArcShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.24f))};
  mixer[kColorKoanDownloadTile] = {ui::kColorSysPrimaryContainer};
  mixer[kColorKoanDownloadTileIcon] = {ui::kColorSysOnPrimaryContainer};
  mixer[kColorKoanDownloadTileShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.1f))};

  // The page-load progress bar (#zen-loading-progress-bar): color-mix(in
  // srgb, --zen-primary-color, light-dark(black, white) at 50% 70%), i.e.
  // the primary colour at 30% and the half-transparent black (white) at 70%:
  // 65% opaque, 46% of the way from that black (white) to the primary. The
  // long load's track is black at 10% (white in dark mode).
  const SkColor loading_mix = dark ? SK_ColorWHITE : SK_ColorBLACK;
  mixer[kColorKoanLoadingBar] = {
      SkColorSetA(color_utils::AlphaBlend(primary, loading_mix, 0.3f / 0.65f),
                  Alpha(0.65f))};
  mixer[kColorKoanLoadingBarTrack] = {SkColorSetA(loading_mix, Alpha(0.1f))};

  // Split view (zen-split-view.css): the pane header is
  // --zen-active-split-outline-color, the active pane's outline (here
  // Chromium's), with contrast-color() glyphs at 80% and a shadow of black
  // at 10%. Rearranging dims the panes over the window background (as
  // Glance's backdrop); the drop zone is --zen-colors-secondary at 30%, and
  // the drag image is drawn on white (Zen's canvas).
  mixer[kColorKoanSplitHeader] = {kColorMultiContentsViewActiveContentOutline};
  mixer[kColorKoanSplitHeaderIcon] = ui::SetAlpha(
      ui::GetColorWithMaxContrast(kColorKoanSplitHeader), Alpha(0.8f));
  mixer[kColorKoanSplitHeaderShadow] = {
      SkColorSetA(SK_ColorBLACK, Alpha(0.1f))};
  mixer[kColorKoanSplitRearrangeDim] = {kColorToolbar};
  const SkColor zen_secondary =
      dark ? color_utils::AlphaBlend(primary, SkColorSetRGB(0x10, 0x10, 0x10),
                                     0.3f)
           : color_utils::AlphaBlend(
                 color_utils::AlphaBlend(primary, SK_ColorBLACK, 0.5f),
                 SK_ColorWHITE, 0.2f);
  mixer[kColorKoanSplitDropZone] = {SkColorSetA(zen_secondary, Alpha(0.3f))};
  mixer[kColorKoanSplitDragImage] = {SK_ColorWHITE};

  // Folders (zen-folders.css), from the accent colour (the active Space's
  // tint): the folder behind is the accent at 60% over grey (#c1c1c1 in dark
  // mode), its cover the accent at 30% over white (60% over black in dark
  // mode), the outline the accent at 50% over black (15% over #ebebeb in
  // dark mode). The tab count is the text colour at 60%; a highlighted
  // search result the text colour at 10%.
  mixer[kColorKoanFolderIconBack] = ui::AlphaBlend(
      ui::kColorSysPrimary,
      dark ? SkColorSetRGB(0xc1, 0xc1, 0xc1) : SkColorSetRGB(0x80, 0x80, 0x80),
      Alpha(0.6f));
  mixer[kColorKoanFolderIconFront] =
      ui::AlphaBlend(ui::kColorSysPrimary, dark ? SK_ColorBLACK : SK_ColorWHITE,
                     Alpha(dark ? 0.6f : 0.3f));
  mixer[kColorKoanFolderIconStroke] =
      ui::AlphaBlend(ui::kColorSysPrimary,
                     dark ? SkColorSetRGB(0xeb, 0xeb, 0xeb) : SK_ColorBLACK,
                     Alpha(dark ? 0.15f : 0.5f));
  mixer[kColorKoanFolderCount] = ui::SetAlpha(kColorToolbarText, Alpha(0.6f));
  mixer[kColorKoanFolderSearchItemSelected] =
      ui::SetAlpha(ui::kColorPrimaryForeground, Alpha(0.1f));

  // Space themes: Zen's --zen-themed-toolbar-bg-transparent, its background
  // without a theme (getGradient()) and getToolbarModifiedBaseRaw().
  mixer[kColorKoanThemeWindow] = {dark ? SkColorSetRGB(0x17, 0x17, 0x17)
                                       : SkColorSetRGB(0xe2, 0xe2, 0xe2)};
  mixer[kColorKoanThemeDefaultBackground] = {
      dark ? SkColorSetRGB(0x13, 0x13, 0x13) : SkColorSetRGB(0xe9, 0xe9, 0xe9)};
  mixer[kColorKoanThemeTintBase] = {dark ? SkColorSetRGB(23, 23, 26)
                                         : SkColorSetRGB(240, 240, 244)};
  // An inactive translucent window (Zen's zen.view.grey-out-inactive-windows:
  // InactiveCaption, flat and opaque). On macOS the controller paints the
  // system's window background colour instead (koan_theme_platform.h); this
  // is what that gave in Zen 1.24b on macOS 26, for the test glass.
  mixer[kColorKoanThemeInactiveWindow] = {
      dark ? SkColorSetRGB(0x18, 0x17, 0x17) : SkColorSetRGB(0xe5, 0xe5, 0xe5)};

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
  // Chromium's leaves and the URL bar's card follow the active token set;
  // Koan's own views have their values per look.
  AddShadcnChromiumColors(mixer, key);
  // Zen's default theme over a translucent window's blur
  // (ZenGradientGenerator.mjs getBrowserBg() on macOS): black at 40% in dark
  // mode, none in light mode.
  mixer[kColorKoanGlassTint] = {key.color_mode ==
                                        ui::ColorProviderKey::ColorMode::kDark
                                    ? SkColorSetARGB(0x66, 0, 0, 0)
                                    : SK_ColorTRANSPARENT};
  if (look::IsShadcnStyle()) {
    AddShadcnKoanColors(mixer, key);
  } else {
    AddZenColors(mixer, key);
  }

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
