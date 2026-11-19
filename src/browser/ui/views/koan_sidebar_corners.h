// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_CORNERS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_CORNERS_H_

namespace koan {

// Whether the tabbed layout shows one of upstream's floating corners beside
// the vertical tab strip (BrowserView's vertical_tab_strip_top_corner and
// vertical_tab_strip_bottom_corner: the concave pieces, in the frame's
// colour, at the strip's top-right and bottom-right), given `corner_value`,
// upstream's animated kTopCorner or kBottomCorner value (shown when
// positive), and whether compact mode hides the sidebar.
//
// Never in compact mode. There the strip is laid out once at the width it
// shows at and slid in and out with a layer transform
// (koan_sidebar_animation.h), but the corners are the BrowserView's own
// children, placed from the strip's layout bounds: they don't move with the
// slide. Upstream's flyout runs them through positive values on its own
// timeline (on hover from the resting value to -1 and back, on the collapse
// from 1), and in native fullscreen, where Koan doesn't show the toolbar
// (kShowFullscreenToolbar off), the resting top corner is 1. So a corner
// stood at the top of the window where the sidebar was going to be, ahead of
// it, or where it had just been, painted with the window's background: a
// dark notch flashing at the top edge as compact mode toggled or the hidden
// sidebar was peeked in and out. Zen's floating sidebar has no such corners.
constexpr bool SidebarFloatingCornerVisible(double corner_value,
                                            bool compact_mode) {
  return corner_value > 0.0 && !compact_mode;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_CORNERS_H_
