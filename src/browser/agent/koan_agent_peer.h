// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_PEER_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_PEER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <vector>

#include "koan/browser/agent/koan_agent_auth.h"

// Who is on the other end of an accepted connection, as the OS says it
// (.agent/notes/agent-control.md section 4.1, "Peer check on accept"), never as
// the client says it. Three steps, because they cost differently and run in
// different places:
//
//   TokenForSocket: a getsockopt on the socket, at accept, on the sequence that
//     owns it. On macOS this is LOCAL_PEERTOKEN, the *audit token* of the
//     process that connected: its pid *and* its pid version, which changes when
//     the process execs, so a pid that was reused or exec'd is a different
//     token. (LOCAL_PEERPID is a bare pid and racy.)
//   Resolve: from the token, the executable, its code-signing identity and its
//     ancestors. Slow (the Security framework reads files), so it runs off the
//     sequence, and it has to be callable from any thread. It takes the token
//     and not the descriptor on purpose: the descriptor may be closed, and its
//     number reused by another socket, while this runs.
//   Unchanged: cheap, on the sequence: is that process still the same
//     incarnation running the same executable? The socket survives an exec of
//     its peer and is shared with children that inherit it, so the identity
//     taken at accept is looked at again before a proof is verified and before
//     the user's approval is applied (Session::OnApprovalDecision).
//
// All of them fail closed: nullopt / false means "do not serve this peer".
// What the socket cannot tell: a child that inherited the descriptor is
// indistinguishable from the process that connected, because the token is the
// connector's. That is the connector's decision to share, and it is the
// connector that the user was shown and that the pin names.
namespace koan::agent {

using PeerToken = std::vector<uint8_t>;

struct ResolvedPeer {
  PeerIdentity identity;
  // Whatever Unchanged() needs to recognise the process again; opaque.
  std::vector<uint8_t> handle;
};

class PeerResolver {
 public:
  virtual ~PeerResolver();

  virtual std::optional<PeerToken> TokenForSocket(int fd) const = 0;
  virtual std::optional<ResolvedPeer> Resolve(const PeerToken& token) const = 0;
  virtual bool Unchanged(const ResolvedPeer& peer) const = 0;
};

// The resolver for this platform, or null where there is none (only macOS has
// one, and the gateway does not start without one).
std::unique_ptr<PeerResolver> CreatePlatformPeerResolver();

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_PEER_H_
