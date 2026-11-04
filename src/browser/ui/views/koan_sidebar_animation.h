// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_

#include <memory>

namespace views {
class View;
}

namespace koan {

// Whether expanding and collapsing the sidebar (and compact mode hiding it)
// lays the page out once, at its final size, and clips it while the sidebar
// animates: upstream's kClipAndSingleReflow. Upstream does that on Linux and
// Windows only; on macOS (Apple Silicon) it resizes the page on every frame
// (kContinuousFrameResizeWithSurfaceSync), which flickers around the sidebar
// as the page is cleared and redrawn at each size. Called from
// BrowserViewTabbedLayoutImpl.
bool UseSingleReflowSidebarAnimation();

// Called from BrowserViewTabbedLayoutImpl with `progress`, how far upstream's
// expand-on-hover animation has taken the strip out (0 hidden, 1 out), and
// returns the progress the layout uses for the strip's width.
//
// While compact mode hides the sidebar, the strip is laid out once at the
// width it shows at, and a Koan animation (Zen's timings and easing, see
// koan_sidebar_curve.h) slides its layer in and out with a transform, instead
// of the strip getting a new width on every frame; the strip's children are
// not laid out again. The animation runs on the main thread, which sets the
// transform on each tick. Upstream's progress only tells which way it goes.
// The layout therefore sees the strip as in (1) as soon as the animation
// starts and as gone (0) once it has ended. Otherwise the progress is returned
// as it is, and the layer has no transform.
double LayoutSidebarReveal(views::View* browser_view, double progress);

// Keeps the sidebar as it is (out, if compact mode's hover revealed it) while
// this lives: held by a menu opened from the sidebar, as Zen's `has-popup-menu`
// keeps the toolbox open while one of its popups is.
class SidebarHold {
 public:
  ~SidebarHold();
  SidebarHold(const SidebarHold&) = delete;
  SidebarHold& operator=(const SidebarHold&) = delete;

 private:
  friend std::unique_ptr<SidebarHold> HoldSidebarOpen(views::View* view);
  SidebarHold();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// `view` is the BrowserView or a view in its window. Null if there is no
// vertical tab strip.
std::unique_ptr<SidebarHold> HoldSidebarOpen(views::View* view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_
