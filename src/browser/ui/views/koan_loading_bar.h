// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LOADING_BAR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LOADING_BAR_H_

#include <optional>

#include "base/time/time.h"
#include "ui/gfx/geometry/rect_f.h"

namespace koan {

// Zen's page-load progress bar as a function of time (ZenProgressBar.sys.mjs,
// #zen-loading-progress-bar in zen-single-components.css, the
// zen-progress-bar-* keyframes in zen-animations.css): a pill 5rem by 0.4rem
// that fades in to 80% over 400 ms and pulses (scale 0.85 to 0.95, opacity
// 60% to 100%, 1 s each way); once the load has taken 3 s, the "long load":
// it settles over 300 ms into a 10rem track with a part 75% as wide sweeping
// across it every second; it goes by shrinking to 80% of its width while it
// fades out over 300 ms. Plain logic, so that the unit tests can drive it.
class KoanLoadingBar {
 public:
  // One frame. Rects are relative to the bar's anchor (the middle of its
  // place).
  struct Frame {
    gfx::RectF bar;
    float opacity = 0;
    // The bar is the long load's track (Zen's colour for it, not the bar's).
    bool long_load = false;
    // The long load's sweeping part, to be clipped to `bar`. Empty before it
    // starts.
    gfx::RectF sweep;
  };

  // Zen's sizes at 16px to the rem.
  static constexpr float kWidth = 80;
  static constexpr float kLongLoadWidth = 160;
  static constexpr float kHeight = 6.4f;
  // The largest the bar gets, for a surface to draw it on.
  static constexpr float kMaxWidth = kLongLoadWidth;
  // WAIT_BEFORE_SHOWING_LONG_LOAD.
  static constexpr base::TimeDelta kLongLoadDelay = base::Seconds(3);

  KoanLoadingBar();
  KoanLoadingBar(const KoanLoadingBar&) = delete;
  KoanLoadingBar& operator=(const KoanLoadingBar&) = delete;
  ~KoanLoadingBar();

  // Reduced motion: no fades, pulse or sweep, and the bar goes at once.
  void set_reduced_motion(bool reduced_motion) {
    reduced_motion_ = reduced_motion;
  }

  // A load starts: the bar comes in (or stays, if it is in already). A bar
  // on its way out starts over, as Zen's waits for the old one to go.
  void Show(base::TimeTicks now);
  // The load has taken kLongLoadDelay.
  void StartLongLoad(base::TimeTicks now);
  // The load is over: the bar goes.
  void Hide(base::TimeTicks now);
  // The bar is gone at once (Zen's display: none).
  void Reset();

  // Shown, and not going.
  bool IsShowing() const { return shown_.has_value() && !hidden_.has_value(); }
  bool IsLongLoad() const { return long_load_.has_value(); }
  // Nothing to draw (never shown, or gone).
  bool IsDone(base::TimeTicks now) const;

  Frame FrameAt(base::TimeTicks now) const;

 private:
  // The frame while loading, ignoring Hide().
  Frame LoadingFrameAt(base::TimeTicks now) const;

  bool reduced_motion_ = false;
  std::optional<base::TimeTicks> shown_;
  std::optional<base::TimeTicks> long_load_;
  std::optional<base::TimeTicks> hidden_;
  // Where Hide() found the bar.
  Frame hide_from_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LOADING_BAR_H_
