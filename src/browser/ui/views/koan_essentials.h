// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_H_

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class View;
}  // namespace views

namespace koan {

// Hooks for the vertical tab strip's pinned tabs, which Koan shows as Zen's
// Essentials: a grid of square tiles at the top of the sidebar
// (pinned_tab_container_view.cc, vertical_tab_style_views.cc).

// Caps the number of tiles per row (`columns` fit the width) at Zen's four.
int ClampEssentialsColumns(int columns);

// Paints the background of the pinned tab `tab` as an Essentials tile
// (`selected`: active or multi-selected; `hover`: the hover animation's
// progress, 0..1). Called by PaintSidebarTab (koan_sidebar_tabs.h) with
// Koan's sidebar on.
void PaintEssentialsTile(gfx::Canvas* canvas,
                         const views::View& tab,
                         bool selected,
                         double hover,
                         float corner_radius);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_H_
