// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_admission.h"

namespace koan::cli {

LocalClientVerdict DecideLocalClient(
    const agent::ProcessInfo& holder,
    const agent::ProcessInfo& paired_agent,
    uint32_t uid,
    const std::optional<agent::PeerIdentity>& peer) {
  if (!peer || peer->process.path.empty() || holder.path.empty()) {
    return LocalClientVerdict::kUnresolved;
  }
  if (peer->uid != uid) {
    return LocalClientVerdict::kOtherUser;
  }
  // The browser's own rule (CompareIdentity): the same executable and
  // signature, and the paired agent among the ancestors.
  switch (agent::CompareIdentity(agent::PinnedIdentity{holder, paired_agent},
                                 *peer)) {
    case agent::IdentityChange::kNone:
      return LocalClientVerdict::kAllowed;
    case agent::IdentityChange::kProcessChanged:
      return LocalClientVerdict::kOtherProgram;
    case agent::IdentityChange::kChainChanged:
      return LocalClientVerdict::kAgentMissing;
  }
  return LocalClientVerdict::kUnresolved;
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
