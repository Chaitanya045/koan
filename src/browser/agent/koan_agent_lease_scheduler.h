// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_LEASE_SCHEDULER_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_LEASE_SCHEDULER_H_

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "koan/browser/agent/koan_agent_limits.h"

// When an agent tab holds the expensive part of its keep-alive lease, the
// visible capturer (.agent/notes/agent-control.md section 8). Pure logic over
// tab ids and a clock; AgentTabLease applies its answers to WebContents.
//
// The cheap part of the lease (no freezing or discarding, foreground priority,
// in-page popups, muted) is held for the whole attachment and is not here.
// The capturer is held:
//  - while a command is in flight on the tab, and for kHoldOff after the last
//    one (an LLM turn is seconds; holding the compositor through it burns power
//    for nothing);
//  - while something holds it open (a wait, a screencast, a recording);
//  - until a Koan.keepAlive runs out (at most 10 minutes; 60 s on battery in
//    Low Power Mode);
//  - for as long as the tab is attached, with `live: always` (refused on
//    battery in Low Power Mode, and a held one is downgraded on entry).
// At most EffectiveLeaseLimit() tabs hold one at a time. A tab over the limit
// is queued and gets it when another releases: its command waits, it never
// fails. A lowered limit (the setting, or Low Power Mode) applies as leases
// are released; nothing is dropped.
namespace koan::agent {

class LeaseScheduler {
 public:
  static constexpr base::TimeDelta kHoldOff = base::Seconds(3);
  static constexpr base::TimeDelta kMaxKeepAlive = base::Minutes(10);

  // What to do with capturers after a call: acquire for these tabs, release
  // for those. A tab is never in both.
  struct Changes {
    Changes();
    Changes(Changes&&);
    Changes& operator=(Changes&&);
    ~Changes();

    std::vector<std::string> acquire;
    std::vector<std::string> release;
    bool empty() const { return acquire.empty() && release.empty(); }
  };

  // Why a request was refused on battery in Low Power Mode
  // (Koan.lowPower {reason, maxLeases, maxKeepAliveSeconds}).
  struct LowPowerRefusal {
    int max_leases = 1;
    int max_keep_alive_seconds = 60;
  };

  LeaseScheduler(int limit_setting, bool halve_in_low_power, PowerState power);
  LeaseScheduler(const LeaseScheduler&) = delete;
  LeaseScheduler& operator=(const LeaseScheduler&) = delete;
  ~LeaseScheduler();

  // The settings changed, or the power state did. Entering Low Power Mode on
  // battery downgrades every `live: always` tab (it then follows the hold-off
  // from `now`). A raised limit grants queued tabs at once.
  Changes SetLimit(int limit_setting,
                   bool halve_in_low_power,
                   base::TimeTicks now);
  Changes SetPower(PowerState power, base::TimeTicks now);
  int limit() const;
  PowerState power() const { return power_; }

  // A command is about to run on `tab` (attached). True if it may run now
  // (the tab holds the capturer); false if it was queued: run it when a later
  // Changes lists the tab in `acquire`.
  bool BeginCommand(const std::string& tab, Changes& changes);
  // The command finished: the hold-off starts.
  void EndCommand(const std::string& tab, base::TimeTicks now);

  // Something keeps the capturer open (a wait, a screencast, a recording);
  // like a command, it may be queued.
  bool BeginHold(const std::string& tab, Changes& changes);
  void EndHold(const std::string& tab, base::TimeTicks now);

  // Koan.keepAlive {seconds}: in (0, 600], or (0, 60] on battery in Low Power
  // Mode. Returns the refusal, or nullopt with the tab's capturer held (or
  // queued) until `now + seconds`.
  std::optional<LowPowerRefusal> KeepAlive(const std::string& tab,
                                           base::TimeDelta duration,
                                           base::TimeTicks now,
                                           Changes& changes);
  // `live: always` for the tab (the client's setting). Refused on battery in
  // Low Power Mode.
  std::optional<LowPowerRefusal> SetLiveAlways(const std::string& tab,
                                               bool live,
                                               Changes& changes);

  // The once-a-second check: releases what ran out, then grants the queue.
  Changes Tick(base::TimeTicks now);
  // The tab detached or closed: released at once, and its place given on.
  Changes Detach(const std::string& tab);

  bool IsCapturing(const std::string& tab) const;
  bool IsQueued(const std::string& tab) const;
  size_t capturing_count() const;

 private:
  struct Tab {
    bool capturing = false;
    bool queued = false;
    int in_flight = 0;
    int holds = 0;
    bool live_always = false;
    base::TimeTicks release_after;
    base::TimeTicks keep_alive_until;
  };

  bool Wants(const Tab& tab, base::TimeTicks now) const;
  // Grants the tab now if there is room, otherwise queues it.
  bool Want(const std::string& id, Tab& tab, Changes& changes);
  void GrantQueue(Changes& changes);
  LowPowerRefusal Refusal() const;

  int limit_setting_;
  bool halve_in_low_power_;
  PowerState power_;
  std::map<std::string, Tab> tabs_;
  std::deque<std::string> queue_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_LEASE_SCHEDULER_H_
