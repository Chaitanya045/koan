// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_traffic_lights.h"

#include <algorithm>

#include "build/build_config.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_lights_geometry.h"
#include "koan/browser/ui/views/koan_sidebar_header.h"
#include "koan/browser/ui/views/koan_window_controls.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Not a constant expression, so that the compiler doesn't count the code
// after a check of it as unreachable (and warn) on the platforms without
// traffic lights.
bool HasTrafficLights() {
  return BUILDFLAG(IS_MAC);
}

}  // namespace

bool OwnsTitlebar(const BrowserView* browser_view) {
  return browser_view && features::IsSidebarEnabled() &&
         browser_view->GetIsNormalType() &&
         browser_view->ShouldDrawVerticalTabStrip() &&
         !IsWindowFullscreen(browser_view);
}

std::optional<int> GetTrafficLightsTitlebarHeight(
    const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  // Where Koan hides the toolbar and keeps the sidebar, its top row is the
  // lights' row, at the top of the window (tabs on the left) or, while the
  // strip for them is open, above the page (tabs on the right; see
  // ContentCardInsets()). Elsewhere the toolbar is the row they share with the
  // sidebar's, which is what Chromium centres them in. In compact mode with
  // the sidebar hidden they come with the toolbar, or, with single toolbar,
  // stay in the sidebar's header, which compact mode keeps; in fullscreen
  // Chromium slides them in with the menu bar.
  if (!OwnsTitlebar(view) ||
      (IsCompactModeActive(view) && !KeepsSidebarNav(view)) ||
      !IsToolbarHidden(view)) {
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
  // window's edge to the lights' far edge and down to the row's bottom (the
  // vertical padding is the room Chromium keeps below them). Another
  // application's window over the corner still counts.
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
                         static_cast<int>(exclusion.content.height() +
                                          exclusion.vertical_padding));
  return (frame->CaptionButtonsOnLeadingEdge()
              ? corner
              : gfx::Rect(frame->width() - corner.width(), 0, corner.width(),
                          corner.height()))
      .Contains(pointer);
}

#if !BUILDFLAG(IS_MAC)
bool IsPointerOverWindow(const BrowserView* browser_view) {
  return true;
}
#endif

bool HidesSidebarTrailingControls(const views::View* host) {
  const views::Widget* widget = host->GetWidget();
  const views::View* browser_view = widget ? widget->client_view() : nullptr;
  // The row Zen has is the one Koan's single toolbar makes (the toolbar is
  // hidden and the sidebar's header stays).
  return HasTrafficLights() && browser_view && IsToolbarHidden(browser_view) &&
         (!IsCompactModeActive(browser_view) || KeepsSidebarNav(browser_view));
}

gfx::Rect AvoidTrafficLights(const views::View* host, const gfx::Rect& bounds) {
  // (Compiled everywhere, so that the Linux build checks it; only macOS has
  // traffic lights.)
  if (!HasTrafficLights()) {
    return bounds;
  }
  const views::Widget* widget = host->GetWidget();
  const auto* browser_view =
      widget ? views::AsViewClass<BrowserView>(widget->client_view()) : nullptr;
  const BrowserWidget* browser_widget =
      browser_view ? browser_view->browser_widget() : nullptr;
  const BrowserFrameView* frame =
      browser_widget ? browser_widget->GetFrameView() : nullptr;
  // The lights are over the sidebar only on its leading side (not with tabs
  // on the right) and out of fullscreen, where macOS shows them by itself.
  if (!frame || IsWindowFullscreen(browser_view) ||
      IsSidebarOnRight(browser_view) || !frame->CaptionButtonsOnLeadingEdge()) {
    return bounds;
  }
  const BrowserLayoutParams params = frame->GetBrowserLayoutParams();
  const BrowserLayoutExclusionArea& exclusion = params.leading_exclusion;
  if (exclusion.content.IsEmpty()) {
    return bounds;
  }
  const gfx::Rect lights(
      params.visual_client_area.origin(),
      gfx::Size(static_cast<int>(exclusion.content.width()),
                static_cast<int>(exclusion.content.height() +
                                 exclusion.vertical_padding)));
  // In the host's own coordinates.
  const gfx::Point origin = host->ConvertRectToWidget(gfx::Rect()).origin();
  gfx::Rect lights_in_host = lights;
  lights_in_host.Offset(-origin.x(), -origin.y());
  return MoveClearOf(bounds, lights_in_host, host->width());
}

}  // namespace koan
