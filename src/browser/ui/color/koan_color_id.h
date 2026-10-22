// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"

namespace koan {

// Colours of Koan's own views, after Chrome's. Defined in koan_color_mixer.cc
// (mostly from Chrome's colours, Zen's values today), so a restyle changes
// them in one place.
enum KoanColorIds : ui::ColorId {
  kKoanColorsStart = kChromeColorsEnd,

  // Sidebar tab rows (koan_sidebar_tabs.cc): the selected tab's background
  // and a hovered tab's.
  kColorKoanSidebarTabSelected = kKoanColorsStart,
  kColorKoanSidebarTabHovered,
  // The New Tab row's background
  // while hovered and pressed, and its icon and label.
  kColorKoanSidebarRowHovered,
  kColorKoanSidebarRowPressed,
  kColorKoanSidebarNewTabForeground,
  // The separator above the tabs and its Clear button, shown while the
  // pointer is over the sidebar and over the button itself.
  kColorKoanSidebarSeparator,
  kColorKoanSidebarClearForeground,
  kColorKoanSidebarClearForegroundHovered,

  // A pinned tab away from its pinned URL (koan_pinned_tab_views.cc): the
  // slash after its icon, the reset button's background while hovered and
  // pressed, and the "Back to pinned url" sublabel.
  kColorKoanPinnedTabSlash,
  kColorKoanPinnedTabResetHovered,
  kColorKoanPinnedTabResetPressed,
  kColorKoanPinnedTabSublabel,

  // Essentials tiles (koan_essentials.cc).
  kColorKoanEssentialsTile,
  kColorKoanEssentialsTileHovered,
  kColorKoanEssentialsTileSelected,

  // The URL bar (koan_sidebar_nav_view.cc, koan_url_bar_controller.cc): the
  // sidebar's URL pill and the floating URL bar's shadow.
  kColorKoanUrlPill,
  kColorKoanFloatingUrlBarShadow,

  // Toasts: a vertical gradient, the text, and the background of the
  // shortcut in it.
  kColorKoanToastBackgroundTop,
  kColorKoanToastBackgroundBottom,
  kColorKoanToastForeground,
  kColorKoanToastKeyBackground,
  // The toast's icon button and its icon.
  kColorKoanToastButton,
  kColorKoanToastButtonIcon,

  // Glance (koan_glance_controller.cc): the window background laid over the
  // page behind the card, the card's shadow, and the buttons beside it
  // (normal, and the close button asking to confirm).
  kColorKoanGlanceBackdrop,
  kColorKoanGlanceCardShadow,
  kColorKoanGlanceButton,
  kColorKoanGlanceButtonIcon,
  kColorKoanGlanceButtonConfirm,
  kColorKoanGlanceButtonConfirmIcon,

  // Media cards in the sidebar (koan_media_controller.cc): the card, its
  // shadow and outline, and the progress bar's track and played part.
  kColorKoanMediaCard,
  kColorKoanMediaCardShadow,
  kColorKoanMediaCardOutline,
  kColorKoanMediaProgressTrack,
  kColorKoanMediaProgress,

  // The download animation (koan_download_animation.cc): the flying icon's
  // ring, circle, arrow and shadow; the downloads tile, its icon and shadow.
  kColorKoanDownloadArcRing,
  kColorKoanDownloadArcCircle,
  kColorKoanDownloadArcIcon,
  kColorKoanDownloadArcShadow,
  kColorKoanDownloadTile,
  kColorKoanDownloadTileIcon,
  kColorKoanDownloadTileShadow,

  // The welcome screen (koan_welcome.cc): the backdrop's gradient, the
  // title and the start button; the pages' card (sidebar, content, the
  // surfaces of options and panels, tiles, the mock window), borders,
  // shadows, text, radio circles and the accent buttons.
  kColorKoanWelcomeBackdropTop,
  kColorKoanWelcomeBackdropBottom,
  kColorKoanWelcomeTitle,
  kColorKoanWelcomeStartButton,
  kColorKoanWelcomeStartButtonHovered,
  kColorKoanWelcomeStartButtonIcon,
  kColorKoanWelcomeSidebar,
  kColorKoanWelcomeContent,
  kColorKoanWelcomeSurface,
  kColorKoanWelcomeTile,
  kColorKoanWelcomeMockWindow,
  kColorKoanWelcomeBorder,
  kColorKoanWelcomeShadow,
  kColorKoanWelcomeText,
  kColorKoanWelcomeTextSecondary,
  kColorKoanWelcomeRadio,
  kColorKoanWelcomeAccent,
  kColorKoanWelcomeAccentHovered,
  kColorKoanWelcomeOnAccent,

  kKoanColorsEnd,
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_
