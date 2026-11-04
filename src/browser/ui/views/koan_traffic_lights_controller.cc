// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_traffic_lights_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "koan/browser/ui/views/koan_traffic_lights.h"
#include "ui/views/view.h"

namespace koan {

KoanTrafficLightsController::KoanTrafficLightsController(
    BrowserView* browser_view,
    base::RepeatingClosure on_changed)
    : browser_view_(browser_view), on_changed_(std::move(on_changed)) {}

KoanTrafficLightsController::~KoanTrafficLightsController() = default;

void KoanTrafficLightsController::OnBrowserViewLayout() {
  // The sidebar's region is rebuilt on a switch between vertical and
  // horizontal tabs.
  ObserveSidebar();
  // Fullscreen rebuilds the title bar: AppKit lays the lights out itself and
  // may show them again, and what the title bar was laid out with is gone. Both
  // ways, the caller shows or hides them again, and the height is applied
  // again once out of it.
  const bool fullscreen = browser_view_->IsFullscreen();
  if (fullscreen != was_fullscreen_) {
    was_fullscreen_ = fullscreen;
    applied_height_.reset();
    on_changed_.Run();
  }
  if (fullscreen) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  if (!titlebar_cleared_ && browser_view_->GetWidget() &&
      OwnsTitlebar(browser_view_)) {
    titlebar_cleared_ = true;
    ClearTitlebar(browser_view_);
  }
#endif
  if (update_pending_ ||
      (applied_height_ && *applied_height_ == GetTrafficLightsTitlebarHeight(
                                                  browser_view_.get()))) {
    return;
  }
  // Not from inside layout: it resizes the window. (The height is read when
  // it runs, so a change in between counts.)
  update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanTrafficLightsController::LayOutTrafficLights,
                     weak_factory_.GetWeakPtr()));
}

bool KoanTrafficLightsController::IsSidebarShown() const {
  const views::View* region = browser_view_->tab_strip_view();
  return region && region->GetVisible() && region->width() > 0;
}

void KoanTrafficLightsController::OnViewBoundsChanged(
    views::View* observed_view) {
  NotifyIfSidebarChanged();
}

void KoanTrafficLightsController::OnViewVisibilityChanged(
    views::View* observed_view,
    views::View* starting_view,
    bool visible) {
  NotifyIfSidebarChanged();
}

void KoanTrafficLightsController::OnViewIsDeleting(views::View* observed_view) {
  sidebar_observation_.Reset();
}

void KoanTrafficLightsController::ObserveSidebar() {
  views::View* region = browser_view_->tab_strip_view();
  if (sidebar_observation_.GetSource() != region) {
    sidebar_observation_.Reset();
    if (region) {
      sidebar_observation_.Observe(region);
    }
  }
  NotifyIfSidebarChanged();
}

void KoanTrafficLightsController::NotifyIfSidebarChanged() {
  const bool shown = IsSidebarShown();
  if (shown != std::exchange(sidebar_shown_, shown)) {
    on_changed_.Run();
  }
}

void KoanTrafficLightsController::LayOutTrafficLights() {
  update_pending_ = false;
  BrowserWidget* widget = browser_view_->browser_widget();
  BrowserFrameView* frame = widget ? widget->GetFrameView() : nullptr;
  // (Out of fullscreen, OnBrowserViewLayout() asks again.)
  if (!frame || browser_view_->IsFullscreen()) {
    return;
  }
  // BrowserFrameViewMac makes AppKit lay the title bar out again, asking for
  // the height now, and toggling the window's style mask, which may bring the
  // traffic lights back.
  applied_height_ = GetTrafficLightsTitlebarHeight(browser_view_.get());
  frame->OnTabStripStateChanged();
#if BUILDFLAG(IS_MAC)
  if (OwnsTitlebar(browser_view_)) {
    ClearTitlebar(browser_view_);
  }
#endif
  on_changed_.Run();
}

}  // namespace koan
