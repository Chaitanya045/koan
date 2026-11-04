// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_animation.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
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
#include "ui/gfx/geometry/transform.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/animation/animation_sequence_block.h"
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
// shows at; only its layer's transform changes, in a chain of short linear
// transform animations that follow Zen's curve (the compositor can't take a
// `linear()` table, but it runs a chain of linear segments by itself).
class SidebarReveal {
 public:
  SidebarReveal(views::View* browser_view, views::View* region)
      : browser_view_(browser_view), region_(region) {}
  SidebarReveal(const SidebarReveal&) = delete;
  SidebarReveal& operator=(const SidebarReveal&) = delete;
  ~SidebarReveal() = default;

  // Returns the progress the layout uses for the strip's width.
  double Update(double raw) {
    if (raw > last_raw_) {
      SetTarget(1.0);
    } else if (raw < last_raw_) {
      SetTarget(0.0);
    }
    last_raw_ = raw;
    if (!animating_) {
      if (raw == 0.0 && value_ != 0.0) {
        // Left over (e.g. compact mode was switched off while it was out).
        value_ = 0.0;
        target_ = 0.0;
      }
      // Not while it slides: that would stop it.
      SetTransformNow(value_);
    }
    return (raw > 0.0 || value_ > 0.0 || animating_) ? 1.0 : 0.0;
  }

  // Compact mode is off: the strip is where the layout puts it.
  void Reset() {
    ++generation_;
    animating_ = false;
    value_ = 0.0;
    target_ = 0.0;
    last_raw_ = 0.0;
    region_->layer()->SetTransform(gfx::Transform());
  }

 private:
  // Where the strip is now, 0 (hidden) to 1 (out).
  double CurrentValue() const {
    if (!animating_) {
      return value_;
    }
    const double t = (base::TimeTicks::Now() - slide_start_) / slide_duration_;
    return SidebarSlideValue(from_, target_, t);
  }

  void SetTarget(double target) {
    if (target == target_) {
      return;
    }
    const double from = CurrentValue();
    target_ = target;
    ++generation_;
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      animating_ = false;
      value_ = target;
      region_->layer()->SetTransform(TransformFor(value_));
      return;
    }
    from_ = from;
    // Where it was, at once, whatever the layer reports (this also stops a
    // slide under way).
    region_->layer()->SetTransform(TransformFor(from));
    animating_ = true;
    value_ = from;
    slide_start_ = base::TimeTicks::Now();
    slide_duration_ =
        target > from ? kSidebarRevealDuration : kSidebarHideDuration;
    // Zen's spring is smooth: 25 segments follow it to within a fraction of
    // a pixel; the ease needs fewer.
    const int segments = target > from ? 25 : 10;
    views::AnimationBuilder builder;
    builder
        .OnEnded(base::BindOnce(&SidebarReveal::OnSlideEnded,
                                weak_factory_.GetWeakPtr(), generation_))
        .OnAborted(base::DoNothing());
    views::AnimationSequenceBlock* block = nullptr;
    for (int i = 1; i <= segments; ++i) {
      const double t = static_cast<double>(i) / segments;
      // The last one is exactly where it stops (the CSS drops the spring's
      // 1.003 at the end).
      const double value =
          i == segments ? target : SidebarSlideValue(from, target, t);
      block = block ? &block->Then() : &builder.Once();
      block->SetDuration(slide_duration_ / segments)
          .SetTransform(region_->layer(), TransformFor(value));
    }
  }

  void OnSlideEnded(int generation) {
    if (generation != generation_) {
      return;  // Replaced by a newer slide.
    }
    animating_ = false;
    value_ = target_;
    // The layout kept the strip at its width until now.
    browser_view_->InvalidateLayout();
  }

  gfx::Transform TransformFor(double value) const {
    // Layer transforms aren't mirrored with the layout: the sidebar sits on
    // the right with tabs on the right, or in a right-to-left UI, but not in
    // both.
    const bool slides_from_right =
        IsSidebarOnRight(browser_view_) != base::i18n::IsRTL();
    const float offset = static_cast<float>(
        (1.0 - value) * tabs::kVerticalTabStripDefaultUncollapsedWidth);
    gfx::Transform transform;
    transform.Translate(slides_from_right ? offset : -offset, 0);
    return transform;
  }

  void SetTransformNow(double value) {
    const gfx::Transform transform = TransformFor(value);
    if (region_->layer()->transform() != transform) {
      region_->layer()->SetTransform(transform);
    }
  }

  const raw_ptr<views::View> browser_view_;
  const raw_ptr<views::View> region_;
  // How far out the strip is: 0 hidden, 1 out. While it slides, where the
  // slide started.
  double value_ = 0.0;
  double from_ = 0.0;
  double target_ = 0.0;
  bool animating_ = false;
  base::TimeTicks slide_start_;
  base::TimeDelta slide_duration_;
  // Tells a finished slide from one that was replaced.
  int generation_ = 0;
  // Upstream's progress at the last layout, to tell which way it is going.
  double last_raw_ = 0.0;
  base::WeakPtrFactory<SidebarReveal> weak_factory_{this};
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
