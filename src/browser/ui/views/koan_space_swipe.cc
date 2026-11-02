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

// A swipe switches Space when it goes a quarter of the sidebar's width
// (ZenSpacesSwipe.SUCCESS_THRESHOLD).
constexpr float kSuccessFraction = 0.25f;

// How long the fingers' speed carries a swipe on, when judging it. Zen
// weighs the speed by SUCCESS_VELOCITY_CONTRIBUTION (0.5), in Firefox's
// swipe tracker's units, which these events don't have; this is Koan's own,
// chosen so that a quick flick of the fingers (a few hundred pixels a
// second) switches Space from a short way along.
constexpr float kMomentumSeconds = 0.08f;

// Zen's force multiplier is 1 - |x| / (4.5 * width) (4.5 rather than 4 "to add
// a bit of a buffer"), applied while it stays above half.
constexpr float kResistanceWidths = 4.5f;
constexpr float kMinForce = 0.5f;

// How far a gesture goes before it counts as horizontal or not. Under
// upstream's own swipe controller's 15px, so that it never sees a swipe of
// ours.
constexpr float kAxisLockDistance = 10.f;

// Fingers that rested this long before lifting aren't moving, whatever their
// speed was when they stopped (macOS sends nothing while they rest).
constexpr double kRestSeconds = 0.1;

// How much of the previous speed a new event keeps.
constexpr float kVelocitySmoothing = 0.5f;

}  // namespace

float ResistedSwipeOffset(float offset, float width) {
  const float force =
      std::min(1.f, 1.f - std::abs(offset) / (width * kResistanceWidths));
  // Past the point where Zen stops moving the Space the offset is more than
  // the sidebar's width anyway.
  return std::clamp(force > kMinForce ? offset * force : offset, -width, width);
}

float ProjectedSwipeOffset(float offset, float velocity) {
  return offset + velocity * kMomentumSeconds;
}

bool SwipeSucceeds(float projected, float width) {
  return std::abs(projected) >= width * kSuccessFraction;
}

KoanSwipeTracker::KoanSwipeTracker(Delegate* delegate) : delegate_(delegate) {}

KoanSwipeTracker::~KoanSwipeTracker() = default;

void KoanSwipeTracker::Reset() {
  state_ = State::kIdle;
  total_x_ = 0;
  total_y_ = 0;
  velocity_x_ = 0;
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
        delegate_->EndSwipe(0);
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
        const bool rested = seconds - last_move_seconds_ > kRestSeconds;
        delegate_->EndSwipe(rested ? 0.f : velocity_x_);
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
    const double elapsed = std::max(seconds - last_move_seconds_, 0.001);
    velocity_x_ = kVelocitySmoothing * velocity_x_ +
                  (1 - kVelocitySmoothing) * static_cast<float>(dx / elapsed);
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
  delegate_->UpdateSwipe(total_x_);
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

void KoanSpaceSwipe::UpdateSwipe(float offset) {
  controller_->UpdateSwipe(offset);
}

void KoanSpaceSwipe::EndSwipe(float velocity) {
  controller_->EndSwipe(velocity);
}

}  // namespace koan
