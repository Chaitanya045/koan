// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_GATEWAY_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_GATEWAY_H_

#include <stdint.h>
#include <sys/types.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/timer/timer.h"
#include "koan/browser/agent/koan_agent_admission.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/browser/agent/koan_agent_session.h"
#include "koan/browser/agent/koan_agent_socket.h"

namespace net {
class UnixDomainServerSocket;
}

// The unix socket the agent CLI connects to (.agent/notes/agent-control.md
// section 4.1): Koan's own accept loop, a Session for every connection, and the
// limits that keep a process that only connects from using up the browser.
//
// **It forwards nothing and nothing starts it.** Like Session, an authenticated
// connection answers Koan.status and refuses everything else; and no browser
// code creates a Gateway yet (no preference, no window hook), so this changes
// nothing a user can see. Starting it belongs to the settings change.
//
// What it does, in the order a connection meets it:
//   1. The listening socket is in a 0700 directory, 0600, behind a lock file
//      (whoever holds the lock owns the path), and refuses every uid but the
//      browser's before the connection is accepted.
//   2. ConnectionAdmission: at most 8 connections, 4 of them unauthenticated.
//      A connection over the limit is told Koan.busy and closed, unread.
//   3. The peer's audit token is read at once (PeerResolver::TokenForSocket)
//      and resolved to a process, a signing identity and ancestors off the
//      sequence. No byte is read from the connection until that has succeeded
//      (5 s), and it fails closed: no token or no process (told
//      Koan.peerUnresolved, and logged), another uid or a peer that is
//      blocked (told nothing) means the connection is closed. Once the peer is
//      known, one program may hold only two of the unauthenticated slots
//      (Koan.busy for the third).
//   4. A Session reads and writes the connection. The session asks the gateway
//      to look at the peer again (PeerResolver::Unchanged) before it verifies a
//      proof and before it applies the user's approval.
//   5. Once a second every connection is checked: the session's own timeouts,
//      the pre-authentication deadline (30 s, not counting a prompt) and, for a
//      connection that is closing, how long it may take to flush.
//
// Threads. A Gateway is created on any thread and then used, and destroyed, on
// one sequence (the IO thread in production; destroy it with DeleteSoon there).
// Everything it does on that sequence is non-blocking: the file-system work of
// starting (making the directory, taking the lock, removing a dead socket) runs
// on a thread-pool task that may block, a peer's signature is checked on
// another, and stopping is two cheap system calls (an lstat and an unlink of
// the one socket this gateway bound). The ClientStore is called on the sequence
// for every request and once a second per authenticated connection, so it must
// not block either (the production store keeps its records in memory and
// writes behind).
namespace koan::agent {

// Shows the user an ApprovalRequest (the native dialog; a fake in tests) and
// reports the answer through `answer`. Show() and Cancel() are called on the
// gateway's sequence; `answer` may be run from any thread (it posts itself back
// to the gateway's sequence, and does nothing if the connection or the gateway
// is gone), so a dialog on the UI thread can run it directly. Cancel() is
// called when the connection goes away with the prompt open, so the dialog can
// be dismissed; the presenter must post that to its own thread.
class ApprovalPresenter {
 public:
  virtual ~ApprovalPresenter();

  virtual void Show(const ApprovalRequest& request,
                    base::OnceCallback<void(ApprovalDecision)> answer) = 0;
  virtual void Cancel(uint64_t request_id) = 0;
};

struct GatewayOptions {
  base::FilePath socket_path;
  // The user the browser runs as: the only user whose connections are served.
  uid_t uid = 0;
  // Tests only: the uid the listening socket lets through, to see a connection
  // it refuses (the kernel's check, which runs before the gateway hears of the
  // connection). Unset: `uid`.
  std::optional<uid_t> kernel_uid_for_testing;
};

// What starting prepares off the sequence.
struct PreparedSocket;

class Gateway {
 public:
  enum class StartResult {
    kOk,
    kAlreadyStarted,
    // No way to find out who a peer is on this platform.
    kNoPeerResolver,
    kUnsafePath,
    // Another browser holds the lock on the socket path.
    kInUse,
    kBindFailed,
    // Stop() came first.
    kStopped,
  };

  // How long a connection may take to be resolved, and a closing connection to
  // flush what it still has to send.
  static constexpr base::TimeDelta kResolveDeadline = base::Seconds(5);
  static constexpr base::TimeDelta kFlushDeadline = base::Seconds(2);
  // The most a connection may have queued to send: tight before it has proved
  // who it is, generous after (a screenshot is a few MB).
  static constexpr size_t kMaxQueuedBytesPreAuth = 64 * 1024;
  static constexpr size_t kMaxQueuedBytesAuthenticated = 64 * 1024 * 1024;

  // The pointers must outlive the gateway. `resolver` is shared because its
  // Resolve() runs on thread-pool tasks that may outlast a Stop().
  Gateway(GatewayOptions options,
          std::shared_ptr<const PeerResolver> resolver,
          ClientStore* store,
          ApprovalPresenter* presenter,
          const base::TickClock* tick_clock,
          const base::Clock* clock);
  Gateway(const Gateway&) = delete;
  Gateway& operator=(const Gateway&) = delete;
  ~Gateway();

  // Starts listening and reports the result, once. Making the directory, taking
  // the lock and removing a dead socket happen off the sequence; the bind and
  // the first accept are back on it. `done` runs with kStopped if Stop() comes
  // first, and does not run if the Gateway is destroyed first.
  void Start(base::OnceCallback<void(StartResult)> done);
  // Closes the listener and every connection, releases the lock, and removes
  // the socket file if it is still the one this gateway bound (never another
  // browser's).
  void Stop();
  bool started() const { return state_ == State::kStarted; }

  // The store removed this client: its live connections close at once.
  void OnClientRevoked(std::string_view client_id);

  // Run by a once-a-second timer; public so that tests can drive the clock.
  void CheckTimeouts();

  size_t connection_count() const { return connections_.size(); }
  RateLimiter& limiter_for_testing() { return limiter_; }

 private:
  class Connection;

  enum class State { kStopped, kStarting, kStarted };

  void BeginPrepare();
  void OnPrepared(uint64_t generation, PreparedSocket prepared);
  void FinishStart(StartResult result);
  void DoAccept();
  void OnAcceptComplete(int result);
  // Returns whether to keep accepting.
  bool HandleAccepted(int result);
  void AddConnection(int fd);
  void ScheduleRemoval(ConnectionAdmission::Id id);
  void Remove(ConnectionAdmission::Id id);
  SessionEnvironment SessionEnv();
  base::TimeTicks Now() const { return tick_clock_->NowTicks(); }

  const GatewayOptions options_;
  const std::shared_ptr<const PeerResolver> resolver_;
  const raw_ptr<ClientStore> store_;
  const raw_ptr<ApprovalPresenter> presenter_;
  const raw_ptr<const base::TickClock> tick_clock_;
  const raw_ptr<const base::Clock> clock_;

  RateLimiter limiter_;
  std::set<std::string> accepted_chains_;
  ConnectionAdmission admission_;

  State state_ = State::kStopped;
  base::OnceCallback<void(StartResult)> start_done_;
  std::unique_ptr<net::UnixDomainServerSocket> server_;
  // Held for as long as the socket is: whoever holds it owns the path.
  base::ScopedFD lock_;
  // The file this gateway bound, so that only that file is removed.
  std::optional<SocketIdentity> socket_identity_;
  // Where AcceptSocketDescriptor() writes while an accept is pending.
  int accepted_fd_ = -1;
  base::RepeatingTimer timer_;
  std::map<ConnectionAdmission::Id, std::unique_ptr<Connection>> connections_;

  // Starting prepares the socket path on a worker, and takes the lock there.
  // Stop() can come before that reply: the reply is then stale, and it is the
  // reply's arrival, not Stop(), that releases the lock it carries. A Start()
  // right after must not prepare while one is outstanding, or it would flock a
  // second description of the same file and lose to the first (a settings
  // toggle off and on again would see kInUse for no reason), so it waits for
  // the stale one to come back and then prepares.
  uint64_t generation_ = 0;
  int outstanding_preparations_ = 0;
  bool start_waiting_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<Gateway> weak_factory_{this};
  // Not invalidated by Stop(): see above. Only destroying the Gateway cancels
  // it.
  base::WeakPtrFactory<Gateway> prepare_weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_GATEWAY_H_
