// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_animation.h"

#include <array>

#include "base/i18n/rtl.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "ui/base/class_property.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view_utils.h"

namespace koan {

class SidebarReveal;

}  // namespace koan

DEFINE_UI_CLASS_PROPERTY_TYPE(koan::SidebarReveal*)

namespace koan {

namespace {

// Zen's `--zen-compact-mode-func` (src/zen/compact-mode/sidebar.inc.css): the
// revealed sidebar's `translate` easing, a spring-shaped `linear()` curve
// with its value at 0%, 1%, ... 100% of the time. It overshoots by 1% and
// ends at 1.003423, where the CSS drops the translation.
constexpr std::array<float, 101> kZenCompactModeFunc = {
    0.0f,      0.002748f, 0.010544f, 0.022757f, 0.038804f, 0.058151f, 0.080308f,
    0.104828f, 0.131301f, 0.159358f, 0.188662f, 0.21891f,  0.249828f, 0.281172f,
    0.312724f, 0.344288f, 0.375693f, 0.40679f,  0.437447f, 0.467549f, 0.497f,
    0.525718f, 0.553633f, 0.580688f, 0.60684f,  0.632052f, 0.656298f, 0.679562f,
    0.701831f, 0.723104f, 0.743381f, 0.76267f,  0.780983f, 0.798335f, 0.814744f,
    0.830233f, 0.844826f, 0.858549f, 0.87143f,  0.883498f, 0.894782f, 0.905314f,
    0.915125f, 0.924247f, 0.93271f,  0.940547f, 0.947787f, 0.954463f, 0.960603f,
    0.966239f, 0.971397f, 0.976106f, 0.980394f, 0.984286f, 0.987808f, 0.990984f,
    0.993837f, 0.99639f,  0.998664f, 1.000679f, 1.002456f, 1.004011f, 1.005363f,
    1.006528f, 1.007522f, 1.008359f, 1.009054f, 1.009618f, 1.010065f, 1.010405f,
    1.010649f, 1.010808f, 1.01089f,  1.010904f, 1.010857f, 1.010757f, 1.010611f,
    1.010425f, 1.010205f, 1.009955f, 1.009681f, 1.009387f, 1.009077f, 1.008754f,
    1.008422f, 1.008083f, 1.00774f,  1.007396f, 1.007052f, 1.00671f,  1.006372f,
    1.00604f,  1.005713f, 1.005394f, 1.005083f, 1.004782f, 1.004489f, 1.004207f,
    1.003935f, 1.003674f, 1.003423f,
};

// `--zen-compact-mode-time`.
constexpr base::TimeDelta kRevealDuration = base::Milliseconds(250);
// The hidden sidebar's `transition: translate 0.15s ease`.
constexpr base::TimeDelta kHideDuration = base::Milliseconds(150);

double SpringAt(double t) {
  const double position = std::clamp(t, 0.0, 1.0) * 100.0;
  const size_t index = std::min<size_t>(static_cast<size_t>(position), 99);
  const double fraction = position - static_cast<double>(index);
  return kZenCompactModeFunc[index] +
         fraction *
             (kZenCompactModeFunc[index + 1] - kZenCompactModeFunc[index]);
}

// CSS `ease`.
double EaseAt(double t) {
  static const gfx::CubicBezier ease(0.25, 0.1, 0.25, 1.0);
  return ease.Solve(std::clamp(t, 0.0, 1.0));
}

}  // namespace

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
    const double t = animation_.GetCurrentValue();
    value_ =
        from_ + (target_ - from_) * (target_ > from_ ? SpringAt(t) : EaseAt(t));
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
    animation_.SetDuration(target > from_ ? kRevealDuration : kHideDuration);
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
