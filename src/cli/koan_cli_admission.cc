// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_admission.h"

namespace koan::cli {

std::vector<agent::ProcessInfo> AgentsToServe(
    const std::optional<agent::PinnedIdentity>& pinned,
    const agent::PeerIdentity& self) {
  const agent::ProcessInfo started_by = agent::AgentAncestor(self);
  if (!pinned) {
    return {started_by};
  }
  // As the browser compares the holder with its pin (the holder's program is
  // its own business here: DecideLocalClient compares commands with it).
  if (agent::CompareIdentity(agent::PinnedIdentity{self.process, pinned->agent},
                             self) == agent::IdentityChange::kNone) {
    return {pinned->agent};
  }
  return {pinned->agent, started_by};
}

LocalClientVerdict DecideLocalClient(
    const agent::ProcessInfo& holder,
    const std::vector<agent::ProcessInfo>& agents,
    uint32_t uid,
    const std::optional<agent::PeerIdentity>& peer) {
  if (!peer || peer->process.path.empty() || holder.path.empty()) {
    return LocalClientVerdict::kUnresolved;
  }
  if (peer->uid != uid) {
    return LocalClientVerdict::kOtherUser;
  }
  // The browser's own rule (CompareIdentity): the same executable and
  // signature, and one of the served agents among the ancestors.
  LocalClientVerdict verdict = LocalClientVerdict::kAgentMissing;
  for (const agent::ProcessInfo& served : agents) {
    switch (
        agent::CompareIdentity(agent::PinnedIdentity{holder, served}, *peer)) {
      case agent::IdentityChange::kNone:
        return LocalClientVerdict::kAllowed;
      case agent::IdentityChange::kProcessChanged:
        verdict = LocalClientVerdict::kOtherProgram;
        break;
      case agent::IdentityChange::kChainChanged:
        break;
    }
  }
  return verdict;
}

std::string_view VerdictName(LocalClientVerdict verdict) {
  switch (verdict) {
    case LocalClientVerdict::kAllowed:
      return "allowed";
    case LocalClientVerdict::kUnresolved:
      return "unresolved";
    case LocalClientVerdict::kOtherUser:
      return "other-user";
    case LocalClientVerdict::kOtherProgram:
      return "other-program";
    case LocalClientVerdict::kAgentMissing:
      return "agent-missing";
  }
  return "unknown";
}

}  // namespace koan::cli
