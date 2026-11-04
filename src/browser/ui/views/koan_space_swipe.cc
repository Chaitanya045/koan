// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_swipe.h"

#include "base/time/time.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/events/event_constants.h"

namespace koan {

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
