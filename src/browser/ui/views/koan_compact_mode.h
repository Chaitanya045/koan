// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_

#include <algorithm>

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

// Mirrors the children of `layout` (a BrowserViewLayoutImpl::ProposedLayout,
// which is protected there) between `left` and `right`, and theirs within
// their parents, for IsSidebarOnRight().
template <typename Layout>
void MirrorLayoutChildren(Layout& layout, int left, int right) {
  for (auto& [child, child_layout] : layout.children) {
    child_layout.bounds.set_x(left + right - child_layout.bounds.right());
    MirrorLayoutChildren(child_layout, 0, child_layout.bounds.width());
  }
}

// Narrows the children of the top container's `layout` (as above) by
// `inset` on the leading side, dropping what was there: beside a visible
// sidebar (see below) its top row has the buttons of that corner.
template <typename Layout>
void InsetLayoutChildren(Layout& layout, int inset) {
  for (auto& [child, child_layout] : layout.children) {
    const int left = std::max(child_layout.bounds.x(), inset) - inset;
    const int right = child_layout.bounds.right() - inset;
    child_layout.bounds.set_x(left);
    child_layout.bounds.set_width(std::max(right - left, 0));
  }
}

// Where compact mode hides the toolbar but not the sidebar, the width the
// revealed toolbar leaves for the sidebar (Zen's navbar is above the page
// only); otherwise 0. Called when the top container is laid out on its own.
int GetRevealedToolbarSidebarInset(const views::View* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
