// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_corners.h"

#include "build/build_config.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_window_corners_geometry.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace koan {

gfx::RoundedCornersF RestoredWindowCorners(const BrowserView* browser_view,
                                           float radius) {
  // Koan's own windows, whose views leave the corners clear: those with
  // Koan's layout. Elsewhere the page would reach a bottom corner square.
  const bool round_bottom = browser_view && features::IsSidebarEnabled() &&
                            browser_view->GetIsNormalType();
  return RestoredCorners(radius, round_bottom);
}

void RoundContentsToWindowCorners(views::View* view, bool in_glass_mode) {
#if BUILDFLAG(IS_LINUX)
  auto* const browser_view = views::AsViewClass<BrowserView>(view);
  MultiContentsView* const contents =
      browser_view ? browser_view->multi_contents_view() : nullptr;
  BrowserWidget* const widget =
      browser_view ? browser_view->browser_widget() : nullptr;
  const BrowserFrameView* const frame =
      widget ? widget->GetFrameView() : nullptr;
  if (!contents || !contents->parent() || !frame) {
    return;
  }
  // The window's visible shape, in the frame (none while maximized, tiled or
  // fullscreen), and where it is in the browser view.
  const gfx::RoundedCornersF window_corners = frame->GetWindowRoundedCorners();
  const gfx::RectF window = views::View::ConvertRectToTarget(
      frame, browser_view,
      gfx::SkRectToRectF(frame->GetRestoredClipRegion().rect()));
  const gfx::RectF bounds = views::View::ConvertRectToTarget(
      contents->parent(), browser_view, gfx::RectF(contents->bounds()));
  // Upstream sets the background's radii only in glass mode, earlier in the
  // same layout; otherwise it has none of its own.
  const gfx::RoundedCornersF upstream =
      in_glass_mode ? contents->GetBackgroundRadii() : gfx::RoundedCornersF();
  // (The active page takes these too, where it isn't Koan's card: then it
  // fills the view and reaches the corners itself.)
  contents->SetBackgroundRadii(LargerCorners(
      upstream, CornersAtWindowCorners(bounds, window, window_corners)));
#endif  // BUILDFLAG(IS_LINUX)
}

}  // namespace koan
