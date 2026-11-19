// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_wake_policy.h"

namespace koan::agent {

WakeBlock WakeBlockFor(const WakeInputs& inputs) {
  if (!inputs.leased || !inputs.active) {
    return WakeBlock::kNone;
  }
  if (inputs.keep_awake && !inputs.power.saving()) {
    return WakeBlock::kDisplaySleep;
  }
  return WakeBlock::kAppSuspension;
}

WakeBlock WakePolicy::Update(const WakeInputs& inputs, base::TimeTicks now) {
  const WakeBlock wanted = WakeBlockFor(inputs);
  if (wanted >= held_) {
    held_ = wanted;
    wanted_at_ = now;
    return held_;
  }
  if (!inputs.leased) {
    held_ = WakeBlock::kNone;
  } else if (inputs.power.saving() && held_ == WakeBlock::kDisplaySleep) {
    // Never the display in Low Power Mode on battery. Idle sleep stays away
    // while the agent works, and for the hold-off after.
    held_ = inputs.active || now < wanted_at_ + kHoldOff
                ? WakeBlock::kAppSuspension
                : WakeBlock::kNone;
    if (inputs.active) {
      wanted_at_ = now;
    }
  } else if (now >= wanted_at_ + kHoldOff) {
    held_ = wanted;
    wanted_at_ = now;
  }
  return held_;
}

}  // namespace koan::agent
