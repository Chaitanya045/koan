// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SESSION_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SESSION_H_

#include <stdint.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_frame.h"

// One connection's authentication, as a state machine over frames
// (.agent/notes/agent-control.md sections 4.1 and 5): pairing a new client,
// proving a paired one, and nothing else. It reads and writes frames and asks
// the delegate for a decision from the user; it has no socket, no keychain and
// no UI, so all of it is testable.
//
// **It forwards nothing.** An authenticated session answers Koan.status and
// refuses every other method with Koan.unavailable. Connecting it to the tabs
// is a later change that needs the execution context tracker and the
// re-serialised params forward the design requires (PR 4 and after); until then
// nothing a client sends reaches Classify() or ApplyParamFilter().
//
// Messages are JSON in CDP's shape, {"id": N, "method": "Koan.x", "params":
// {}}, answered by {"id": N, "result": {}} or
// {"id": N, "error": {"code": -32000, "message": "...", "data": "Koan.code"}}.
//
//   Koan.pair {publicKey (base64, 32 bytes), label, capabilities: [names]}
//     asks the user to approve a new client; answers {clientId, capabilities}.
//     The client then authenticates; pairing does not.
//   Koan.authenticate {clientId}              -> {nonce (base64, 32 bytes)}
//   Koan.authenticate {clientId, signature}   -> {authenticated: true, ...}
//     signature is the Ed25519 signature of ProofMessage(clientId, nonce).
//   Koan.status (after authentication)        -> {clientId, capabilities}
namespace koan::agent {

enum class CloseReason {
  kProtocolViolation,
  kTooManyFailures,
  kBlocked,
  kPairingDenied,
  kTimeout,
};

enum class ApprovalKind {
  // A client that has not paired.
  kNewClient,
  // A paired client connecting from another executable, or with another
  // signature, than the one that paired.
  kPeerChanged,
  // The paired program is the same but the agent that started it is not among
  // its ancestors any more. Asked once per client and chain while Koan runs.
  kChainChanged,
};

struct ApprovalRequest {
  uint id = 0;
  ApprovalKind kind = ApprovalKind::kNewClient;
  // As the OS reports it: what the dialog's headline shows.
  PeerIdentity peer;
  // What the client calls itself ("calls itself ..."), only for a new client.
  std::string claimed_label;
  ClientCapabilities requested;
  // For a change: the client and what it was paired as.
  std::string client_id;
  PinnedIdentity previous;
};

struct ApprovalDecision {
  bool approved = false;
  // What the user allows; never more than was requested.
  ClientCapabilities granted;
};

class SessionDelegate {
 public:
  virtual ~SessionDelegate();

  virtual void Send(std::vector<uint8_t> frame) = 0;
  // The session is over; the connection must be closed.
  virtual void Close(CloseReason reason) = 0;
  // Show the user the request. The answer comes back through
  // Session::OnApprovalDecision() with the same id, or never (the session times
  // the request out).
  virtual void RequestApproval(const ApprovalRequest& request) = 0;
};

struct SessionEnvironment {
  SessionEnvironment();
  SessionEnvironment(const SessionEnvironment&);
  SessionEnvironment& operator=(const SessionEnvironment&);
  ~SessionEnvironment();

  raw_ptr<ClientStore> store;
  raw_ptr<RateLimiter> limiter;
  // "clientId|chain digest" of chains the user accepted since Koan started.
  raw_ptr<std::set<std::string>> accepted_chains;
  raw_ptr<const base::TickClock> tick_clock;
  raw_ptr<const base::Clock> clock;
  // Fills the span with random bytes: nonces and client ids. base::RandBytes in
  // production; tests pass a fixed sequence.
  base::RepeatingCallback<void(base::span<uint8_t>)> random_bytes;
};

class Session {
 public:
  enum class State {
    kUnauthenticated,
    kAwaitingApproval,
    kAwaitingProof,
    kAuthenticated,
    kClosed,
  };

  // How long a nonce is valid, a prompt may stay open, and a frame may stay
  // unfinished.
  static constexpr base::TimeDelta kNonceLifetime = base::Seconds(10);
  static constexpr base::TimeDelta kApprovalTimeout = base::Seconds(60);
  static constexpr base::TimeDelta kFrameTimeout = base::Seconds(10);

  Session(PeerIdentity peer,
          SessionEnvironment environment,
          SessionDelegate* delegate);
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session();

  // Bytes from the socket.
  void OnBytesReceived(base::span<const uint8_t> bytes);

  // The user's answer to an ApprovalRequest. Ignored if `request_id` is not the
  // pending one.
  void OnApprovalDecision(uint64_t request_id, ApprovalDecision decision);

  // Called by the gateway about once a second: expires a nonce, a prompt and an
  // unfinished frame.
  void CheckTimeouts();

  State state() const { return state_; }
  // Set once authenticated.
  const std::string& client_id() const { return client_id_; }
  const ClientCapabilities& capabilities() const { return capabilities_; }

 private:
  struct Pending;

  void Drain();
  void HandleFrame(base::span<const uint8_t> payload);
  void HandleUnauthenticated(int id,
                             const std::string& method,
                             const base::DictValue& params);
  void HandleAuthenticated(int id, const std::string& method);
  void HandlePair(int id, const base::DictValue& params);
  void HandleAuthenticate(int id, const base::DictValue& params);
  void BeginProof(int id, const ClientRecord& record);
  void FinishProof(int id, const base::DictValue& params);

  void Ask(ApprovalRequest request, Pending pending);
  void Fail(int id, const std::string& code, const std::string& message);
  void CountFailure();
  void Reply(int id, base::DictValue result);
  void ReplyError(int id, const std::string& code, const std::string& message);
  void Send(const base::DictValue& message);
  void Close(CloseReason reason);
  base::TimeTicks Now() const;
  std::string NewClientId();

  const PeerIdentity peer_;
  const std::string peer_key_;
  SessionEnvironment env_;
  raw_ptr<SessionDelegate> delegate_;
  FrameDecoder decoder_;
  State state_ = State::kUnauthenticated;
  uint64_t next_approval_id_ = 1;

  std::unique_ptr<Pending> pending_;
  // The nonce handed out, and for whom, until it is answered or expires.
  std::vector<uint8_t> nonce_;
  std::string nonce_client_id_;
  base::TimeTicks nonce_deadline_;

  base::TimeTicks partial_since_;

  std::string client_id_;
  ClientCapabilities capabilities_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SESSION_H_
