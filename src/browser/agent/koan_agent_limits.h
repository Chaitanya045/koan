// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_LIMITS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_LIMITS_H_

#include "base/time/time.h"

namespace base {
class Value;
}

// Agent control's limits that the user can change
// (.agent/notes/agent-control.md section 6.1), and how a stored value is read.
// Pure logic: the browser reads the prefs and passes the values in.
namespace koan::agent {

struct LimitSpec {
  int default_value;
  int min;
  int max;
};

// Awake (leased) agent tabs at a time.
inline constexpr LimitSpec kLeasedTabsLimit = {4, 1, 16};
// Approval prompts a minute, per user.
inline constexpr LimitSpec kPromptsLimit = {3, 1, 10};
// Failed attempts before a peer is blocked.
inline constexpr LimitSpec kFailuresLimit = {5, 1, 20};
// Above this many awake tabs the settings row warns about memory and power.
inline constexpr int kLeasedTabsWarningAbove = 8;

// How long a peer that reached the failure limit is blocked, in the browser
// (the pure-logic RateLimiter keeps its own default, which its tests use).
inline constexpr base::TimeDelta kBrowserBlockDuration = base::Minutes(5);
// On battery with Low Power Mode on, Koan.keepAlive is limited to this, and
// `live: always` is refused.
inline constexpr base::TimeDelta kMaxKeepAliveLowPower = base::Seconds(60);

// The stored value if it is an integer in [spec.min, spec.max], and the
// default otherwise: a missing, non-integer (a double, a string, a bool, a
// list), 0, negative or out-of-range value is never used, and is never rounded
// to the nearest edge (a hand-edited 9999 is not 16).
int ReadLimit(const base::Value* stored, const LimitSpec& spec);
// A boolean pref, or `default_value` if it is missing or not a bool.
bool ReadSwitch(const base::Value* stored, bool default_value);

// The power state the lease limit depends on.
struct PowerState {
  bool on_battery = false;
  bool low_power_mode = false;

  bool saving() const { return on_battery && low_power_mode; }
  bool operator==(const PowerState&) const = default;
};

// How many tabs may hold a lease now: the setting, halved (rounding down, at
// least 1) on battery with Low Power Mode on when `halve_in_low_power` is set.
int EffectiveLeaseLimit(int setting, bool halve_in_low_power, PowerState power);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_LIMITS_H_
