// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_

#include <memory>

class ExpandOnHoverLock;

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
// not laid out again. The slide is a chain of short linear transform animations
// that the compositor runs. Upstream's progress only tells which way it goes.
// The layout therefore sees the strip as in (1) as soon as the animation
// starts and as gone (0) once it has ended. Otherwise the progress is returned
// as it is, and the layer has no transform.
double LayoutSidebarReveal(views::View* browser_view, double progress);

// The width the strip shows at when it is out on hover, for
// BrowserViewTabbedLayoutImpl in place of upstream's
// tabs::kVerticalTabStripDefaultUncollapsedWidth (240): while compact mode
// hides the sidebar, the sidebar's own width (Zen's 230 by default, or what
// the user dragged it to), as Zen's compact mode shows it; otherwise
// upstream's.
int SidebarRevealWidth(views::View* browser_view);

// Compact mode has just hidden the docked sidebar (the page already takes its
// place): the strip is shown out, over the page, and slides away in Zen's
// 0.1 s (kSidebarToggleDuration), as Zen's animateCompactMode() does. Nothing
// unless compact mode is active in the window.
void SlideSidebarAway(views::View* browser_view);

// Whether compact mode's sidebar is out (revealed) or on its way out, in the
// window of `browser_view`. False without compact mode.
bool IsSidebarShownOut(views::View* browser_view);

// Compact mode has just stopped hiding the sidebar: the strip is docked at
// once, without upstream's expand animation, so that the page is laid out
// beside it in one step. With `slide_in` (the sidebar was hidden) the strip
// then slides in from the window's edge in Zen's 0.1 s; without it (the
// sidebar was out) it just stays where it is, as Zen's animateCompactMode()
// doesn't animate a sidebar that is already shown.
void DockSidebarAfterCompact(views::View* browser_view, bool slide_in);

// The next time the sidebar is revealed (the strip going out) it just is out,
// with no slide, once; `snap` false takes that back. For an empty tab holding
// the sidebar as compact mode hides it: Zen applies that state without
// animation.
void SnapNextSidebarReveal(views::View* browser_view, bool snap);

// Keeps the sidebar as it is (out, if compact mode's hover revealed it) while
// this lives: held by a menu opened from the sidebar, as Zen's `has-popup-menu`
// keeps the toolbox open while one of its popups is.
class SidebarHold {
 public:
  ~SidebarHold();

  // The menu has closed. Zen removes `has-popup-menu` at once if the pointer
  // is over the window, and otherwise at the pointer's next move over it, so a
  // sidebar whose menu closed with the pointer elsewhere doesn't snap shut
  // under it. May be called again (the runner's callback, then after
  // RunMenuAt()): later calls find the hold released or waiting.
  void ReleaseWhenPointerMoves();

  SidebarHold(const SidebarHold&) = delete;
  SidebarHold& operator=(const SidebarHold&) = delete;

 private:
  friend std::unique_ptr<SidebarHold> HoldSidebarOpen(views::View* view);
  SidebarHold();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Lets go of `lock`, a lock on the sidebar (may be null), after the current
// task. For teardown: closing a window destroys the browser's animation
// controller before the BrowserView, and letting go of a lock makes the
// sidebar's region decide again and collapse with an animation, which needs it.
// By the time the task runs the region is gone and the lock is inert (the
// region clears its locks in its destructor).
void ReleaseExpandOnHoverLockSoon(std::unique_ptr<ExpandOnHoverLock>& lock);

// `view` is the BrowserView or a view in its window. Null if there is no
// vertical tab strip.
std::unique_ptr<SidebarHold> HoldSidebarOpen(views::View* view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_ANIMATION_H_
