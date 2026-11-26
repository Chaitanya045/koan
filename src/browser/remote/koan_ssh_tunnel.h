// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "koan/browser/remote/koan_ssh_askpass.h"
#include "koan/browser/remote/koan_ssh_command.h"
#include "koan/browser/remote/koan_ssh_failure.h"

namespace koan::remote {

// What the tunnel needs from the system, so tests can fake it
// (koan_ssh_tunnel_platform_posix.h is the real one). Called and answered on
// the tunnel's sequence.
class SshTunnelPlatform {
 public:
  // A running ssh. Destroying it stops ssh (and everything it started) and
  // runs no more of its callbacks.
  class Process {
   public:
    virtual ~Process() = default;
  };

  struct ProcessCallbacks {
    ProcessCallbacks();
    ProcessCallbacks(ProcessCallbacks&&);
    ProcessCallbacks& operator=(ProcessCallbacks&&);
    ~ProcessCallbacks();

    // A line ssh printed to stderr (unsanitized, without the newline).
    base::RepeatingCallback<void(std::string line)> on_stderr_line;
    // A prompt from ssh's askpass helper. `reply` is null for kNotice.
    base::RepeatingCallback<void(SshPrompt prompt,
                                 SshPromptReplyCallback reply)>
        on_prompt;
    // ssh is gone (after its last stderr line), with the supervisor's exit
    // code: ssh's own, -1 if killed by a signal, or kLaunchFailedExitCode.
    base::OnceCallback<void(int exit_code)> on_exit;
  };

  static constexpr int kLaunchFailedExitCode = 127;

  virtual ~SshTunnelPlatform() = default;

  // Starts ssh for a SOCKS tunnel to `destination` on 127.0.0.1:`port`.
  virtual std::unique_ptr<Process> Launch(const SshDestination& destination,
                                          uint16_t port,
                                          ProcessCallbacks callbacks) = 0;

  // A free loopback port: `preferred` if it's free, else any. Nullopt if none.
  virtual void PickPort(
      std::optional<uint16_t> preferred,
      base::OnceCallback<void(std::optional<uint16_t>)> callback) = 0;

  // Whether a SOCKS5 server answers on 127.0.0.1:`port`. Asked only once ssh
  // has said its -D listener is up (ReadSshForwardLine()): before that,
  // whoever answers there isn't ssh.
  virtual void ProbeSocks(uint16_t port,
                          base::OnceCallback<void(bool)> callback) = 0;
};

enum class SshTunnelState {
  // Not started, or Stop()ped.
  kStopped,
  // ssh is starting, connecting or authenticating.
  kConnecting,
  // ssh waits for the user (status().prompt).
  kNeedsInput,
  // ssh's SOCKS proxy is up on 127.0.0.1:status().port: ssh said it listens
  // there, and it answers.
  kConnected,
  // ssh ended (status().failure); the next attempt is at
  // status().next_attempt.
  kRetrying,
  // ssh ended for a reason retrying can't fix (status().failure). Reconnect()
  // tries again.
  kFailed,
};

struct SshTunnelStatus {
  SshTunnelStatus();
  SshTunnelStatus(const SshTunnelStatus&);
  SshTunnelStatus& operator=(const SshTunnelStatus&);
  ~SshTunnelStatus();

  SshTunnelState state = SshTunnelState::kStopped;
  // kConnected: the SOCKS5 port on 127.0.0.1.
  uint16_t port = 0;
  // kRetrying, kFailed: why ssh ended; `detail` is ssh's line about it
  // (untrusted text, sanitized).
  SshFailure failure = SshFailure::kNone;
  std::string detail;
  // kRetrying: when the next attempt starts.
  base::TimeTicks next_attempt;
  // kNeedsInput: the open prompt (also given to the SshPromptDelegate).
  std::optional<SshPrompt> prompt;
  // Failed attempts since the last connection (or Start()/Reconnect()).
  int failed_attempts = 0;
};

// One Space's SSH tunnel: runs ssh as a SOCKS proxy, reconnects with backoff
// when it drops, and reports its state. PR 1 wires kConnected to the Space's
// runtime proxy (and every other state to "no connection", so the Space never
// falls back to a direct connection); PR 3 shows the states and prompts.
class SshTunnel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSshTunnelStatusChanged(const SshTunnelStatus& status) = 0;
  };

  // The backoff between failed attempts: 1 s, doubling, at most 60 s. A port
  // clash is retried at once, a few times.
  static constexpr base::TimeDelta kFirstRetryDelay = base::Seconds(1);
  static constexpr base::TimeDelta kMaxRetryDelay = base::Seconds(60);
  static constexpr int kMaxImmediatePortRetries = 3;
  // How often the SOCKS port is probed once ssh listens on it.
  static constexpr base::TimeDelta kProbeInterval = base::Milliseconds(250);
  // A connection that lasted this long resets the backoff when it drops.
  static constexpr base::TimeDelta kStableConnection = base::Seconds(30);

  // `destination` must be valid (ParseSshDestination()). `platform` and
  // `prompts` must outlive the tunnel.
  SshTunnel(SshDestination destination,
            SshTunnelPlatform* platform,
            SshPromptDelegate* prompts);
  SshTunnel(const SshTunnel&) = delete;
  SshTunnel& operator=(const SshTunnel&) = delete;
  // Stops ssh.
  ~SshTunnel();

  const SshDestination& destination() const { return destination_; }
  const SshTunnelStatus& status() const { return status_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Connects (from kStopped or kFailed; otherwise nothing).
  void Start();
  // Stops ssh, cancels any prompt and retry: kStopped.
  void Stop();
  // Starts a fresh attempt now, from any state but kStopped: the user's
  // "Reconnect", or a retry the user doesn't want to wait for.
  void Reconnect();

  // System events (KoanSshTunnelSystemEvents forwards them). While suspended
  // no attempt starts. On resume and on a network change, a waiting retry
  // starts at once, with the backoff reset; a connected tunnel restarts too
  // (its TCP connection is probably gone, and ssh would need up to
  // ServerAliveInterval * ServerAliveCountMax to notice) unless connecting
  // needed the user's input, which a restart would ask for again.
  void OnSuspend();
  void OnResume();
  void OnNetworkChanged();

 private:
  // How far ssh's output has got with Koan's -D listener on port_.
  enum class ForwardState {
    kWaiting,
    // "Local forwarding listening on 127.0.0.1 port <port_>." (before bind).
    kBinding,
    // ssh entered its session after that with no bind error: the listener on
    // port_ is ssh's, and only now is it probed.
    kListening,
  };

  void SetState(SshTunnelState state);
  void Notify();
  void BeginAttempt();
  void OnPortPicked(int attempt, std::optional<uint16_t> port);
  void OnStderrLine(int attempt, std::string line);
  void OnPrompt(int attempt, SshPrompt prompt, SshPromptReplyCallback reply);
  void OnPromptReply(int attempt,
                     int prompt_id,
                     SshPromptKind kind,
                     SshPromptReplyCallback reply,
                     SshPromptReply answer);
  void OnExit(int attempt, int exit_code);
  void ScheduleProbe();
  void Probe();
  void OnProbeResult(int attempt, bool ready);
  void OnForwardLine(SshForwardLine line);
  void EndAttempt(SshFailure failure, std::string detail);
  void CancelPrompts();
  void RestartIfStale();
  void ResetBackoff();
  base::TimeDelta RetryDelay() const;

  const SshDestination destination_;
  const std::string destination_text_;
  const raw_ptr<SshTunnelPlatform> platform_;
  const raw_ptr<SshPromptDelegate> prompts_;

  SshTunnelStatus status_;
  base::ObserverList<Observer> observers_;

  // The current attempt; callbacks of older ones are ignored.
  int attempt_ = 0;
  std::unique_ptr<SshTunnelPlatform::Process> process_;
  uint16_t port_ = 0;
  std::optional<uint16_t> last_port_;
  std::vector<std::string> stderr_lines_;
  ForwardState forward_ = ForwardState::kWaiting;
  // Prompts shown to the delegate in this attempt.
  std::vector<int> shown_prompts_;
  int next_prompt_id_ = 1;
  // The user answered a prompt to connect (password, code, host key).
  bool needed_input_ = false;
  // A secret prompt was cancelled: the attempt ends as kCancelled.
  bool cancelled_ = false;
  base::TimeTicks connected_at_;
  // Failures the backoff counts: reset by a connection that lasted
  // kStableConnection, Start(), Reconnect(), a resume or network change.
  int backoff_failures_ = 0;
  int immediate_port_retries_ = 0;
  bool suspended_ = false;

  base::OneShotTimer retry_timer_;
  base::OneShotTimer probe_timer_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<SshTunnel> weak_factory_{this};
};

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_H_
