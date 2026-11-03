// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_HEADER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_HEADER_H_

#include "ui/gfx/geometry/rect.h"

namespace views {
class View;
struct ProposedLayout;
}  // namespace views

namespace koan {

// Hooks for VerticalTabStripTopContainer, the row at the top of the vertical
// tab strip (sidebar toggle, tab groups / tab search). In single-toolbar
// mode Koan adds the navigation buttons to that row, at its trailing end,
// like Zen's sidebar header.

// Adds layouts for Koan's views in `host` to `layout`: right-aligned to
// `right` and vertically centred on `center_y`; hidden if they would overlap
// a view already in `layout` on that row.
void LayoutSidebarHeaderTrailingViews(const views::View* host,
                                      views::ProposedLayout& layout,
                                      int right,
                                      int center_y);

// The width of Koan's views in `host`, for its preferred width.
int GetSidebarHeaderTrailingWidth(const views::View* host);

// With tabs on the right (see koan_compact_mode.h), mirrors the row laid out
// in `layout` inside `width`, so the space kept for caption buttons and the
// sidebar toggle end up at the window edge.
void MirrorSidebarHeaderLayout(const views::View* host,
                               views::ProposedLayout& layout,
                               int width);

// Where `bounds` (in `host`) would be under the macOS traffic lights, which
// are over the window's leading corner whatever Chromium reserved for them
// (it reserves nothing while the sidebar sits above the toolbar's row, or
// when its layout ran before the window's frame), the bounds moved clear of
// them: to their right where the row has the room, else below them.
// `bounds` itself elsewhere, and everywhere but on macOS.
gfx::Rect AvoidTrafficLights(const views::View* host, const gfx::Rect& bounds);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_HEADER_H_
