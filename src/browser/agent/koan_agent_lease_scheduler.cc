// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_lease_scheduler.h"

#include <algorithm>
#include <utility>

namespace koan::agent {

LeaseScheduler::Changes::Changes() = default;
LeaseScheduler::Changes::Changes(Changes&&) = default;
LeaseScheduler::Changes& LeaseScheduler::Changes::operator=(Changes&&) =
    default;
LeaseScheduler::Changes::~Changes() = default;

LeaseScheduler::LeaseScheduler(int limit_setting,
                               bool halve_in_low_power,
                               PowerState power)
    : limit_setting_(limit_setting),
      halve_in_low_power_(halve_in_low_power),
      power_(power) {}

LeaseScheduler::~LeaseScheduler() = default;

int LeaseScheduler::limit() const {
  return EffectiveLeaseLimit(limit_setting_, halve_in_low_power_, power_);
}

LeaseScheduler::Changes LeaseScheduler::SetLimit(int limit_setting,
                                                 bool halve_in_low_power,
                                                 base::TimeTicks now) {
  limit_setting_ = limit_setting;
  halve_in_low_power_ = halve_in_low_power;
  Changes changes;
  GrantQueue(changes);
  return changes;
}

LeaseScheduler::Changes LeaseScheduler::SetPower(PowerState power,
                                                 base::TimeTicks now) {
  const bool entering = power.saving() && !power_.saving();
  power_ = power;
  if (entering) {
    // Downgraded, not dropped: they follow the hold-off from now on.
    for (auto& [id, tab] : tabs_) {
      if (tab.live_always) {
        tab.live_always = false;
        tab.release_after = std::max(tab.release_after, now + kHoldOff);
      }
      tab.keep_alive_until =
          std::min(tab.keep_alive_until, now + kMaxKeepAliveLowPower);
    }
  }
  Changes changes;
  GrantQueue(changes);
  return changes;
}

bool LeaseScheduler::BeginCommand(const std::string& id, Changes& changes) {
  Tab& tab = tabs_[id];
  ++tab.in_flight;
  return Want(id, tab, changes);
}

void LeaseScheduler::EndCommand(const std::string& id, base::TimeTicks now) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return;
  }
  it->second.in_flight = std::max(it->second.in_flight - 1, 0);
  it->second.release_after = now + kHoldOff;
}

bool LeaseScheduler::BeginHold(const std::string& id, Changes& changes) {
  Tab& tab = tabs_[id];
  ++tab.holds;
  return Want(id, tab, changes);
}

void LeaseScheduler::EndHold(const std::string& id, base::TimeTicks now) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return;
  }
  it->second.holds = std::max(it->second.holds - 1, 0);
  it->second.release_after = now + kHoldOff;
}

std::optional<LeaseScheduler::LowPowerRefusal> LeaseScheduler::KeepAlive(
    const std::string& id,
    base::TimeDelta duration,
    base::TimeTicks now,
    Changes& changes) {
  if (power_.saving() && duration > kMaxKeepAliveLowPower) {
    return Refusal();
  }
  if (!duration.is_positive()) {
    return std::nullopt;
  }
  Tab& tab = tabs_[id];
  tab.keep_alive_until =
      std::max(tab.keep_alive_until, now + std::min(duration, kMaxKeepAlive));
  Want(id, tab, changes);
  return std::nullopt;
}

std::optional<LeaseScheduler::LowPowerRefusal> LeaseScheduler::SetLiveAlways(
    const std::string& id,
    bool live,
    Changes& changes) {
  if (live && power_.saving()) {
    return Refusal();
  }
  Tab& tab = tabs_[id];
  tab.live_always = live;
  if (live) {
    Want(id, tab, changes);
  }
  return std::nullopt;
}

LeaseScheduler::Changes LeaseScheduler::Tick(base::TimeTicks now) {
  Changes changes;
  for (auto& [id, tab] : tabs_) {
    if (tab.capturing && !Wants(tab, now)) {
      tab.capturing = false;
      changes.release.push_back(id);
    }
  }
  GrantQueue(changes);
  return changes;
}

LeaseScheduler::Changes LeaseScheduler::Detach(const std::string& id) {
  Changes changes;
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return changes;
  }
  if (it->second.capturing) {
    changes.release.push_back(id);
  }
  tabs_.erase(it);
  std::erase(queue_, id);
  GrantQueue(changes);
  return changes;
}

bool LeaseScheduler::IsCapturing(const std::string& id) const {
  auto it = tabs_.find(id);
  return it != tabs_.end() && it->second.capturing;
}

bool LeaseScheduler::IsQueued(const std::string& id) const {
  auto it = tabs_.find(id);
  return it != tabs_.end() && it->second.queued;
}

size_t LeaseScheduler::capturing_count() const {
  return std::ranges::count_if(
      tabs_, [](const auto& item) { return item.second.capturing; });
}

bool LeaseScheduler::Wants(const Tab& tab, base::TimeTicks now) const {
  return tab.in_flight > 0 || tab.holds > 0 || tab.live_always ||
         now < tab.keep_alive_until || now < tab.release_after;
}

bool LeaseScheduler::Want(const std::string& id, Tab& tab, Changes& changes) {
  if (tab.capturing) {
    return true;
  }
  if (!tab.queued && queue_.empty() &&
      capturing_count() < static_cast<size_t>(limit())) {
    tab.capturing = true;
    changes.acquire.push_back(id);
    return true;
  }
  if (!tab.queued) {
    tab.queued = true;
    queue_.push_back(id);
  }
  return false;
}

void LeaseScheduler::GrantQueue(Changes& changes) {
  while (!queue_.empty() && capturing_count() < static_cast<size_t>(limit())) {
    const std::string id = std::move(queue_.front());
    queue_.pop_front();
    auto it = tabs_.find(id);
    if (it == tabs_.end()) {
      continue;
    }
    Tab& tab = it->second;
    tab.queued = false;
    if (tab.in_flight > 0 || tab.holds > 0 || tab.live_always ||
        !tab.keep_alive_until.is_null()) {
      // A queued tab does not hold a capturer, so it is not being released.
      tab.capturing = true;
      changes.acquire.push_back(id);
    }
  }
}

LeaseScheduler::LowPowerRefusal LeaseScheduler::Refusal() const {
  return LowPowerRefusal{limit(),
                         static_cast<int>(kMaxKeepAliveLowPower.InSeconds())};
}

}  // namespace koan::agent
