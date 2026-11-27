// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_command.h"

#include <algorithm>
#include <array>

#include "base/strings/cstring_view.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace koan::remote {

namespace {

constexpr size_t kMaxHostLength = 253;
constexpr size_t kMaxUserLength = 64;

// Any of these set means ssh has a locale (utf8.c msetlocale()).
constexpr auto kLocaleVariables =
    std::to_array<base::cstring_view>({"LC_ALL", "LC_CTYPE", "LANG"});

bool IsHostNameChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '.' || c == '-' || c == '_';
}

bool IsIPv6Literal(std::string_view host) {
  if (host.size() < 2 || host.size() > 45 ||
      host.find(':') == std::string_view::npos) {
    return false;
  }
  for (char c : host) {
    if (!base::IsHexDigit(c) && c != ':' && c != '.') {
      return false;
    }
  }
  return true;
}

std::optional<uint16_t> ParsePort(std::string_view text) {
  if (text.empty() || text.size() > 5) {
    return std::nullopt;
  }
  for (char c : text) {
    if (!base::IsAsciiDigit(c)) {
      return std::nullopt;
    }
  }
  unsigned port = 0;
  if (!base::StringToUint(text, &port) || port == 0 || port > 65535) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(port);
}

// The script BuildSupervisedArgv() runs; "$@" is ssh's argv. Kept POSIX sh
// (macOS runs it with bash or dash as /bin/sh). See koan_ssh_command.h.
constexpr char kSupervisorScript[] =
    // Koan's pipe moves to fd 3; ssh gets /dev/null (it reads no stdin
    // with -N anyway, which is why ssh can't watch Koan itself).
    "exec 3<&0 </dev/null\n"
    // The watcher: EOF on Koan's pipe kills the whole process group, with
    // SIGKILL after kSshKillGraceSeconds for whatever ignores SIGTERM (if Koan
    // crashed, nobody else would). Only then does it ignore SIGTERM itself, so
    // that ssh's own exit (below) still ends it.
    "(IFS= read -r _ <&3; trap '' TERM; kill -TERM 0; sleep 3; kill -KILL 0)"
    " 2>/dev/null 4>&- &\n"
    "exec 3<&-\n"
    // fd 4 is Koan's exit pipe: only this shell holds it, so it closes when
    // the shell exits, whatever the rest of the group does.
    "\"$@\" 4>&-\n"
    "r=$?\n"
    // ssh is gone: end the watcher and any helper left in the group.
    "trap '' TERM\n"
    "kill -TERM 0 2>/dev/null\n"
    "exit \"$r\"\n";

}  // namespace

bool IsValidSshHost(std::string_view host) {
  if (host.empty()) {
    return false;
  }
  if (IsIPv6Literal(host)) {
    return true;
  }
  if (host.size() > kMaxHostLength || host.front() == '-' ||
      host.front() == '.') {
    return false;
  }
  for (char c : host) {
    if (!IsHostNameChar(c)) {
      return false;
    }
  }
  return true;
}

bool IsValidSshUser(std::string_view user) {
  if (user.empty() || user.size() > kMaxUserLength || user.front() == '-') {
    return false;
  }
  for (char c : user) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '.' && c != '_' && c != '-' &&
        c != '+') {
      return false;
    }
  }
  return true;
}

std::optional<SshDestination> ParseSshDestination(std::string_view text) {
  text = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  SshDestination destination;
  if (const size_t at = text.rfind('@'); at != std::string_view::npos) {
    destination.user = std::string(text.substr(0, at));
    if (!IsValidSshUser(destination.user)) {
      return std::nullopt;
    }
    text = text.substr(at + 1);
  }
  std::string_view host = text;
  std::string_view port;
  if (text.starts_with('[')) {
    const size_t close = text.find(']');
    if (close == std::string_view::npos) {
      return std::nullopt;
    }
    host = text.substr(1, close - 1);
    std::string_view rest = text.substr(close + 1);
    if (!rest.empty()) {
      if (!rest.starts_with(':')) {
        return std::nullopt;
      }
      port = rest.substr(1);
      if (port.empty()) {
        return std::nullopt;
      }
    }
    if (!IsIPv6Literal(host)) {
      return std::nullopt;
    }
  } else if (const size_t colon = text.find(':');
             colon != std::string_view::npos) {
    // One colon is a port; more is a bare IPv6 literal, which can't carry
    // one ("[v6]:port" can).
    if (text.find(':', colon + 1) == std::string_view::npos) {
      host = text.substr(0, colon);
      port = text.substr(colon + 1);
      if (port.empty()) {
        return std::nullopt;
      }
    }
  }
  if (!IsValidSshHost(host)) {
    return std::nullopt;
  }
  destination.host = std::string(host);
  if (!port.empty()) {
    destination.port = ParsePort(port);
    if (!destination.port) {
      return std::nullopt;
    }
  }
  return destination;
}

std::string SshDestinationToString(const SshDestination& destination) {
  std::string text;
  if (!destination.user.empty()) {
    text = destination.user + "@";
  }
  const bool v6 = destination.host.find(':') != std::string::npos;
  if (v6) {
    text += "[" + destination.host + "]";
  } else {
    text += destination.host;
  }
  if (destination.port) {
    text += ":" + base::NumberToString(*destination.port);
  }
  return text;
}

std::vector<std::string> BuildSshTunnelArgv(const base::FilePath& ssh,
                                            const SshDestination& destination,
                                            uint16_t port) {
  CHECK(IsValidSshHost(destination.host));
  CHECK(destination.user.empty() || IsValidSshUser(destination.user));
  CHECK_NE(port, 0);
  std::vector<std::string> argv = {
      ssh.value(),
      // No remote command, stdin from /dev/null, no tty.
      "-N",
      "-n",
      "-T",
      // The SOCKS5 forward, on loopback only. OpenSSH can't pick the port
      // itself (port 0 is refused for -L and -D, readconf.c
      // parse_forward()), so Koan picks a free one and retries on a clash.
      "-D",
      base::StringPrintf("127.0.0.1:%u", port),
      // Command-line options take precedence over the config files, so these
      // hold whatever the user's config says.
      //
      // The user's own LocalForward, RemoteForward and DynamicForward for the
      // host apply too, and one of them often fails: the same forward is up
      // in a terminal. That must not end the tunnel. The tunnel watches for
      // its own -D instead (ReadSshForwardLine()).
      "-o",
      "ExitOnForwardFailure=no",
      // A config's "ClearAllForwardings yes" would clear -D too (readconf.c
      // fill_default_options() clears the command line's forwards as well),
      // and the tunnel would wait for a listener that never comes.
      "-o",
      "ClearAllForwardings=no",
      "-o",
      base::StringPrintf("ServerAliveInterval=%d",
                         kSshServerAliveIntervalSeconds),
      "-o",
      base::StringPrintf("ServerAliveCountMax=%d", kSshServerAliveCountMax),
      "-o",
      base::StringPrintf("ConnectTimeout=%d", kSshConnectTimeoutSeconds),
      // A ControlMaster would make ssh a mux client of (or a master that
      // outlives) this process; ForkAfterAuthentication (-f, OpenSSH 8.7+)
      // would leave the process group. Either breaks the tunnel's lifetime.
      "-o",
      "ControlMaster=no",
      "-o",
      "ControlPath=none",
      "-o",
      "ForkAfterAuthentication=no",
      "-o",
      "RemoteCommand=none",
      "-o",
      "RequestTTY=no",
      "-o",
      "PermitLocalCommand=no",
      // Koan reads ssh's errors to tell the user what went wrong (a config's
      // LogLevel QUIET would hide them), and debug1's "Local forwarding
      // listening on" and "Entering interactive session." to know that the
      // -D listener is ssh's. The tunnel drops the other debug lines
      // (IsSshChatterLine()); ssh never logs a password or an answer. Set
      // with -o, not -v, so a ProxyJump's ssh doesn't inherit it (ssh.c
      // passes only -v on).
      "-o",
      "LogLevel=DEBUG1",
  };
  if (destination.port) {
    argv.push_back("-p");
    argv.push_back(base::NumberToString(*destination.port));
  }
  if (!destination.user.empty()) {
    argv.push_back("-l");
    argv.push_back(destination.user);
  }
  // "--" ends the options (ssh.c sets opt_terminated), so nothing after it is
  // read as one, even if a later ssh learned to re-parse options after the
  // destination.
  argv.push_back("--");
  argv.push_back(destination.host);
  return argv;
}

std::vector<std::string> BuildSupervisedArgv(
    const base::FilePath& sh,
    const std::vector<std::string>& ssh_argv) {
  std::vector<std::string> argv = {sh.value(), "-c", kSupervisorScript,
                                   "koan-ssh-tunnel"};
  argv.insert(argv.end(), ssh_argv.begin(), ssh_argv.end());
  return argv;
}

std::string_view SshSupervisorScript() {
  return kSupervisorScript;
}

base::EnvironmentMap BuildSshEnvironment(const base::FilePath& askpass_helper,
                                         base::Environment& current) {
  base::EnvironmentMap environment;
  // A GUI app on macOS gets launchd's PATH, so a ProxyCommand such as
  // `cloudflared` or `tailscale nc` from Homebrew wouldn't be found. Its
  // directories go after the system ones, which keep precedence.
  std::string path = current.GetVar("PATH").value_or(std::string());
  if (path.empty()) {
    path = kSshDefaultPath;
  }
  const std::vector<std::string_view> dirs = base::SplitStringPiece(
      path, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (std::string_view extra : kSshExtraPathDirs) {
    if (!std::ranges::contains(dirs, extra)) {
      path = base::StrCat({path, ":", extra});
    }
  }
  environment["PATH"] = path;
  // Without a locale ssh shows non-ASCII text (a server's prompt, a path) as
  // \ooo escapes (utf8.c msetlocale()). "UTF-8" is macOS's UTF-8 LC_CTYPE.
  bool has_locale = false;
  for (base::cstring_view name : kLocaleVariables) {
    has_locale |= !current.GetVar(name).value_or(std::string()).empty();
  }
  if (!has_locale) {
    environment["LC_CTYPE"] = "UTF-8";
  }
  environment["SSH_ASKPASS"] = askpass_helper.value();
  environment["SSH_ASKPASS_REQUIRE"] = "force";
  // ssh sets this for each helper it starts; a stale value from Koan's own
  // environment must not reach the first one.
  environment["SSH_ASKPASS_PROMPT"] = std::string();
  return environment;
}

}  // namespace koan::remote
