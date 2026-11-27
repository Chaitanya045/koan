// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_PLATFORM_POSIX_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_PLATFORM_POSIX_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "koan/browser/remote/koan_ssh_tunnel.h"

namespace koan::remote {

// The system ssh and the shell for its supervisor: /usr/bin/ssh and /bin/sh,
// never looked up in PATH, and only if each is a regular executable file
// owned by root and writable by no one else (on macOS both are on the
// read-only, SIP-protected system volume). Empty if not. Blocking.
base::FilePath ResolveSystemSsh();
base::FilePath ResolveSystemShell();

// The real SshTunnelPlatform: launches the supervised ssh (koan_ssh_command.h)
// in a process group of its own, with a fresh 0700 askpass directory, off the
// calling sequence; probes and picks loopback ports on the thread pool.
//
// Lifetime: Koan holds the only write end of the supervisor's stdin pipe, on
// the tunnel's sequence. Destroying the Process closes it, which makes the
// supervisor kill its whole group; so does Koan's exit, even a crash, since
// the kernel closes the pipe. The group then also gets SIGTERM, and SIGKILL
// once the supervisor has exited or after a few seconds, before the
// supervisor is reaped. The supervisor's exit is seen on its exit pipe (or
// stderr), so a member that ignores SIGTERM and keeps stderr open can't keep
// a dead ssh "running".
class SshTunnelPlatformPosix : public SshTunnelPlatform {
 public:
  SshTunnelPlatformPosix();
  SshTunnelPlatformPosix(const SshTunnelPlatformPosix&) = delete;
  SshTunnelPlatformPosix& operator=(const SshTunnelPlatformPosix&) = delete;
  ~SshTunnelPlatformPosix() override;

  // SshTunnelPlatform:
  std::unique_ptr<Process> Launch(const SshDestination& destination,
                                  uint16_t port,
                                  ProcessCallbacks callbacks) override;
  void PickPort(
      std::optional<uint16_t> preferred,
      base::OnceCallback<void(std::optional<uint16_t>)> callback) override;
  void ProbeSocks(uint16_t port,
                  base::OnceCallback<void(bool)> callback) override;

  // Runs `argv` under the supervisor in place of ssh, for tests.
  void set_ssh_argv_for_testing(std::vector<std::string> argv) {
    ssh_argv_for_testing_ = std::move(argv);
  }

 private:
  std::vector<std::string> ssh_argv_for_testing_;
};

// The blocking halves, for tests.
std::optional<uint16_t> PickFreeLoopbackPort(std::optional<uint16_t> preferred);
bool ProbeSocks5(uint16_t port);

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_PLATFORM_POSIX_H_
