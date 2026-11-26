// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_COMMAND_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_COMMAND_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"

// What Koan runs for a remote Space: the system ssh as a SOCKS tunnel, under a
// small /bin/sh supervisor that ends the tunnel when Koan goes away.
// .agent/notes/upstream/remote-spaces.md has the OpenSSH facts behind every
// option. Pure.
namespace koan::remote {

// Where to connect: a Host alias of the user's config or a host name or IP
// address (ssh applies the config to both), with an optional user and port.
struct SshDestination {
  std::string user;              // Empty: the config's User, or the local user.
  std::string host;              // An IPv6 literal is kept without brackets.
  std::optional<uint16_t> port;  // Empty: the config's Port, or 22.

  bool operator==(const SshDestination&) const = default;
};

// Whether `host` can be given to ssh as a destination host, and never be read
// as an option or a URI: [A-Za-z0-9._-] not starting with '-' or '.', at most
// 253 characters, or an IPv6 literal ([0-9A-Fa-f:.], at least one ':').
// Stricter than OpenSSH's own valid_hostname() (ssh.c).
bool IsValidSshHost(std::string_view host);
// The same for a user name: [A-Za-z0-9._+-] not starting with '-', at most 64
// characters. Stricter than valid_ruser() (ssh.c).
bool IsValidSshUser(std::string_view user);

// "host", "user@host", "host:port", "user@host:port", "[v6]" or
// "user@[v6]:port", the user's own text or a config alias. Surrounding
// whitespace is ignored; anything else invalid (an option, spaces, control
// characters, shell metacharacters, a bad port) gives nullopt.
std::optional<SshDestination> ParseSshDestination(std::string_view text);
// The text ParseSshDestination() reads back, for saving a Space's host.
std::string SshDestinationToString(const SshDestination& destination);

// The keepalive the tunnel asks for: a dead link is noticed within
// interval * count.
inline constexpr int kSshServerAliveIntervalSeconds = 10;
inline constexpr int kSshServerAliveCountMax = 3;
inline constexpr int kSshConnectTimeoutSeconds = 15;

// ssh's arguments (argv[0] is `ssh`) for a SOCKS5 tunnel on 127.0.0.1:`port`:
// no remote command, no tty, no connection sharing, no forking, keepalives,
// and an exit if the forward can't be set up. Everything else comes from the
// user's own config, keys and agent. `destination` must be valid; its host
// comes after "--", so it can never be an option.
std::vector<std::string> BuildSshTunnelArgv(const base::FilePath& ssh,
                                            const SshDestination& destination,
                                            uint16_t port);

// The supervisor's argv: `sh` -c <script> koan-ssh-tunnel <ssh_argv...>. It
// must be launched in a process group of its own, with stdin a pipe whose
// only writer is Koan. The script runs ssh in the foreground and a watcher in
// the background; when that pipe closes (Koan stopped the tunnel, quit or
// crashed), the watcher kills the whole group: ssh and any askpass helper.
// When ssh exits by itself, the script kills the group too and exits with
// ssh's status.
std::vector<std::string> BuildSupervisedArgv(
    const base::FilePath& sh,
    const std::vector<std::string>& ssh_argv);

// The supervisor script itself (for tests and the design note).
std::string_view SshSupervisorScript();

// The environment changes for ssh: Koan's own environment is inherited
// (SSH_AUTH_SOCK, HOME, PATH), plus the askpass helper, forced even without a
// display (SSH_ASKPASS_REQUIRE=force, OpenSSH 8.4+). An empty value unsets a
// variable (base::LaunchOptions::environment).
base::EnvironmentMap BuildSshEnvironment(const base::FilePath& askpass_helper);

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_COMMAND_H_
