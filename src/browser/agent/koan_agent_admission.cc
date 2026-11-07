// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_admission.h"

#include <algorithm>

namespace koan::agent {

ConnectionAdmission::ConnectionAdmission() = default;
ConnectionAdmission::~ConnectionAdmission() = default;

ConnectionAdmission::Id ConnectionAdmission::TryAdmit() {
  if (connections_.size() >= kMaxConnections ||
      unauthenticated() >= kMaxUnauthenticated) {
    return kRefused;
  }
  const Id id = next_id_++;
  connections_[id] = false;
  return id;
}

void ConnectionAdmission::MarkAuthenticated(Id id) {
  const auto it = connections_.find(id);
  if (it != connections_.end()) {
    it->second = true;
  }
}

void ConnectionAdmission::Release(Id id) {
  connections_.erase(id);
}

size_t ConnectionAdmission::unauthenticated() const {
  return static_cast<size_t>(std::ranges::count_if(
      connections_, [](const auto& connection) { return !connection.second; }));
}

}  // namespace koan::agent
