// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_content_card.h"

#include <algorithm>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_window_controls.h"

namespace koan {

namespace {

// The card's insets with `sidebar_side` on the sidebar's side and the
// margin on the others.
gfx::Insets SidebarSideInsets(const BrowserView* browser_view,
                              int top,
                              int sidebar_side) {
  return IsSidebarOnRight(browser_view)
             ? gfx::Insets::TLBR(top, kContentCardMargin, kContentCardMargin,
                                 sidebar_side)
             : gfx::Insets::TLBR(top, sidebar_side, kContentCardMargin,
                                 kContentCardMargin);
}

}  // namespace

bool UseContentCard(const BrowserView* browser_view) {
  return features::IsSidebarEnabled() && browser_view->GetIsNormalType() &&
         browser_view->ShouldDrawVerticalTabStrip() &&
         !browser_view->IsFullscreen();
}

gfx::Insets ContentCardInsets(const BrowserView* browser_view) {
  if (IsToolbarHidden(browser_view)) {
    // No toolbar above the card: it reaches the top of the window, leaving
    // only a gutter, or (while revealed) the strip where the frame draws its
    // caption buttons; they paint above the client view and would otherwise
    // sit on the page.
    const int top =
        IsCaptionStripCollapsed(browser_view)
            ? kContentCardMargin
            : std::max(kContentCardMargin,
                       GetCaptionButtonAreaSize(browser_view).height());
    // Compact mode may hide the sidebar: then a gutter on its side too.
    const int sidebar_side =
        IsCompactModeActive(browser_view) ? kContentCardMargin : 0;
    return SidebarSideInsets(browser_view, top, sidebar_side);
  }
  // The vertical tab strip already leaves a gap on its side, and the toolbar
  // sits directly above the card.
  return SidebarSideInsets(browser_view, 0, 0);
}

gfx::RoundedCornersF ActiveContentsRadii(
    const BrowserView* browser_view,
    const gfx::RoundedCornersF& default_radii) {
  return UseContentCard(browser_view)
             ? gfx::RoundedCornersF(kContentCardCornerRadius)
             : default_radii;
}

}  // namespace koan
