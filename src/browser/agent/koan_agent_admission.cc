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
  connections_[id] = Connection();
  return id;
}

bool ConnectionAdmission::AssignPeer(Id id, const std::string& peer_key) {
  const auto it = connections_.find(id);
  if (it == connections_.end()) {
    return true;
  }
  // Counted without this connection, which has no peer yet.
  if (unauthenticated_for_peer(peer_key) >= kMaxUnauthenticatedPerPeer) {
    return false;
  }
  it->second.peer_key = peer_key;
  return true;
}

void ConnectionAdmission::MarkAuthenticated(Id id) {
  const auto it = connections_.find(id);
  if (it != connections_.end()) {
    it->second.authenticated = true;
  }
}

void ConnectionAdmission::Release(Id id) {
  connections_.erase(id);
}

size_t ConnectionAdmission::unauthenticated() const {
  return static_cast<size_t>(std::ranges::count_if(
      connections_,
      [](const auto& connection) { return !connection.second.authenticated; }));
}

size_t ConnectionAdmission::unauthenticated_for_peer(
    const std::string& peer_key) const {
  return static_cast<size_t>(
      std::ranges::count_if(connections_, [&](const auto& connection) {
        return !connection.second.authenticated &&
               connection.second.peer_key == peer_key;
      }));
}

}  // namespace koan::agent
