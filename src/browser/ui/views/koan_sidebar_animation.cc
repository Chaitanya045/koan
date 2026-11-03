// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_animation.h"

#include "base/i18n/rtl.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view_utils.h"

namespace koan {

bool UseSingleReflowSidebarAnimation() {
  return features::IsSidebarSingleReflowEnabled();
}

double LayoutSidebarReveal(views::View* browser_view, double progress) {
  auto* view = views::AsViewClass<BrowserView>(browser_view);
  views::View* region = view ? view->tab_strip_view() : nullptr;
  // Only the vertical tab strip (the sidebar) is slid; the horizontal one
  // isn't Koan's to transform.
  if (!region || region->GetClassName() != "VerticalTabStripRegionView" ||
      !region->layer()) {
    return progress;
  }

  gfx::Transform transform;
  double layout_progress = progress;
  if (IsCompactModeActive(browser_view)) {
    layout_progress = progress > 0.0 ? 1.0 : 0.0;
    // Layer transforms aren't mirrored with the layout: the sidebar sits on
    // the right with tabs on the right, or in a right-to-left UI, but not in
    // both.
    const bool slides_from_right =
        IsSidebarOnRight(browser_view) != base::i18n::IsRTL();
    const float offset = static_cast<float>(
        (1.0 - progress) * tabs::kVerticalTabStripDefaultUncollapsedWidth);
    transform.Translate(slides_from_right ? offset : -offset, 0);
  }
  if (region->layer()->transform() != transform) {
    region->layer()->SetTransform(transform);
  }
  return layout_progress;
}

}  // namespace koan
