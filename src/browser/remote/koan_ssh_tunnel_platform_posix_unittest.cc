// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_tunnel_platform_posix.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>

#include "base/files/scoped_file.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "koan/browser/remote/koan_ssh_command.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

// A listening loopback socket; its port.
base::ScopedFD Listen(uint16_t* port) {
  base::ScopedFD fd(socket(AF_INET, SOCK_STREAM, 0));
  CHECK(fd.is_valid());
  sockaddr_in address = {};
#if BUILDFLAG(IS_APPLE)
  address.sin_len = sizeof(address);
#endif
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)),
           0);
  CHECK_EQ(listen(fd.get(), 1), 0);
  socklen_t length = sizeof(address);
  CHECK_EQ(
      getsockname(fd.get(), reinterpret_cast<sockaddr*>(&address), &length), 0);
  *port = ntohs(address.sin_port);
  return fd;
}

TEST(KoanSshTunnelPlatformPosixTest, PicksFreeLoopbackPorts) {
  const std::optional<uint16_t> port = PickFreeLoopbackPort(std::nullopt);
  ASSERT_TRUE(port);
  EXPECT_NE(*port, 0);
  // A free preferred port is kept; a taken one isn't.
  EXPECT_EQ(PickFreeLoopbackPort(port), port);
  uint16_t taken = 0;
  base::ScopedFD listener = Listen(&taken);
  const std::optional<uint16_t> other = PickFreeLoopbackPort(taken);
  ASSERT_TRUE(other);
  EXPECT_NE(*other, taken);
}

TEST(KoanSshTunnelPlatformPosixTest, ProbesForASocks5Server) {
  base::test::TaskEnvironment task_environment;
  uint16_t port = 0;
  base::ScopedFD listener = Listen(&port);
  // Something listens, but no SOCKS5 answer: the probe times out (1 s).
  {
    base::test::TestFuture<bool> probe;
    SshTunnelPlatformPosix().ProbeSocks(port, probe.GetCallback());
    EXPECT_FALSE(probe.Get());
  }
  listener.reset();
  // Nothing listens.
  {
    base::test::TestFuture<bool> probe;
    SshTunnelPlatformPosix().ProbeSocks(port, probe.GetCallback());
    EXPECT_FALSE(probe.Get());
  }
  // ssh's -D listener answers the greeting with "no authentication".
  listener = Listen(&port);
  base::test::TestFuture<bool> probe;
  SshTunnelPlatformPosix().ProbeSocks(port, probe.GetCallback());
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::ScopedFD client(
        HANDLE_EINTR(accept(listener.get(), nullptr, nullptr)));
    ASSERT_TRUE(client.is_valid());
    std::array<uint8_t, 3> greeting = {};
    ASSERT_EQ(HANDLE_EINTR(recv(client.get(), greeting.data(), greeting.size(),
                                MSG_WAITALL)),
              3);
    EXPECT_EQ(greeting, (std::array<uint8_t, 3>{0x05, 0x01, 0x00}));
    constexpr std::array<uint8_t, 2> kChoice = {0x05, 0x00};
    ASSERT_EQ(
        HANDLE_EINTR(send(client.get(), kChoice.data(), kChoice.size(), 0)), 2);
  }
  EXPECT_TRUE(probe.Get());
}

// The supervisor (koan_ssh_command.h), with a stand-in for ssh.
class Supervised {
 public:
  explicit Supervised(std::vector<std::string> command) {
    std::array<int, 2> fds;
    CHECK_EQ(pipe(fds.data()), 0);
    base::ScopedFD read_end(fds[0]);
    stdin_write_.reset(fds[1]);
    base::LaunchOptions options;
    options.fds_to_remap = {{read_end.get(), STDIN_FILENO}};
    options.new_process_group = true;
    process_ = base::LaunchProcess(
        BuildSupervisedArgv(base::FilePath("/bin/sh"), command), options);
    CHECK(process_.IsValid());
  }

  void ClosePipe() { stdin_write_.reset(); }

  // The exit code, or nullopt if it's still running after the timeout.
  std::optional<int> Wait(base::TimeDelta timeout) {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
    int exit_code = 0;
    if (!process_.WaitForExitWithTimeout(timeout, &exit_code)) {
      return std::nullopt;
    }
    return exit_code;
  }

  // Whether any process is left in the supervisor's group.
  bool GroupAlive() const { return kill(-process_.Pid(), 0) == 0; }

  void KillGroup() { kill(-process_.Pid(), SIGKILL); }

 private:
  base::ScopedFD stdin_write_;
  base::Process process_;
};

TEST(KoanSshTunnelPlatformPosixTest, TheSupervisorPassesSshsExitCode) {
  Supervised supervised({"/bin/sh", "-c", "exit 255"});
  EXPECT_EQ(supervised.Wait(TestTimeouts::action_timeout()), 255);
}

TEST(KoanSshTunnelPlatformPosixTest, ClosingThePipeKillsTheGroup) {
  // "ssh" and a helper it started, both of which would run for a long time.
  Supervised supervised({"/bin/sh", "-c", "sleep 600 & sleep 600"});
  EXPECT_FALSE(supervised.Wait(base::Milliseconds(200)));
  // Koan stops the tunnel, quits or crashes.
  supervised.ClosePipe();
  const std::optional<int> exit_code =
      supervised.Wait(TestTimeouts::action_timeout());
  EXPECT_EQ(exit_code, -1);  // SIGTERM.
  // Reaped, but its group's members may still be exiting: give them a moment.
  for (int i = 0; i < 50 && supervised.GroupAlive(); ++i) {
    base::PlatformThread::Sleep(base::Milliseconds(20));
  }
  EXPECT_FALSE(supervised.GroupAlive());
  if (supervised.GroupAlive()) {
    supervised.KillGroup();
  }
}

TEST(KoanSshTunnelPlatformPosixTest, SshExitingEndsWhatItLeftBehind) {
  // ssh exits, leaving a child (a stray helper) in the group.
  Supervised supervised({"/bin/sh", "-c", "sleep 600 & exit 7"});
  EXPECT_EQ(supervised.Wait(TestTimeouts::action_timeout()), 7);
  for (int i = 0; i < 50 && supervised.GroupAlive(); ++i) {
    base::PlatformThread::Sleep(base::Milliseconds(20));
  }
  EXPECT_FALSE(supervised.GroupAlive());
  if (supervised.GroupAlive()) {
    supervised.KillGroup();
  }
}

TEST(KoanSshTunnelPlatformPosixTest, TrustsOnlyRootOwnedSystemBinaries) {
  // /bin/sh always qualifies; on macOS so does /usr/bin/ssh.
  EXPECT_EQ(ResolveSystemShell(), base::FilePath("/bin/sh"));
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(ResolveSystemSsh(), base::FilePath("/usr/bin/ssh"));
#endif
}

}  // namespace
}  // namespace koan::remote
