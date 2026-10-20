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

  // Essentials tiles (koan_essentials.cc).
  kColorKoanEssentialsTile,
  kColorKoanEssentialsTileHovered,
  kColorKoanEssentialsTileSelected,

  // Toasts: a vertical gradient, the text, and the background of the
  // shortcut in it.
  kColorKoanToastBackgroundTop,
  kColorKoanToastBackgroundBottom,
  kColorKoanToastForeground,
  kColorKoanToastKeyBackground,

  kKoanColorsEnd,
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_COLOR_ID_H_
