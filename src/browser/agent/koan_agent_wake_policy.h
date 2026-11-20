// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_WAKE_POLICY_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_WAKE_POLICY_H_

#include "base/time/time.h"
#include "koan/browser/agent/koan_agent_limits.h"

// Which macOS power assertion Koan takes for agents
// (.agent/notes/agent-control.md section 8, "Display sleep"). Pure logic;
// AgentLeaseManager applies it through the device service's WakeLock, which
// is Chromium's PowerSaveBlocker (IOPMAssertionCreateWithName, so it shows in
// `pmset -g assertions` under the reason string).
//
// An agent is at work while a tab of its holds the capturer for a command in
// flight, a screencast or a keep-alive (LeaseScheduler::IsActive()). Then:
//  - with the client's `keep-awake` capability: the display does not sleep
//    (kPreventDisplaySleep), or the window-less tab's CVDisplayLink would stop
//    and the page with it;
//  - without it: idle sleep is prevented (kPreventAppSuspension, macOS's
//    NoIdleSleep), so a command in flight is not cut off; the display sleeps;
//  - on battery in Low Power Mode, never the display: kPreventAppSuspension
//    only, whatever the capability, and dropped to it at once on entry.
// Nothing while no agent is at work: held 3 s more after the last work (as
// the capturer's hold-off), and dropped at once when no tab is leased (no
// agent is connected). A closed lid is left to macOS: neither assertion keeps
// a MacBook awake with its lid closed (clamshell sleep), and none is wanted.
namespace koan::agent {

enum class WakeBlock {
  kNone,
  // device::mojom::WakeLockType::kPreventAppSuspension.
  kAppSuspension,
  // device::mojom::WakeLockType::kPreventDisplaySleep.
  kDisplaySleep,
};

// The reason macOS shows for the assertion.
inline constexpr char kWakeReason[] = "Koan: an agent is working";

struct WakeInputs {
  // Some tab is leased: an agent is connected.
  bool leased = false;
  // Some agent is at work in a tab.
  bool active = false;
  // Some agent at work has the `keep-awake` capability.
  bool keep_awake = false;
  PowerState power;
};

// What the inputs want at this moment, before the hold-off.
WakeBlock WakeBlockFor(const WakeInputs& inputs);

class WakePolicy {
 public:
  static constexpr base::TimeDelta kHoldOff = base::Seconds(3);

  // The assertion to hold now. A stronger one is taken at once; a weaker one
  // (or none) only once the stronger was not wanted for kHoldOff, except that
  // no lease, or Low Power Mode on battery, lowers it at once.
  WakeBlock Update(const WakeInputs& inputs, base::TimeTicks now);
  WakeBlock held() const { return held_; }

 private:
  WakeBlock held_ = WakeBlock::kNone;
  // When `held_` was last wanted.
  base::TimeTicks wanted_at_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_WAKE_POLICY_H_
