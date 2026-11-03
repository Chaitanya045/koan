// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_

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
// width it shows at and slid in with a transform on its layer (which runs on
// the compositor and doesn't lay out its children again), instead of getting
// a new width on every frame. The layout therefore sees it as in (1) as soon
// as the animation starts and as gone (0) once it has ended. Otherwise the
// progress is returned as it is, and the layer has no transform.
double LayoutSidebarReveal(views::View* browser_view, double progress);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_
