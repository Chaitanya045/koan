// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VECTOR_ICONS_KOAN_ICON_SIZES_H_
#define KOAN_BROWSER_UI_VECTOR_ICONS_KOAN_ICON_SIZES_H_

namespace koan {

// The size (a square, in dip) Zen draws its icons at, per kind of surface
// (.agent/notes/zen-icons-inventory.md, "Render size"). The icons are the
// koan_icons.h ones, all on a 24-unit canvas, so a size is the caller's.
// Sizes of one view only (the Clear dart's 10, the URL pill's 16, ...) are
// with the view, in koan_sidebar_metrics.h.

// A glyph in a toolbar button: --zen-toolbar-button-size, 17 on macOS (a 31 x
// 29 button): the sidebar's toggle, Back, Forward and Reload.
inline constexpr int kToolbarIconSize = 17;

// Favicons, tab close buttons, the "+ New Tab" row, URL bar identity icons.
inline constexpr int kFaviconIconSize = 16;

// A row of the app menu.
inline constexpr int kMenuIconSize = 18;

// The "+" of the bottom bar (#zen-create-new-button).
inline constexpr int kCreateIconSize = 20;

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VECTOR_ICONS_KOAN_ICON_SIZES_H_
