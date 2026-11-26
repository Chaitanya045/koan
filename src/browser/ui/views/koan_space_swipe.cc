// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_swipe.h"

#include "base/time/time.h"
#include "koan/browser/ui/views/koan_scroll_direction.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/events/event_constants.h"

namespace koan {

KoanSpaceSwipe::KoanSpaceSwipe(KoanWorkspaceController* controller)
    : controller_(controller) {}

KoanSpaceSwipe::~KoanSpaceSwipe() = default;

namespace {

SwipePhase GetPhase(const ui::ScrollEvent& event) {
  switch (event.momentum_phase()) {
    case ui::EventMomentumPhase::BEGAN:
    case ui::EventMomentumPhase::MAY_BEGIN:
      return SwipePhase::kBegin;
    case ui::EventMomentumPhase::INERTIAL_UPDATE:
      return SwipePhase::kMomentum;
    case ui::EventMomentumPhase::END:
    case ui::EventMomentumPhase::BLOCKED:
      return SwipePhase::kEnd;
    case ui::EventMomentumPhase::NONE:
      return SwipePhase::kUpdate;
  }
  return SwipePhase::kUpdate;
}

double GetSeconds(const ui::ScrollEvent& event) {
  return (event.time_stamp() - base::TimeTicks()).InSecondsF();
}

}  // namespace

bool KoanSpaceSwipe::OnScrollEvent(const ui::ScrollEvent& event) {
  // The swipe follows the fingers, not the system's natural scrolling setting
  // (as Gecko's swipe tracker, and Zen with it, and Chromium's own swipe back
  // and forward do): the offsets are turned round when the system had.
  const float dx =
      ScrollFollowsFingers(event) ? event.x_offset() : -event.x_offset();
  return tracker_.OnScroll(dx, event.y_offset(), GetPhase(event),
                           GetSeconds(event));
}

void KoanSpaceSwipe::CaptureScrolls(gfx::NativeWindow window) {
  if (!capture_) {
    capture_ = KoanScrollCapture::Create(window, this);
  }
  capturing_ = true;
}

bool KoanSpaceSwipe::IsCapturing() const {
  return capturing_;
}

bool KoanSpaceSwipe::TakeScroll(const ui::ScrollEvent& event) {
  return tracker_.Owns(GetPhase(event), GetSeconds(event)) &&
         OnScrollEvent(event);
}

void KoanSpaceSwipe::UpdateSwipe(float amount) {
  controller_->UpdateSwipe(amount);
}

void KoanSpaceSwipe::EndSwipe(float amount, float velocity) {
  controller_->EndSwipe(amount, velocity);
}

}  // namespace koan
