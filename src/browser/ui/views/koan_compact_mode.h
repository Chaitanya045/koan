// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_

namespace views {
class View;
}

namespace koan {

// Whether compact mode is applied to the window of `browser_view` (a
// BrowserView). When true, the vertical tab strip takes no layout space (it
// only appears as a hover overlay) and the toolbar floats above the page.
// Called from BrowserViewTabbedLayoutImpl.
bool IsCompactModeActive(const views::View* browser_view);

// Whether the top toolbar is hidden (compact mode or single toolbar), so the
// page card reaches the top of the window.
bool IsToolbarHidden(const views::View* browser_view);

// Whether the strip at the top of the window where the frame draws its
// caption buttons (close, ...) is collapsed: like Zen, those buttons only
// show while the pointer is at the top edge, and the page card otherwise
// reaches up to a thin gutter.
bool IsCaptionStripCollapsed(const views::View* browser_view);

// Whether the sidebar (vertical tab strip) is on the right, everything
// mirrored (Zen's "Tabs on the right"). `view` is the BrowserView or a view in
// its window. The browser view's layout is computed as usual with the caption
// button areas swapped and then mirrored; the sidebar's top row and resize
// area mirror too.
bool IsSidebarOnRight(const views::View* view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
