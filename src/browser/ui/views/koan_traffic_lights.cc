// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_traffic_lights.h"

#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_window_controls.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace koan {

std::optional<int> GetTrafficLightsTitlebarHeight(
    const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  // Where Koan hides the toolbar and keeps the sidebar, its top row is the
  // lights' row, at the top of the window (tabs on the left) or, while the
  // strip for them is open, above the page (tabs on the right; see
  // ContentCardInsets()). Elsewhere the toolbar is the row they share with the
  // sidebar's, which is what Chromium centres them in. In compact mode with
  // the sidebar hidden they come with the toolbar; in fullscreen Chromium
  // slides them in with the menu bar.
  if (!view || !features::IsSidebarEnabled() || !view->GetIsNormalType() ||
      !view->ShouldDrawVerticalTabStrip() || view->IsFullscreen() ||
      IsCompactModeActive(view) || !IsToolbarHidden(view)) {
    return std::nullopt;
  }
  return GetHiddenToolbarRowHeight(GetCaptionButtonAreaSize(view).height());
}

bool IsPointerOverTrafficLights(const BrowserView* browser_view) {
  const BrowserWidget* widget = browser_view->browser_widget();
  const BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  if (!frame) {
    return false;
  }
  // The lights are in the corner their exclusion covers: it reaches from the
  // window's edge to the lights' far edge (and their vertical padding).
  const BrowserLayoutParams params = frame->GetBrowserLayoutParams();
  const BrowserLayoutExclusionArea& exclusion =
      frame->CaptionButtonsOnLeadingEdge() ? params.leading_exclusion
                                           : params.trailing_exclusion;
  if (exclusion.content.IsEmpty()) {
    return false;
  }
  gfx::Point pointer = display::Screen::Get()->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(frame, &pointer);
  const gfx::Rect corner(static_cast<int>(exclusion.content.width()),
                         static_cast<int>(exclusion.content.height()));
  return (frame->CaptionButtonsOnLeadingEdge()
              ? corner
              : gfx::Rect(frame->width() - corner.width(), 0, corner.width(),
                          corner.height()))
      .Contains(pointer);
}

}  // namespace koan
