// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_FAILURE_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_FAILURE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What a tunnel's ssh printed: whether Koan's own SOCKS forward is listening,
// and why ssh ended (OpenSSH's messages and its exit status;
// .agent/notes/upstream/remote-spaces.md lists where each comes from). Pure.
namespace koan::remote {

enum class SshFailure {
  kNone,
  // No usable ssh (or /bin/sh) on this machine, or launching it failed.
  kSshNotFound,
  // ssh rejected one of the options Koan passes: older than OpenSSH 8.7.
  kSshTooOld,
  // ssh rejected the user's config or the destination.
  kBadConfig,
  // The host name didn't resolve.
  kHostNotFound,
  // The host didn't answer: refused, timed out, no route, network down.
  kHostUnreachable,
  // The server refused every key / password.
  kAuthFailed,
  // The host's key differs from known_hosts: possibly an attack. Never retried.
  kHostKeyChanged,
  // The host's key couldn't be verified: the user said no, or strict checking
  // has no key for it. Never retried.
  kHostKeyNotVerified,
  // ssh couldn't listen on the SOCKS port: it was taken between Koan picking
  // it and ssh binding it.
  kPortInUse,
  // An established connection died (keepalive timeout, closed by the server).
  kConnectionLost,
  // The user cancelled a prompt (password, passphrase, code).
  kCancelled,
  // Anything else.
  kUnknown,
};

// Whether the tunnel tries again by itself after `failure`.
bool IsRetryableSshFailure(SshFailure failure);

// The reason for an exit of ssh (through the supervisor) with `exit_code`
// (-1: killed by a signal), given its last stderr lines, and whether this run
// had reached "connected".
SshFailure ClassifySshExit(const std::vector<std::string>& stderr_lines,
                           int exit_code,
                           bool was_connected);

// The stderr line that explains the failure best, for the UI's details: the
// last one that names an error, else the last one. Untrusted text (it can
// quote the server), already sanitized by AddSshStderrLine().
std::string SshFailureDetail(const std::vector<std::string>& stderr_lines);

// Appends `line` to `lines`, keeping the last kMaxSshStderrLines, each cut to
// kMaxSshStderrLineLength bytes and with control characters replaced. ssh's
// debug and verbose chatter (IsSshChatterLine()) is dropped, so it can't push
// the errors out or become the failure's detail.
inline constexpr size_t kMaxSshStderrLines = 40;
inline constexpr size_t kMaxSshStderrLineLength = 512;
void AddSshStderrLine(std::vector<std::string>& lines, std::string_view line);

// Whether `line` is one of the debug1-3 lines (or the verbose-level notes)
// that ssh prints only because Koan runs it at LogLevel=DEBUG1. They can quote
// paths, commands from the user's config and the server's text, so Koan never
// keeps them.
bool IsSshChatterLine(std::string_view line);

// What a stderr line says about Koan's -D listener on 127.0.0.1:`port`.
// ssh sets up its forwards right after authenticating, one by one, and only
// then enters its session (ssh.c ssh_session2(), channels.c
// channel_setup_fwd_listener_tcpip(), clientloop.c client_loop()):
//   debug1: Local forwarding listening on 127.0.0.1 port <port>.   kBinding
//   bind [127.0.0.1]:<port>: Address already in use                kFailed
//   ... (the user's own forwards) ...
//   debug1: Entering interactive session.                  kSessionStarted
// kBinding is printed just before bind(), so only kSessionStarted after it,
// with no kFailed between, means the port is ssh's.
enum class SshForwardLine {
  kOther,
  kBinding,
  kFailed,
  kSessionStarted,
};
SshForwardLine ReadSshForwardLine(std::string_view line, uint16_t port);

// A debug name for logs and tests (never ssh's text).
std::string_view SshFailureName(SshFailure failure);

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_FAILURE_H_
