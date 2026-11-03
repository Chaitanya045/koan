// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpacesSwipe.mjs (the success threshold, the velocity weight and the
//   resistance of the Space dragged by a swipe)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_space_swipe.h"

#include <algorithm>
#include <cmath>

#include "base/time/time.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/events/event_constants.h"

namespace koan {

namespace {

// Gecko's widget.swipe.pixel-size on macOS: the travel of one gesture amount,
// in points.
constexpr float kSwipePixelSize = 550.f;

// ZenSpacesSwipe.SUCCESS_THRESHOLD and SUCCESS_VELOCITY_CONTRIBUTION.
constexpr float kSuccessThreshold = 0.25f;
constexpr float kSuccessVelocityContribution = 0.5f;

// zen.workspaces.swipe-actions.delta-multiplier.
constexpr float kDeltaMultiplier = 100.f;

// "4.5 instead of 4 to add a bit of a buffer" (ZenSpacesSwipe).
constexpr float kForceWidths = 4.5f;
constexpr float kMinForce = 0.5f;

// Gecko's SwipeTracker::ProcessEvent() takes at least 8 ms between two events
// for a velocity.
constexpr double kMinElapsedSeconds = 0.008;

// How far a gesture goes before it counts as horizontal or not. Under
// upstream's own swipe controller's 15px, so that it never sees a swipe of
// ours.
constexpr float kAxisLockDistance = 10.f;

}  // namespace

float SwipeGestureAmount(float travel_left) {
  return travel_left / kSwipePixelSize;
}

float SwipeTranslate(float amount,
                     float strip_width,
                     SwipeTranslateState& state) {
  const float delta = amount * kDeltaMultiplier;
  float translate = state.last_delta + delta;
  const float force =
      std::min(1.f, 1.f - std::abs(translate) / (strip_width * kForceWidths));
  if (force > kMinForce) {
    translate *= force;
    state.last_delta = delta + (translate - delta) * 0.5f;
  } else {
    translate = state.last_delta;
  }
  return translate;
}

float SwipeOffsetPixels(float translate, float strip_width) {
  return translate / 2.f / 100.f * strip_width;
}

bool SwipeSucceeds(float amount, float velocity) {
  const float target = amount < 0 ? -1.f : 1.f;
  return amount * target + velocity * target * kSuccessVelocityContribution >=
         kSuccessThreshold;
}

KoanSwipeTracker::KoanSwipeTracker(Delegate* delegate) : delegate_(delegate) {}

KoanSwipeTracker::~KoanSwipeTracker() = default;

void KoanSwipeTracker::Reset() {
  state_ = State::kIdle;
  total_x_ = 0;
  total_y_ = 0;
  velocity_ = 0;
  last_move_seconds_ = 0;
  has_moved_ = false;
  in_momentum_ = false;
}

bool KoanSwipeTracker::OnScroll(float dx,
                                float dy,
                                SwipePhase phase,
                                double seconds) {
  switch (phase) {
    case SwipePhase::kBegin:
      // (A swipe whose end was missed is over: let go of the Space.)
      if (state_ == State::kSwiping) {
        delegate_->EndSwipe(SwipeGestureAmount(-total_x_), 0);
      }
      Reset();
      state_ = State::kUndecided;
      return Move(dx, dy, seconds);

    case SwipePhase::kUpdate:
      // Fingers moving without a beginning (or after an end): a new gesture.
      if (state_ == State::kIdle || state_ == State::kEnded) {
        Reset();
        state_ = State::kUndecided;
      }
      return Move(dx, dy, seconds);

    case SwipePhase::kMomentum:
      in_momentum_ = true;
      return state_ == State::kEnded;

    case SwipePhase::kEnd: {
      // The fingers lifting, and, after the momentum, its end.
      const bool lifted = state_ == State::kSwiping;
      if (lifted) {
        delegate_->EndSwipe(SwipeGestureAmount(-total_x_), velocity_);
        state_ = State::kEnded;
      }
      const bool ours = lifted || state_ == State::kEnded;
      if (in_momentum_) {
        Reset();
      }
      return ours;
    }
  }
  return false;
}

bool KoanSwipeTracker::Move(float dx, float dy, double seconds) {
  if (state_ == State::kPassthrough) {
    return false;
  }
  if (has_moved_) {
    const double elapsed =
        std::max(seconds - last_move_seconds_, kMinElapsedSeconds);
    velocity_ = static_cast<float>(SwipeGestureAmount(-dx) / elapsed);
  }
  has_moved_ = true;
  last_move_seconds_ = seconds;
  total_x_ += dx;
  total_y_ += dy;

  if (state_ == State::kUndecided) {
    const float x = std::abs(total_x_);
    const float y = std::abs(total_y_);
    if (y >= kAxisLockDistance && y > x) {
      state_ = State::kPassthrough;
      return false;
    }
    if (x < kAxisLockDistance || x <= y) {
      return false;
    }
    state_ = State::kSwiping;
  }
  delegate_->UpdateSwipe(SwipeGestureAmount(-total_x_));
  return true;
}

KoanSpaceSwipe::KoanSpaceSwipe(KoanWorkspaceController* controller)
    : controller_(controller) {}

KoanSpaceSwipe::~KoanSpaceSwipe() = default;

bool KoanSpaceSwipe::OnScrollEvent(const ui::ScrollEvent& event) {
  SwipePhase phase = SwipePhase::kUpdate;
  switch (event.momentum_phase()) {
    case ui::EventMomentumPhase::BEGAN:
    case ui::EventMomentumPhase::MAY_BEGIN:
      phase = SwipePhase::kBegin;
      break;
    case ui::EventMomentumPhase::INERTIAL_UPDATE:
      phase = SwipePhase::kMomentum;
      break;
    case ui::EventMomentumPhase::END:
    case ui::EventMomentumPhase::BLOCKED:
      phase = SwipePhase::kEnd;
      break;
    case ui::EventMomentumPhase::NONE:
      phase = SwipePhase::kUpdate;
      break;
  }
  return tracker_.OnScroll(
      event.x_offset(), event.y_offset(), phase,
      (event.time_stamp() - base::TimeTicks()).InSecondsF());
}

void KoanSpaceSwipe::UpdateSwipe(float amount) {
  controller_->UpdateSwipe(amount);
}

void KoanSpaceSwipe::EndSwipe(float amount, float velocity) {
  controller_->EndSwipe(amount, velocity);
}

}  // namespace koan
