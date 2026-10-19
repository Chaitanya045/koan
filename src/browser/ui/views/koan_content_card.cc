// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_content_card.h"

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"

namespace koan {

bool UseContentCard(const BrowserView* browser_view) {
  return features::IsSidebarEnabled() && browser_view->GetIsNormalType() &&
         browser_view->ShouldDrawVerticalTabStrip() &&
         !browser_view->IsFullscreen();
}

gfx::Insets ContentCardInsets(const BrowserView* browser_view) {
  if (IsCompactModeActive(browser_view)) {
    return gfx::Insets(kContentCardMargin);
  }
  if (IsToolbarHidden(browser_view)) {
    // Single toolbar: the card reaches the top of the window, with a gutter.
    return gfx::Insets::TLBR(kContentCardMargin, 0, kContentCardMargin,
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
