// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_ADMISSION_H_
#define KOAN_CLI_KOAN_CLI_ADMISSION_H_

#include <stdint.h>

#include <optional>
#include <string_view>
#include <vector>

#include "koan/browser/agent/koan_agent_auth.h"

// Who may use a holder's connection (.agent/notes/agent-control.md section
// 12). The browser approved one program: `koan`, run by the agent the pairing
// recorded (section 5.2), and checks it on every connection. The holder
// keeps that connection open for later commands, so it must ask the same of
// each command that connects to it, or any program of the user's could act
// as the approved agent. Pure: the holder resolves the peer from the OS
// (koan_agent_peer.h, the gateway's resolver) and this decides.
namespace koan::cli {

enum class LocalClientVerdict {
  kAllowed,
  // The OS could not say who it is (it exited, a failed lookup, or no
  // resolver on this platform): never served.
  kUnresolved,
  kOtherUser,
  // Not the holder's own `koan` executable (its path or signature differs).
  kOtherProgram,
  // `koan`, but not run by the agent the pairing recorded.
  kAgentMissing,
};

// The agents whose `koan` a holder serves, from the pairing's record
// (`pinned`, nullopt for a record without one) and the holder's own identity
// (`self`, read before it connected, as the browser reads it). The browser has
// just let the holder in, so it accepts the holder's chain: either that chain
// has the pinned agent, and only that agent is served, or the browser let it
// in anyway (the user approved a changed chain, or the record's pin is not the
// browser's, as with a koan that read it after the dialog), and the agent that
// started the holder is served too. An entry may be empty: a chain with no
// agent in it.
std::vector<agent::ProcessInfo> AgentsToServe(
    const std::optional<agent::PinnedIdentity>& pinned,
    const agent::PeerIdentity& self);

// `holder`: the holder's own executable as the OS describes it.
// `agents`: from AgentsToServe(). An empty agent stands for a chain that was
// unknown at pairing, which, as in the browser, refuses a chain that has an
// agent in it. `uid`: the holder's user.
LocalClientVerdict DecideLocalClient(
    const agent::ProcessInfo& holder,
    const std::vector<agent::ProcessInfo>& agents,
    uint32_t uid,
    const std::optional<agent::PeerIdentity>& peer);

std::string_view VerdictName(LocalClientVerdict verdict);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_ADMISSION_H_
