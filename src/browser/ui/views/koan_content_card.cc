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

bool UseContentCard(const BrowserView* browser_view) {
  return features::IsSidebarEnabled() && browser_view->GetIsNormalType() &&
         browser_view->ShouldDrawVerticalTabStrip() &&
         !browser_view->IsFullscreen();
}

gfx::Insets ContentCardInsets(const BrowserView* browser_view) {
  if (IsToolbarHidden(browser_view)) {
    // No toolbar above the card: it reaches the top of the window, leaving
    // only a gutter, or the strip where the frame draws its caption buttons
    // (they paint above the client view and would otherwise sit on the page).
    const int top = std::max(kContentCardMargin,
                             GetCaptionButtonAreaSize(browser_view).height());
    // Compact mode also hides the sidebar: gutter on the leading side too.
    const int leading =
        IsCompactModeActive(browser_view) ? kContentCardMargin : 0;
    return gfx::Insets::TLBR(top, leading, kContentCardMargin,
                             kContentCardMargin);
  }
  // The vertical tab strip already leaves a gap on the leading side, and the
  // toolbar sits directly above the card.
  return gfx::Insets::TLBR(0, 0, kContentCardMargin, kContentCardMargin);
}

gfx::RoundedCornersF ActiveContentsRadii(
    const BrowserView* browser_view,
    const gfx::RoundedCornersF& default_radii) {
  return UseContentCard(browser_view)
             ? gfx::RoundedCornersF(kContentCardCornerRadius)
             : default_radii;
}

}  // namespace koan
