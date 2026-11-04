// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_

#include <algorithm>

class BrowserView;

namespace views {
class View;
}

namespace koan {

// Whether the window is fullscreen: BrowserView::IsFullscreen() without the
// crash while the window is going away. Then browser_widget() is null (the
// BrowserView's own call dereferences it) while GetWidget() still returns the
// dying widget, so that is no test of the window being alive.
bool IsWindowFullscreen(const BrowserView* browser_view);

// Whether compact mode is applied to the window of `browser_view` (a
// BrowserView). When true, the vertical tab strip takes no layout space (it
// only appears as a hover overlay) and the toolbar floats above the page.
// Called from BrowserViewTabbedLayoutImpl.
bool IsCompactModeActive(const views::View* browser_view);

// Whether compact mode hides the sidebar but keeps its header (the sidebar
// toggle and the navigation buttons: single toolbar), which then sits in the
// window's top row, at its leading corner.
bool KeepsSidebarNav(const views::View* browser_view);

// Whether the top toolbar is hidden (compact mode or single toolbar), so the
// page card reaches the top of the window.
bool IsToolbarHidden(const views::View* browser_view);

// macOS: whether the window is in native fullscreen with Koan's toolbar
// hidden (single toolbar, or compact mode hiding the toolbar). Like Zen,
// nothing of the top chrome is revealed from the top edge then (Chromium's
// toolbar, bookmarks bar and title bar strip): Koan's own layout stays, with
// the sidebar's header and the URL pill, or in compact mode the toolbar's
// own slide. Called from the BrowserView and BrowserFrameViewMac hooks, so
// that Chromium's fullscreen toolbar style is "none" and its dialogs anchor
// to the window. False elsewhere.
bool HidesFullscreenTopChrome(const views::View* browser_view);

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

// The height of the sidebar's top row where Koan's toolbar is hidden and the
// strip reaches the top of the window, given `caption_buttons_height`, the
// height of the frame's caption buttons beside it. On macOS the row holds
// the traffic lights and has Zen's toolbar height, whatever the buttons'
// own (koan_traffic_lights.h centres them in it); elsewhere it is just the
// buttons' height.
int GetHiddenToolbarRowHeight(int caption_buttons_height);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
