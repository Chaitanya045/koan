// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_registry.h"

#include <algorithm>
#include <array>
#include <utility>

namespace koan::agent {

namespace {

// Sorted, for binary search. Every entry is also allowed by the method policy;
// a unit test checks both.
constexpr auto kReadOnlyMethods = std::to_array<std::string_view>({
    "Accessibility.disable",
    "Accessibility.enable",
    "Accessibility.getChildAXNodes",
    "Accessibility.getFullAXTree",
    "Accessibility.getPartialAXTree",
    "Accessibility.getRootAXNode",
    "Accessibility.queryAXTree",
    "DOM.describeNode",
    "DOM.disable",
    "DOM.enable",
    "DOM.getAttributes",
    "DOM.getBoxModel",
    "DOM.getContentQuads",
    "DOM.getDocument",
    "DOM.getFrameOwner",
    "DOM.getNodeForLocation",
    "DOM.getOuterHTML",
    "DOM.pushNodesByBackendIdsToFrontend",
    "DOM.querySelector",
    "DOM.querySelectorAll",
    "DOM.requestNode",
    "IO.close",
    "IO.read",
    "Inspector.disable",
    "Inspector.enable",
    "Log.disable",
    "Log.enable",
    "Network.disable",
    "Network.enable",
    "Page.captureScreenshot",
    "Page.disable",
    "Page.enable",
    "Page.getFrameTree",
    "Page.getLayoutMetrics",
    "Page.getNavigationHistory",
    "Page.screencastFrameAck",
    "Page.setLifecycleEventsEnabled",
    "Page.startScreencast",
    "Page.stopScreencast",
    "Runtime.disable",
    "Runtime.enable",
    "Runtime.getProperties",
    "Runtime.releaseObject",
    "Runtime.releaseObjectGroup",
});

}  // namespace

TargetRegistry::TargetRegistry() = default;
TargetRegistry::~TargetRegistry() = default;

TargetRegistry::AddResult TargetRegistry::CanAddAgentTab(
    std::string_view client_id,
    std::string_view task) const {
  if (client_id.empty()) {
    return AddResult::kInvalid;
  }
  size_t total = 0;
  for (const auto& [id, entry] : entries_) {
    total += entry.agent_owned;
  }
  if (total >= kMaxTabsTotal) {
    return AddResult::kBrowserFull;
  }
  if (CountFor(client_id, std::nullopt) >= kMaxTabsPerClient) {
    return AddResult::kClientFull;
  }
  if (CountFor(client_id, task) >= kMaxTabsPerTask) {
    return AddResult::kTaskFull;
  }
  return AddResult::kOk;
}

TargetRegistry::AddResult TargetRegistry::AddAgentTab(std::string target_id,
                                                      std::string client_id,
                                                      std::string task) {
  if (target_id.empty()) {
    return AddResult::kInvalid;
  }
  if (entries_.contains(target_id)) {
    return AddResult::kExists;
  }
  if (AddResult can = CanAddAgentTab(client_id, task); can != AddResult::kOk) {
    return can;
  }
  entries_.emplace(std::move(target_id),
                   TargetEntry{std::move(client_id), std::move(task),
                               /*agent_owned=*/true, TabState::kDriven});
  return AddResult::kOk;
}

TargetRegistry::AddResult TargetRegistry::GrantUserTab(std::string target_id,
                                                       std::string client_id,
                                                       TabState state) {
  if (target_id.empty() || client_id.empty() || state == TabState::kPaused) {
    return AddResult::kInvalid;
  }
  auto it = entries_.find(target_id);
  if (it != entries_.end()) {
    TargetEntry& entry = it->second;
    if (entry.agent_owned || entry.client_id != client_id) {
      return AddResult::kExists;
    }
    // The same client asking for more (readable to driven): the caller had the
    // user approve it.
    entry.state = state;
    return AddResult::kOk;
  }
  entries_.emplace(std::move(target_id),
                   TargetEntry{std::move(client_id), std::string(),
                               /*agent_owned=*/false, state});
  return AddResult::kOk;
}

bool TargetRegistry::Pause(std::string_view target_id) {
  auto it = entries_.find(target_id);
  if (it == entries_.end()) {
    return false;
  }
  if (it->second.state == TabState::kDriven) {
    it->second.state = TabState::kPaused;
  }
  return true;
}

bool TargetRegistry::Resume(std::string_view target_id) {
  auto it = entries_.find(target_id);
  if (it == entries_.end() || it->second.state != TabState::kPaused) {
    return false;
  }
  it->second.state = TabState::kDriven;
  return true;
}

bool TargetRegistry::Remove(std::string_view target_id) {
  auto it = entries_.find(target_id);
  if (it == entries_.end()) {
    return false;
  }
  entries_.erase(it);
  return true;
}

std::vector<std::string> TargetRegistry::RemoveClient(
    std::string_view client_id) {
  std::vector<std::string> removed;
  std::erase_if(entries_, [&](const auto& item) {
    if (item.second.client_id != client_id) {
      return false;
    }
    removed.push_back(item.first);
    return true;
  });
  return removed;
}

std::vector<std::string> TargetRegistry::RemoveTask(std::string_view client_id,
                                                    std::string_view task) {
  std::vector<std::string> removed;
  std::erase_if(entries_, [&](const auto& item) {
    const TargetEntry& entry = item.second;
    if (!entry.agent_owned || entry.client_id != client_id ||
        entry.task != task) {
      return false;
    }
    removed.push_back(item.first);
    return true;
  });
  return removed;
}

const TargetEntry* TargetRegistry::Find(std::string_view target_id) const {
  auto it = entries_.find(target_id);
  return it == entries_.end() ? nullptr : &it->second;
}

std::vector<std::string> TargetRegistry::TargetsFor(
    std::string_view client_id) const {
  std::vector<std::string> targets;
  for (const auto& [id, entry] : entries_) {
    if (entry.client_id == client_id) {
      targets.push_back(id);
    }
  }
  return targets;
}

bool TargetRegistry::MaySend(std::string_view client_id,
                             std::string_view target_id,
                             std::string_view method) const {
  const TargetEntry* entry = Find(target_id);
  if (!entry || client_id.empty() || entry->client_id != client_id) {
    return false;
  }
  return entry->state == TabState::kDriven || IsReadOnlyMethod(method);
}

size_t TargetRegistry::CountFor(std::string_view client_id,
                                std::optional<std::string_view> task) const {
  size_t count = 0;
  for (const auto& [id, entry] : entries_) {
    if (entry.agent_owned && entry.client_id == client_id &&
        (!task || entry.task == *task)) {
      ++count;
    }
  }
  return count;
}

bool IsReadOnlyMethod(std::string_view method) {
  return std::ranges::binary_search(kReadOnlyMethods, method);
}

base::span<const std::string_view> ReadOnlyMethods() {
  return kReadOnlyMethods;
}

}  // namespace koan::agent
