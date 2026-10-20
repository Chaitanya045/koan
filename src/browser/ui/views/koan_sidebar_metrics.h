// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_METRICS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_METRICS_H_

#include "build/build_config.h"
#include "chrome/browser/ui/layout_constants.h"

// Sizes of Koan's sidebar tab list, URL bar and toasts, in one place (Zen's
// today: zen-tabs/vertical-tabs.css, zen-omnibox.css, zen-popup.css). Colours
// are in koan/browser/ui/color/koan_color_id.h.
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

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_METRICS_H_
