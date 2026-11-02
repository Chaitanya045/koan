// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_swipe.h"

#include <algorithm>
#include <cmath>

#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/events/event_constants.h"

namespace koan {

namespace {

// How far a gesture goes before it counts as horizontal or not. Under
// upstream's own swipe controller's 15px, so that it never sees a swipe of
// ours.
constexpr float kAxisLockDistance = 10.f;

// A scroll stream with no event for this long is over.
constexpr base::TimeDelta kGestureTimeout = base::Milliseconds(300);

// How much of the previous speed a new event keeps.
constexpr float kVelocitySmoothing = 0.5f;

}  // namespace

KoanSpaceSwipe::KoanSpaceSwipe(KoanWorkspaceController* controller)
    : controller_(controller) {}

KoanSpaceSwipe::~KoanSpaceSwipe() = default;

void KoanSpaceSwipe::Reset() {
  state_ = State::kIdle;
  total_x_ = 0;
  total_y_ = 0;
  velocity_x_ = 0;
  last_event_time_ = base::TimeTicks();
}

bool KoanSpaceSwipe::OnScrollEvent(const ui::ScrollEvent& event) {
  const base::TimeTicks now = event.time_stamp();
  const bool lifted =
      event.type() == ui::EventType::kScrollFlingStart ||
      event.type() == ui::EventType::kScrollFlingCancel ||
      event.scroll_event_phase() == ui::ScrollEventPhase::kEnd ||
      event.momentum_phase() != ui::EventMomentumPhase::NONE;
  // A new gesture: its first event, or one after a pause.
  if (event.scroll_event_phase() == ui::ScrollEventPhase::kBegan ||
      (!last_event_time_.is_null() &&
       now - last_event_time_ > kGestureTimeout)) {
    // (One whose end was missed is over: let go of the Space.)
    if (state_ == State::kSwiping) {
      controller_->EndSwipe(0);
    }
    Reset();
  }

  if (lifted) {
    // Fingers up: the swipe ends, with the speed they had. What follows is
    // the system's momentum for it, which the swipe takes until it is over.
    const bool ours = state_ == State::kSwiping || state_ == State::kEnded;
    if (state_ == State::kSwiping) {
      controller_->EndSwipe(velocity_x_);
      state_ = State::kEnded;
    }
    if (event.type() == ui::EventType::kScrollFlingCancel ||
        event.momentum_phase() == ui::EventMomentumPhase::END) {
      Reset();
    } else {
      last_event_time_ = now;
    }
    return ours;
  }

  if (state_ == State::kEnded || state_ == State::kPassthrough) {
    last_event_time_ = now;
    return state_ == State::kEnded;
  }

  if (!last_event_time_.is_null()) {
    const float seconds =
        std::max((now - last_event_time_).InSecondsF(), 0.001);
    velocity_x_ = kVelocitySmoothing * velocity_x_ +
                  (1 - kVelocitySmoothing) * event.x_offset() / seconds;
  }
  last_event_time_ = now;
  total_x_ += event.x_offset();
  total_y_ += event.y_offset();

  if (state_ == State::kIdle) {
    state_ = State::kUndecided;
  }
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
  controller_->UpdateSwipe(total_x_);
  return true;
}

}  // namespace koan
