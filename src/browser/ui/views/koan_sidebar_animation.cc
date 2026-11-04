// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_animation.h"

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_sidebar_curve.h"
#include "ui/base/class_property.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace koan {

class SidebarReveal;

}  // namespace koan

DEFINE_UI_CLASS_PROPERTY_TYPE(koan::SidebarReveal*)

namespace koan {

// Slides the sidebar's layer in and out for one window, with Zen's timings and
// easing, from the progress of upstream's expand-on-hover animation (which
// only tells it which way to go). The strip is laid out once, at the width it
// shows at; only its layer's transform changes, every frame, on the compositor.
class SidebarReveal : public gfx::AnimationDelegate {
 public:
  SidebarReveal(views::View* browser_view, views::View* region)
      : browser_view_(browser_view), region_(region), animation_(this) {}
  SidebarReveal(const SidebarReveal&) = delete;
  SidebarReveal& operator=(const SidebarReveal&) = delete;
  ~SidebarReveal() override = default;

  // Returns the progress the layout uses for the strip's width.
  double Update(double raw) {
    if (raw > last_raw_) {
      SetTarget(1.0);
    } else if (raw < last_raw_) {
      SetTarget(0.0);
    }
    last_raw_ = raw;
    if (raw == 0.0 && !animation_.is_animating() && value_ != 0.0) {
      // Left over (e.g. compact mode was switched off while it was out).
      value_ = 0.0;
      target_ = 0.0;
    }
    ApplyTransform();
    return (raw > 0.0 || value_ > 0.0 || animation_.is_animating()) ? 1.0 : 0.0;
  }

  // Compact mode is off: the strip is where the layout puts it.
  void Reset() {
    animation_.Stop();
    value_ = 0.0;
    target_ = 0.0;
    last_raw_ = 0.0;
    region_->layer()->SetTransform(gfx::Transform());
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    if (!IsCompactModeActive(browser_view_)) {
      // Compact mode was switched off while the strip was sliding: it is
      // where the layout puts it from now on.
      value_ = 0.0;
      target_ = 0.0;
    } else {
      value_ = SidebarSlideValue(from_, target_, animation_.GetCurrentValue());
    }
    ApplyTransform();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    value_ = target_;
    ApplyTransform();
    // The layout kept the strip at its width until now.
    browser_view_->InvalidateLayout();
  }

 private:
  void SetTarget(double target) {
    if (target == target_) {
      return;
    }
    target_ = target;
    from_ = value_;
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      animation_.Stop();
      value_ = target_;
      return;
    }
    animation_.SetDuration(target > from_ ? kSidebarRevealDuration
                                          : kSidebarHideDuration);
    animation_.Start();
  }

  void ApplyTransform() {
    // Layer transforms aren't mirrored with the layout: the sidebar sits on
    // the right with tabs on the right, or in a right-to-left UI, but not in
    // both.
    const bool slides_from_right =
        IsSidebarOnRight(browser_view_) != base::i18n::IsRTL();
    const float offset = static_cast<float>(
        (1.0 - value_) * tabs::kVerticalTabStripDefaultUncollapsedWidth);
    gfx::Transform transform;
    transform.Translate(slides_from_right ? offset : -offset, 0);
    if (region_->layer()->transform() != transform) {
      region_->layer()->SetTransform(transform);
    }
  }

  const raw_ptr<views::View> browser_view_;
  const raw_ptr<views::View> region_;
  gfx::LinearAnimation animation_;
  // How far out the strip is: 0 hidden, 1 out (more while it overshoots).
  double value_ = 0.0;
  double from_ = 0.0;
  double target_ = 0.0;
  // Upstream's progress at the last layout, to tell which way it is going.
  double last_raw_ = 0.0;
};

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(SidebarReveal, kSidebarRevealKey)

}  // namespace

struct SidebarHold::Impl : public ui::EventObserver {
  void OnEvent(const ui::Event& event) override {
    lock.reset();
    // Not from inside its own dispatch.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Impl::StopMonitoring, weak_factory.GetWeakPtr()));
  }
  void StopMonitoring() { monitor.reset(); }

  std::unique_ptr<ExpandOnHoverLock> lock;
  raw_ptr<views::Widget> widget = nullptr;
  std::unique_ptr<views::EventMonitor> monitor;
  base::WeakPtrFactory<Impl> weak_factory{this};
};

SidebarHold::SidebarHold() = default;

SidebarHold::~SidebarHold() {
  // A hold goes with its menu, which a closing window destroys too late to
  // collapse the sidebar (see ReleaseExpandOnHoverLockSoon()).
  if (impl_) {
    ReleaseExpandOnHoverLockSoon(impl_->lock);
  }
}

void ReleaseExpandOnHoverLockSoon(std::unique_ptr<ExpandOnHoverLock>& lock) {
  if (!lock) {
    return;
  }
  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    lock.reset();
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(lock));
}

void SidebarHold::ReleaseWhenPointerMoves() {
  views::Widget* widget = impl_->widget;
  if (impl_->monitor) {
    return;
  }
  if (!widget || !impl_->lock) {
    impl_->lock.reset();
    return;
  }
  if (widget->GetWindowBoundsInScreen().Contains(
          display::Screen::Get()->GetCursorScreenPoint())) {
    impl_->lock.reset();
    return;
  }
  impl_->monitor = views::EventMonitor::CreateWindowMonitor(
      impl_.get(), widget->GetNativeWindow(),
      {ui::EventType::kMouseMoved, ui::EventType::kMouseEntered});
}

std::unique_ptr<SidebarHold> HoldSidebarOpen(views::View* view) {
  auto* browser_view = views::AsViewClass<BrowserView>(view);
  if (!browser_view && view && view->GetWidget()) {
    browser_view =
        views::AsViewClass<BrowserView>(view->GetWidget()->client_view());
  }
  TabStripRegionView* region =
      browser_view ? browser_view->tab_strip_view() : nullptr;
  if (!region || region->GetClassName() != "VerticalTabStripRegionView") {
    return nullptr;
  }
  std::unique_ptr<SidebarHold> hold(new SidebarHold());
  hold->impl_ = std::make_unique<SidebarHold::Impl>();
  hold->impl_->widget = browser_view->GetWidget();
  hold->impl_->lock =
      region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  return hold;
}

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

  SidebarReveal* reveal = region->GetProperty(kSidebarRevealKey);
  if (!IsCompactModeActive(browser_view)) {
    if (reveal) {
      reveal->Reset();
    }
    return progress;
  }
  if (!reveal) {
    reveal = region->SetProperty(
        kSidebarRevealKey,
        std::make_unique<SidebarReveal>(browser_view, region));
  }
  return reveal->Update(progress);
}

}  // namespace koan
