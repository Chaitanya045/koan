// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_loading_bar.h"

#include <algorithm>
#include <cmath>

#include "ui/gfx/geometry/cubic_bezier.h"

namespace koan {

namespace {

// Zen's timings (ZenProgressBar.sys.mjs, zen-single-components.css,
// zen-animations.css).
constexpr base::TimeDelta kFadeIn = base::Milliseconds(400);
constexpr float kFadeInOpacity = 0.8f;
// zen-progress-bar-pulse: 1 s, alternate, ease-in-out.
constexpr base::TimeDelta kPulse = base::Seconds(1);
constexpr float kPulseScaleFrom = 0.85f;
constexpr float kPulseScaleTo = 0.95f;
constexpr float kPulseOpacityFrom = 0.6f;
// zen-progress-bar-settle: 300 ms, ease-out.
constexpr base::TimeDelta kSettle = base::Milliseconds(300);
// zen-progress-bar-long-load: 1 s, ease-in-out, after a 300 ms delay; the
// sweeping part is 75% of the track and moves by 4/3 of its own width each
// side of its start.
constexpr base::TimeDelta kSweep = base::Seconds(1);
constexpr float kSweepWidth = 0.75f;
constexpr float kSweepTravel = 4.f / 3.f;
// The way out: 300 ms, linear (Web Animations' default), to scaleX(0.8).
constexpr base::TimeDelta kHide = base::Milliseconds(300);
constexpr float kHideScaleX = 0.8f;

// CSS's ease-in-out and ease-out.
double EaseInOut(double t) {
  static const gfx::CubicBezier bezier(0.42, 0, 0.58, 1);
  return bezier.Solve(std::clamp(t, 0.0, 1.0));
}
double EaseOut(double t) {
  static const gfx::CubicBezier bezier(0, 0, 0.58, 1);
  return bezier.Solve(std::clamp(t, 0.0, 1.0));
}

// The fraction of `period` `elapsed` is into its current cycle.
double CycleFraction(base::TimeDelta elapsed, base::TimeDelta period) {
  return std::fmod(std::max(0.0, elapsed / period), 1.0);
}

float Lerp(float from, float to, double t) {
  return from + (to - from) * static_cast<float>(t);
}

// A `width` by `height` rect centred on the anchor.
gfx::RectF Centered(float width, float height) {
  return gfx::RectF(-width / 2, -height / 2, width, height);
}

}  // namespace

KoanLoadingBar::KoanLoadingBar() = default;
KoanLoadingBar::~KoanLoadingBar() = default;

void KoanLoadingBar::Show(base::TimeTicks now) {
  if (IsShowing()) {
    return;
  }
  shown_ = now;
  long_load_.reset();
  hidden_.reset();
}

void KoanLoadingBar::StartLongLoad(base::TimeTicks now) {
  if (IsShowing() && !long_load_) {
    long_load_ = now;
  }
}

void KoanLoadingBar::Hide(base::TimeTicks now) {
  if (!IsShowing()) {
    return;
  }
  hide_from_ = LoadingFrameAt(now);
  hidden_ = now;
}

void KoanLoadingBar::Reset() {
  shown_.reset();
  long_load_.reset();
  hidden_.reset();
}

bool KoanLoadingBar::IsDone(base::TimeTicks now) const {
  if (!shown_) {
    return true;
  }
  if (!hidden_) {
    return false;
  }
  return reduced_motion_ || now - *hidden_ >= kHide;
}

KoanLoadingBar::Frame KoanLoadingBar::LoadingFrameAt(
    base::TimeTicks now) const {
  Frame frame;
  if (!shown_) {
    return frame;
  }
  if (long_load_) {
    // The settle starts from the bar's own style (scale 0 at its top left
    // corner, 5rem wide) and ends centred, 10rem wide, opaque. Its translate
    // is of the growing width, unscaled; the scale is about the top left.
    const base::TimeDelta elapsed = now - *long_load_;
    const double settle = reduced_motion_ ? 1.0 : EaseOut(elapsed / kSettle);
    const float width = Lerp(kWidth, kLongLoadWidth, settle);
    const float scale = static_cast<float>(settle);
    frame.bar =
        gfx::RectF(-width / 2, -kHeight / 2, width * scale, kHeight * scale);
    frame.opacity = 1;
    frame.long_load = true;
    if (!reduced_motion_ && elapsed >= kSettle) {
      const float sweep_width = kSweepWidth * frame.bar.width();
      const double travel = EaseInOut(CycleFraction(elapsed - kSettle, kSweep));
      frame.sweep =
          gfx::RectF(frame.bar.x() + Lerp(-kSweepTravel, kSweepTravel, travel) *
                                         sweep_width,
                     frame.bar.y(), sweep_width, frame.bar.height());
    }
    return frame;
  }
  if (reduced_motion_) {
    frame.bar = Centered(kWidth * kPulseScaleTo, kHeight * kPulseScaleTo);
    frame.opacity = 1;
    return frame;
  }
  // The pulse, alternating: there and back every two periods.
  const base::TimeDelta elapsed = now - *shown_;
  const double cycle = CycleFraction(elapsed, 2 * kPulse) * 2;
  const double pulse = EaseInOut(cycle <= 1 ? cycle : 2 - cycle);
  const float scale = Lerp(kPulseScaleFrom, kPulseScaleTo, pulse);
  frame.bar = Centered(kWidth * scale, kHeight * scale);
  // The fade in overrides the pulse's opacity while it runs.
  frame.opacity = elapsed < kFadeIn
                      ? kFadeInOpacity * static_cast<float>(elapsed / kFadeIn)
                      : Lerp(kPulseOpacityFrom, 1, pulse);
  return frame;
}

KoanLoadingBar::Frame KoanLoadingBar::FrameAt(base::TimeTicks now) const {
  if (IsDone(now)) {
    return Frame();
  }
  if (!hidden_) {
    return LoadingFrameAt(now);
  }
  // Going: from where Hide() found it to 80% of the width it had unscaled,
  // its full height, transparent, centred. The sweep goes on inside.
  const double t = (now - *hidden_) / kHide;
  const Frame& from = hide_from_;
  const float unscaled_width = from.long_load ? kLongLoadWidth : kWidth;
  Frame frame;
  frame.long_load = from.long_load;
  frame.bar = Centered(Lerp(from.bar.width(), kHideScaleX * unscaled_width, t),
                       Lerp(from.bar.height(), kHeight, t));
  frame.opacity = Lerp(from.opacity, 0, t);
  const Frame loading = LoadingFrameAt(now);
  if (!loading.sweep.IsEmpty() && !loading.bar.IsEmpty()) {
    const float ratio = frame.bar.width() / loading.bar.width();
    frame.sweep = gfx::RectF(
        frame.bar.x() + (loading.sweep.x() - loading.bar.x()) * ratio,
        frame.bar.y(), loading.sweep.width() * ratio, frame.bar.height());
  }
  return frame;
}

}  // namespace koan
