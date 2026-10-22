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

  // Glance (koan_glance_controller.cc): the window background laid over the
  // page behind the card, the card's shadow, and the buttons beside it
  // (normal, and the close button asking to confirm).
  kColorKoanGlanceBackdrop,
  kColorKoanGlanceCardShadow,
  kColorKoanGlanceButton,
  kColorKoanGlanceButtonIcon,
  kColorKoanGlanceButtonConfirm,
  kColorKoanGlanceButtonConfirmIcon,

  kKoanColorsEnd,
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_
