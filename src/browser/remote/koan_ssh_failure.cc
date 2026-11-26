// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_failure.h"

#include <algorithm>
#include <initializer_list>

#include "base/containers/adapters.h"
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

bool AnyLine(const std::vector<std::string>& lines,
             std::initializer_list<std::string_view> parts) {
  return std::ranges::any_of(lines, [parts](const std::string& line) {
    return ContainsAny(line, parts);
  });
}

// The reasons in order of precedence: a changed host key outranks everything
// printed after it, and so on down. The messages are OpenSSH's (ssh.c,
// sshconnect.c, sshconnect2.c, readconf.c, channels.c, clientloop.c,
// packet.c, kex.c; the design note lists them).
SshFailure ClassifyLines(const std::vector<std::string>& lines,
                         bool was_connected) {
  if (AnyLine(lines, {"REMOTE HOST IDENTIFICATION HAS CHANGED",
                      "POSSIBLE DNS SPOOFING DETECTED",
                      "has changed and you "
                      "have requested strict "
                      "checking"})) {
    return SshFailure::kHostKeyChanged;
  }
  if (AnyLine(lines, {"Host key verification failed.",
                      "and you have requested strict checking."})) {
    return SshFailure::kHostKeyNotVerified;
  }
  // Koan's own options come from the "command-line" pseudo-file: an ssh that
  // doesn't know one is too old.
  for (const std::string& line : lines) {
    if (Contains(line, "command-line") &&
        ContainsAny(line, {"Bad configuration option", "nsupported option"})) {
      return SshFailure::kSshTooOld;
    }
  }
  if (AnyLine(lines, {"Bad configuration option", "nsupported option",
                      "bad configuration options",
                      "hostname contains invalid characters",
                      "remote username contains invalid characters",
                      "Can't open user config file", "usage: ssh"})) {
    return SshFailure::kBadConfig;
  }
  if (AnyLine(lines, {"Permission denied (", "Too many authentication failures",
                      "No more authentication methods"})) {
    return SshFailure::kAuthFailed;
  }
  if (AnyLine(lines,
              {"Could not resolve hostname", "Could not resolve host"})) {
    return SshFailure::kHostNotFound;
  }
  if (AnyLine(lines, {"Could not request local forwarding."}) ||
      std::ranges::any_of(lines, [](const std::string& line) {
        return line.starts_with("bind [127.0.0.1]:") &&
               Contains(line, "Address already in use");
      })) {
    return SshFailure::kPortInUse;
  }
  if (AnyLine(lines, {"Timeout, server ", "closed by remote host",
                      "client_loop: send disconnect", "Disconnected from "})) {
    return SshFailure::kConnectionLost;
  }
  if (AnyLine(lines, {"Connection closed by ", "Connection reset by ",
                      "Broken pipe"})) {
    return was_connected ? SshFailure::kConnectionLost
                         : SshFailure::kHostUnreachable;
  }
  if (AnyLine(lines,
              {"connect to host ", "banner exchange", "timed out",
               "Network is unreachable", "No route to host", "Host is down"})) {
    return SshFailure::kHostUnreachable;
  }
  return was_connected ? SshFailure::kConnectionLost : SshFailure::kUnknown;
}

bool NamesError(std::string_view line) {
  return ContainsAny(
      line, {"rror", "denied", "failed", "Could not", "refused", "timed out",
             "Timeout", "closed", "reset", "unreachable", "No route", "invalid",
             "Bad ", "nsupported", "CHANGED", "Address already in use"});
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
  return ClassifyLines(stderr_lines, was_connected);
}

std::string SshFailureDetail(const std::vector<std::string>& stderr_lines) {
  for (const std::string& line : base::Reversed(stderr_lines)) {
    if (NamesError(line)) {
      return line;
    }
  }
  return stderr_lines.empty() ? std::string() : stderr_lines.back();
}

void AddSshStderrLine(std::vector<std::string>& lines, std::string_view line) {
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
    case SshFailure::kConnectionLost:
      return "connection-lost";
    case SshFailure::kCancelled:
      return "cancelled";
    case SshFailure::kUnknown:
      return "unknown";
  }
}

}  // namespace koan::remote
