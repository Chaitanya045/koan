// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_METRICS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_METRICS_H_

#include "build/build_config.h"
#include "chrome/browser/ui/layout_constants.h"

// Sizes of Koan's sidebar tab list, URL bar, toasts, media cards, download
// animation and welcome screen, in one place (Zen's today:
// zen-tabs/vertical-tabs.css, zen-omnibox.css, zen-popup.css,
// zen-media-controls.css, zen-download-*-animation.css, zen-welcome.css).
// Colours are in koan/browser/ui/color/koan_color_id.h.
namespace koan {

// Shared with Chromium's tab rows and tiles (Koan sets them in the
// layout_constants.cc hook): the corner radius of rows and tiles, a row's
// height and the inset of everything in the strip from its edges.
inline int SidebarRowCornerRadius() {
  return GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius);
}
inline int SidebarRowHeight() {
  return GetLayoutConstant(LayoutConstant::kVerticalTabHeight);
}
inline int SidebarStripInset() {
  return GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);
}

// The expanded sidebar's width limits (Zen's --zen-toolbox-min-width and
// zen.view.sidebar-expanded.max-width).
inline constexpr int kSidebarMinWidth = 163;
inline constexpr int kSidebarMaxWidth = 500;

// The gap between a tab's favicon and its title (Zen's
// --tab-icon-end-margin).
inline constexpr int kSidebarTabIconGap = 10;

// A pinned tab away from its pinned URL (zen-tabs/vertical-tabs.css): the
// reset button over its favicon (from the row's edge to past the icon, inset
// from the top and bottom), the slash at the button's end, 8px before the
// label, and the sublabel ("Back to pinned url", Zen's x-small) under the
// label, which moves up to make room.
inline constexpr int kPinnedResetButtonHalfWidth = 14;
inline constexpr int kPinnedResetButtonInset = 4;
inline constexpr int kPinnedResetLabelGap = 8;
inline constexpr float kPinnedSlashWidth = 2.5f;
inline constexpr float kPinnedSlashHeight = 16.f;
inline constexpr float kPinnedSlashRadius = 2.f;
inline constexpr float kPinnedSlashAngle = 15.f;
inline constexpr int kPinnedIconSize = 16;
inline constexpr int kPinnedSublabelFontSize = 10;
inline constexpr int kPinnedSublabelHeight = 12;
inline constexpr int kPinnedSublabelRise = 5;
// A tab's own icon (Change Icon…): an emoji drawn at this size in a favicon.
inline constexpr int kCustomIconFontSize = 13;
// The icon picker: rows of emoji buttons below a field.
inline constexpr int kIconPickerColumns = 8;
inline constexpr int kIconPickerButtonSize = 32;
inline constexpr int kIconPickerSpacing = 8;

// Narrower than this a row is in the collapsed sidebar: icons only.
inline constexpr int kMinExpandedRowWidth = 100;

// The New Tab row: its icon where the tabs have their favicon, the label a
// little left of their titles.
inline constexpr int kNewTabIconSize = 16;
inline constexpr int kNewTabIconX = 8;
inline constexpr int kNewTabLabelX = 28;

// The separator above the tabs: a 1px line inset from both ends, and the
// Clear button at its end (icon, gap, label, padding before it). On macOS
// Zen pads the whole row by 5px on both sides.
inline constexpr int kSeparatorRowHeight = 22;
inline constexpr int kSeparatorInset = 4;
#if BUILDFLAG(IS_MAC)
inline constexpr int kSeparatorRowPadding = 5;
#else
inline constexpr int kSeparatorRowPadding = 0;
#endif
inline constexpr int kClearFontSize = 10;
inline constexpr int kClearIconSize = 10;
inline constexpr int kClearIconGap = 2;
inline constexpr int kClearPadding = 8;
// How far the icon moves down while the button is pressed.
inline constexpr int kClearPressedOffset = 2;

// The sidebar's URL pill (and the URL bar while it edits over the pill), and
// the floating URL bar (#urlbar[breakout-extend]).
inline constexpr float kUrlPillCornerRadius = 9;
inline constexpr float kFloatingUrlBarCornerRadius = 15;

// The pill's insets before the site's name and after its buttons, the gap
// between them, and its icon buttons (Zen's urlbar page-action size).
inline constexpr int kUrlPillLeadingInset = 12;
inline constexpr int kUrlPillTrailingInset = 5;
inline constexpr int kUrlPillSpacing = 2;
inline constexpr int kUrlPillButtonSize = 26;
inline constexpr int kUrlPillIconSize = 16;

// Zen's floating URL bar (ZenUIManager.mjs, zen-omnibox.css): min(window
// width / 1.5, 750) wide, centred vertically as if it were at least 333 tall
// (with its results), text 1.15em (1.5em on macOS) and a box-shadow of
// 0 30px 140px -15px.
inline constexpr int kFloatingUrlBarMaxWidth = 750;
inline constexpr int kFloatingUrlBarMinHeight = 333;
#if BUILDFLAG(IS_MAC)
inline constexpr int kFloatingUrlBarFontDelta = 6;
#else
inline constexpr int kFloatingUrlBarFontDelta = 2;
#endif
inline constexpr int kFloatingUrlBarShadowY = 30;
inline constexpr int kFloatingUrlBarShadowBlur = 140;
inline constexpr int kFloatingUrlBarShadowSpread = -15;

// With "normal", the bar edits in place and widens over the page once the
// user types (Zen's #urlbar[open] in the single toolbar: min(90%, 40rem)).
inline constexpr int kUrlBarOpenMaxWidth = 640;

// Toasts, at the top right of the window: Zen's 42px with 6px padding
// (48px and 8px on macOS), plus 4px around the text.
#if BUILDFLAG(IS_MAC)
inline constexpr int kToastHeight = 48;
inline constexpr int kToastPadding = 12;
#else
inline constexpr int kToastHeight = 42;
inline constexpr int kToastPadding = 10;
#endif
inline constexpr int kToastGap = 4;
inline constexpr int kToastCornerRadius = 14;
inline constexpr int kToastMargin = 8;
inline constexpr int kToastFontSize = 14;
inline constexpr int kToastKeyFontSize = 12;
inline constexpr int kToastKeyCornerRadius = 4;
inline constexpr int kToastKeyPaddingVertical = 5;
inline constexpr int kToastKeyPaddingHorizontal = 7;
// The toast's icon button: 28px high with 10px corners, a 14px icon and 8px
// on either side of it.
inline constexpr int kToastButtonHeight = 28;
inline constexpr int kToastButtonCornerRadius = 10;
inline constexpr int kToastButtonIconSize = 14;
inline constexpr int kToastButtonPadding = 8;

// Media cards at the bottom of the sidebar (zen-media-controls.css): 14px
// corners (--border-radius-medium in the sidebar; 2px less on macOS) with
// 4px 6px padding and room for a 6px shadow, 26px buttons with 16px icons
// and 5px corners; the playback buttons spread over at most 10em; rows for
// the title (20px) and artist (16px) and for the progress (a 4px bar
// between the times). Behind the front card, two more peek out 8px each.
#if BUILDFLAG(IS_MAC)
inline constexpr int kMediaCardCornerRadius = 12;
#else
inline constexpr int kMediaCardCornerRadius = 14;
#endif
inline constexpr int kMediaCardShadowMargin = 3;
inline constexpr int kMediaCardShadowBlur = 6;
inline constexpr int kMediaCardPaddingVertical = 4;
inline constexpr int kMediaCardPaddingHorizontal = 6;
inline constexpr int kMediaButtonSize = 26;
inline constexpr int kMediaIconSize = 16;
inline constexpr int kMediaButtonCornerRadius = 5;
inline constexpr int kMediaControlsGap = 11;
inline constexpr int kMediaPlaybackMaxWidth = 160;
inline constexpr int kMediaTitleHeight = 20;
inline constexpr int kMediaArtistHeight = 16;
inline constexpr int kMediaInfoGap = 4;
inline constexpr int kMediaProgressRowHeight = 28;
inline constexpr int kMediaProgressBarHeight = 4;
inline constexpr int kMediaProgressMargin = 8;
inline constexpr int kMediaStackPeek = 8;
// Narrower than this the sidebar shows icons only: no media cards.
inline constexpr int kMediaMinSidebarWidth = 150;

// The download animation: a 30px circle with a 2px ring and Zen's big
// shadow (0 3px 8px), the arrow at 70% of the inner circle; the downloads
// tile is 42px with 12px corners and a 24px icon, 24px from the window's
// bottom and sidebar edges (34px at the end of its slide in, 50px outside
// the window before).
inline constexpr int kDownloadArcSize = 30;
inline constexpr int kDownloadArcRing = 2;
inline constexpr int kDownloadArcShadowY = 3;
inline constexpr int kDownloadArcShadowBlur = 8;
inline constexpr int kDownloadTileSize = 42;
inline constexpr int kDownloadTileCornerRadius = 12;
inline constexpr int kDownloadTileIconSize = 24;
inline constexpr int kDownloadTileShadowBlur = 6;
inline constexpr int kDownloadTileEdge = 24;
inline constexpr int kDownloadTileOvershoot = 34;
inline constexpr int kDownloadTileOutside = -50;
// Room around both for their shadows.
inline constexpr int kDownloadShadowMargin = 10;

// The welcome screen (zen-welcome.css, 1rem = 16px).
// The title: 5.2rem bold at most, lines 1.05 high, letters -0.03em closer;
// the start button below it.
inline constexpr int kWelcomeTitleMaxSize = 83;
inline constexpr int kWelcomeTitleMinSize = 40;
inline constexpr float kWelcomeTitleLineHeight = 1.05f;
inline constexpr float kWelcomeTitleLetterSpacing = -0.03f;
inline constexpr int kWelcomeStartButtonWidth = 100;
inline constexpr int kWelcomeStartButtonHeight = 48;
inline constexpr int kWelcomeStartButtonIconSize = 18;
inline constexpr int kWelcomeStartButtonGap = 28;
// The pages: a card 86% x 82% of the window (all of it below 1000px) with
// 1.4rem corners; its sidebar 38% wide, padded 4rem 3.8rem 3.6rem, with the
// back button, the title (3rem bold), the description (1.25rem, 1.6 high)
// and the big accent buttons (1.2rem semibold, 12px corners) at the bottom.
inline constexpr float kWelcomeCardWidth = 0.86f;
inline constexpr float kWelcomeCardHeight = 0.82f;
inline constexpr int kWelcomeCardFullWindowBelow = 1000;
inline constexpr int kWelcomeCardCornerRadius = 22;
inline constexpr float kWelcomeSidebarWidth = 0.38f;
inline constexpr int kWelcomeSidebarPaddingTop = 64;
inline constexpr int kWelcomeSidebarPaddingSide = 61;
inline constexpr int kWelcomeSidebarPaddingBottom = 58;
inline constexpr int kWelcomeSidebarShadowX = 4;
inline constexpr int kWelcomeSidebarShadowBlur = 24;
inline constexpr int kWelcomeBackFontSize = 18;
inline constexpr int kWelcomeBackIconSize = 16;
inline constexpr int kWelcomeBackIconGap = 8;
inline constexpr int kWelcomeBackHeight = 24;
inline constexpr int kWelcomeBackGap = 35;
inline constexpr int kWelcomeHeadingSize = 48;
inline constexpr int kWelcomeHeadingMinSize = 32;
inline constexpr int kWelcomeHeadingGap = 38;
inline constexpr int kWelcomeDescriptionSize = 20;
inline constexpr float kWelcomeDescriptionLineHeight = 1.6f;
inline constexpr int kWelcomeDescriptionGap = 19;
inline constexpr int kWelcomeButtonHeight = 62;
inline constexpr int kWelcomeButtonCornerRadius = 12;
inline constexpr int kWelcomeButtonFontSize = 19;
inline constexpr int kWelcomeButtonGap = 16;
// The choices: cards at most 30rem wide with 20px corners, padded 1.9rem,
// text 1.3rem, a 22px radio circle (its dot inset 3px), 2.5rem apart and
// 4rem from the content's sides; search engines at most 24rem wide, with a
// tile on the trailing third.
inline constexpr int kWelcomeOptionMaxWidth = 480;
inline constexpr int kWelcomeOptionHeight = 86;
inline constexpr int kWelcomeOptionCornerRadius = 20;
inline constexpr int kWelcomeOptionPadding = 30;
inline constexpr int kWelcomeOptionFontSize = 21;
inline constexpr int kWelcomeOptionGap = 40;
inline constexpr int kWelcomeOptionSideMargin = 64;
inline constexpr int kWelcomeOptionTextGap = 16;
inline constexpr int kWelcomeOptionShadowY = 2;
inline constexpr int kWelcomeOptionShadowBlur = 10;
inline constexpr int kWelcomeRadioSize = 22;
inline constexpr int kWelcomeRadioDotInset = 3;
inline constexpr int kWelcomeEngineMaxWidth = 384;
inline constexpr int kWelcomeEngineHeight = 76;
inline constexpr int kWelcomeEngineGap = 16;
inline constexpr float kWelcomeEngineTileWidth = 0.34f;
inline constexpr int kWelcomeEngineTileFontSize = 26;
// The colours: a panel of 56px swatches 20px apart, padded 28px.
inline constexpr int kWelcomeSwatchSize = 56;
inline constexpr int kWelcomeSwatchGap = 20;
inline constexpr int kWelcomePanelPadding = 28;
// The Essentials: a mock window (2.4rem top-left corner) 92% as wide as the
// page after its 3rem top and 6rem left padding; its sidebar 80% wide,
// padded 2.6rem 2.4rem, with three 22px dots, the 3-column grid of 2rem
// rounded tiles 1.5rem apart (a 3px border when chosen) and two 4.5rem mock
// tabs with 1.6rem corners, 1.6rem apart.
inline constexpr int kWelcomeMockCornerRadius = 38;
inline constexpr int kWelcomeMockPaddingTop = 48;
inline constexpr int kWelcomeMockPaddingLeft = 96;
inline constexpr float kWelcomeMockWidth = 0.92f;
inline constexpr float kWelcomeMockSidebarWidth = 0.8f;
inline constexpr int kWelcomeMockSidebarPaddingVertical = 42;
inline constexpr int kWelcomeMockSidebarPaddingHorizontal = 38;
inline constexpr int kWelcomeMockDotSize = 22;
inline constexpr int kWelcomeMockDotGap = 16;
inline constexpr int kWelcomeMockDotsMargin = 10;
inline constexpr int kWelcomeMockGap = 26;
inline constexpr int kWelcomeMockGridGap = 24;
inline constexpr int kWelcomeMockTabHeight = 72;
inline constexpr int kWelcomeMockTabCornerRadius = 26;
inline constexpr int kWelcomeTileCornerRadius = 32;
inline constexpr int kWelcomeTileBorder = 3;
inline constexpr int kWelcomeTileNameFontSize = 12;

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_METRICS_H_
