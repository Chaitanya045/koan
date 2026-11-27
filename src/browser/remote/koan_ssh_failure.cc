// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_failure.h"

#include <algorithm>
#include <initializer_list>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "koan/browser/remote/koan_ssh_text.h"

namespace koan::remote {

namespace {

bool Contains(std::string_view line, std::string_view part) {
  return line.find(part) != std::string_view::npos;
}

bool ContainsAny(std::string_view line,
                 std::initializer_list<std::string_view> parts) {
  return std::ranges::any_of(
      parts, [line](std::string_view part) { return Contains(line, part); });
}

// Whether ssh ended by itself, after printing its reason: the supervisor
// passes ssh's own status on (255 for an error), and 128 + N if a signal
// killed it; -1 is the supervisor itself killed.
bool SshExitedByItself(int exit_code) {
  return exit_code >= 0 && (exit_code <= 128 || exit_code == 255);
}

// The server's reason for a disconnect, from the line packet.c prints just
// before its fatal "Disconnected from <host> port <port>" (or alone).
std::string_view DisconnectReason(std::string_view last,
                                  std::string_view before) {
  if (last.starts_with("Received disconnect from ")) {
    return last;
  }
  if (last.starts_with("Disconnected from ") &&
      before.starts_with("Received disconnect from ")) {
    return before;
  }
  return std::string_view();
}

// The reason ssh's last line gives (ClassifySshExit() says why only that
// one). The messages are OpenSSH's (ssh.c, sshconnect.c, sshconnect2.c,
// readconf.c, clientloop.c, packet.c, kex.c; the design note lists them).
SshFailure ClassifyLastLine(const std::vector<std::string>& lines) {
  if (lines.empty()) {
    return SshFailure::kUnknown;
  }
  const std::string_view last = lines.back();
  const std::string_view before =
      lines.size() > 1 ? std::string_view(lines[lines.size() - 2])
                       : std::string_view();
  // verify_host_key_callback()'s fatal(). For a changed key, check_host_key()
  // prints "Host key for <host> has changed and you have requested strict
  // checking." just before it (sshconnect.c), after the warning box.
  if (last == "Host key verification failed.") {
    return Contains(before,
                    "has changed and you have requested strict checking")
               ? SshFailure::kHostKeyChanged
               : SshFailure::kHostKeyNotVerified;
  }
  // Koan's own options come from the "command-line" pseudo-file: an ssh that
  // doesn't know one is too old.
  if (Contains(last, "command-line") &&
      ContainsAny(last, {"Bad configuration option", "nsupported option"})) {
    return SshFailure::kSshTooOld;
  }
  if (ContainsAny(last, {"Bad configuration option", "nsupported option",
                         "bad configuration options",
                         "hostname contains invalid characters",
                         "remote username contains invalid characters",
                         "Can't open user config file"})) {
    return SshFailure::kBadConfig;
  }
  if (Contains(last, "Permission denied (") ||
      Contains(DisconnectReason(last, before),
               "Too many authentication failures")) {
    return SshFailure::kAuthFailed;
  }
  if (ContainsAny(last,
                  {"Could not resolve hostname", "Could not resolve host"})) {
    return SshFailure::kHostNotFound;
  }
  // No kPortInUse: a failed forward doesn't end ssh (ExitOnForwardFailure=no),
  // and the tunnel ends the attempt itself when Koan's own one fails
  // (ReadSshForwardLine()). A bind error here is the user's own forward's.
  if (ContainsAny(last, {"Timeout, server ", "closed by remote host",
                         "client_loop: send disconnect", "Disconnected from ",
                         "Received disconnect from "})) {
    return SshFailure::kConnectionLost;
  }
  if (ContainsAny(last, {"Connection closed by ", "Connection reset by ",
                         "Broken pipe", "connect to host ", "banner exchange",
                         "timed out", "Network is unreachable",
                         "No route to host", "Host is down"})) {
    return SshFailure::kHostUnreachable;
  }
  return SshFailure::kUnknown;
}

}  // namespace

bool IsRetryableSshFailure(SshFailure failure) {
  switch (failure) {
    case SshFailure::kHostNotFound:
    case SshFailure::kHostUnreachable:
    case SshFailure::kPortInUse:
    case SshFailure::kConnectionLost:
    case SshFailure::kUnknown:
      return true;
    case SshFailure::kNone:
    case SshFailure::kSshNotFound:
    case SshFailure::kSshTooOld:
    case SshFailure::kBadConfig:
    case SshFailure::kAuthFailed:
    case SshFailure::kHostKeyChanged:
    case SshFailure::kHostKeyNotVerified:
    case SshFailure::kIpv6Only:
    case SshFailure::kCancelled:
      return false;
  }
}

SshFailure ClassifySshExit(const std::vector<std::string>& stderr_lines,
                           int exit_code,
                           bool was_connected) {
  // 127/126: the supervisor couldn't run ssh.
  if (exit_code == 127 || exit_code == 126) {
    return SshFailure::kSshNotFound;
  }
  // The host key and the login were fine; whatever ssh (or the server's
  // text) printed, the connection dropped.
  if (was_connected) {
    return SshFailure::kConnectionLost;
  }
  if (!SshExitedByItself(exit_code)) {
    return SshFailure::kUnknown;
  }
  return ClassifyLastLine(stderr_lines);
}

std::string SshFailureDetail(const std::vector<std::string>& stderr_lines) {
  if (stderr_lines.empty()) {
    return std::string();
  }
  const std::string_view last = stderr_lines.back();
  const std::string_view reason = DisconnectReason(
      last, stderr_lines.size() > 1
                ? std::string_view(stderr_lines[stderr_lines.size() - 2])
                : std::string_view());
  return std::string(reason.empty() ? last : reason);
}

bool IsSshChatterLine(std::string_view line) {
  line = base::TrimWhitespaceASCII(line, base::TRIM_LEADING);
  // log.c do_log() prefixes debug lines with their level. The rest are
  // verbose() notes (sshconnect2.c, clientloop.c) that LogLevel=INFO hides.
  return line.starts_with("debug1: ") || line.starts_with("debug2: ") ||
         line.starts_with("debug3: ") ||
         line.starts_with("Authenticated to ") ||
         line.starts_with("Authenticated using ") ||
         line.starts_with("Transferred: sent ") ||
         line.starts_with("Bytes per second: ") ||
         // A ProxyJump's ssh at the user's LogLevel VERBOSE, after Koan's
         // ssh exited and hung it up; or Koan's own ssh killed in its session.
         line.starts_with("Killed by signal ");
}

SshForwardLine ReadSshForwardLine(std::string_view line, uint16_t port) {
  // ssh ends its lines with "\r\n".
  line = base::TrimWhitespaceASCII(line, base::TRIM_TRAILING);
  const std::string port_text = base::NumberToString(port);
  if (line == "debug1: Entering interactive session.") {
    return SshForwardLine::kSessionStarted;
  }
  if (line ==
      base::StrCat({"debug1: Local forwarding listening on 127.0.0.1 port ",
                    port_text, "."})) {
    return SshForwardLine::kBinding;
  }
  // channel_setup_fwd_listener_tcpip()'s error_f() when the address family
  // ("AddressFamily inet6") excludes 127.0.0.1. It names no port, but Koan's
  // -D comes first, and with any other family the lookup can't fail.
  if (line.starts_with(
          "channel_setup_fwd_listener_tcpip: getaddrinfo(127.0.0.1): ")) {
    return SshForwardLine::kUnsupported;
  }
  // error() and verbose() lines of channel_setup_fwd_listener_tcpip(), and
  // ssh_init_forwarding()'s when no local forward at all could be set up.
  if (line.starts_with(base::StrCat({"bind [127.0.0.1]:", port_text, ": "})) ||
      line.starts_with(
          base::StrCat({"listen [127.0.0.1]:", port_text, ": "})) ||
      line.starts_with(
          base::StrCat({"socket [127.0.0.1]:", port_text, ": "})) ||
      line.ends_with(base::StrCat({"cannot listen to port: ", port_text})) ||
      line == "Could not request local forwarding.") {
    return SshForwardLine::kFailed;
  }
  return SshForwardLine::kOther;
}

void AddSshStderrLine(std::vector<std::string>& lines, std::string_view line) {
  if (IsSshChatterLine(line)) {
    return;
  }
  std::string clean = std::string(base::TrimWhitespaceASCII(
      SanitizeSshText(line, kMaxSshStderrLineLength, /*keep_newlines=*/false),
      base::TRIM_ALL));
  if (clean.empty()) {
    return;
  }
  lines.push_back(std::move(clean));
  if (lines.size() > kMaxSshStderrLines) {
    lines.erase(lines.begin(),
                lines.begin() + (lines.size() - kMaxSshStderrLines));
  }
}

std::string_view SshFailureName(SshFailure failure) {
  switch (failure) {
    case SshFailure::kNone:
      return "none";
    case SshFailure::kSshNotFound:
      return "ssh-not-found";
    case SshFailure::kSshTooOld:
      return "ssh-too-old";
    case SshFailure::kBadConfig:
      return "bad-config";
    case SshFailure::kHostNotFound:
      return "host-not-found";
    case SshFailure::kHostUnreachable:
      return "host-unreachable";
    case SshFailure::kAuthFailed:
      return "auth-failed";
    case SshFailure::kHostKeyChanged:
      return "host-key-changed";
    case SshFailure::kHostKeyNotVerified:
      return "host-key-not-verified";
    case SshFailure::kPortInUse:
      return "port-in-use";
    case SshFailure::kIpv6Only:
      return "ipv6-only";
    case SshFailure::kConnectionLost:
      return "connection-lost";
    case SshFailure::kCancelled:
      return "cancelled";
    case SshFailure::kUnknown:
      return "unknown";
  }
}

}  // namespace koan::remote
