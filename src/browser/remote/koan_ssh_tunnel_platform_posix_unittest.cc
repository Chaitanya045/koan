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
#include <memory>
#include <optional>
#include <string>

#include "base/files/scoped_file.h"
#include "base/functional/callback_helpers.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/repeating_test_future.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/test/test_timeouts.h"
#include "base/threading/platform_thread.h"
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

  pid_t pid() const { return process_.Pid(); }

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

// Whether process group `group` is empty, waiting up to `timeout` for its
// members to go.
bool GroupGoneWithin(pid_t group, base::TimeDelta timeout) {
  const base::TimeTicks end = base::TimeTicks::Now() + timeout;
  while (kill(-group, 0) == 0) {
    if (base::TimeTicks::Now() >= end) {
      return false;
    }
    base::PlatformThread::Sleep(base::Milliseconds(20));
  }
  return true;
}

TEST(KoanSshTunnelPlatformPosixTest, ClosingThePipeKillsTheGroup) {
  // "ssh", a helper it started, and a ProxyCommand that ignores SIGTERM, all
  // of which would run for a long time.
  Supervised supervised({"/bin/sh", "-c",
                         "(trap '' TERM; exec sleep 600) & sleep 600 & "
                         "sleep 600"});
  EXPECT_FALSE(supervised.Wait(base::Milliseconds(200)));
  // Koan crashes (or stops the tunnel, or quits).
  supervised.ClosePipe();
  const std::optional<int> exit_code =
      supervised.Wait(TestTimeouts::action_timeout());
  EXPECT_EQ(exit_code, -1);  // SIGTERM.
  // Nobody is left to send SIGKILL but the watcher, after its grace.
  const pid_t group = supervised.pid();
  EXPECT_TRUE(GroupGoneWithin(group, base::Seconds(kSshKillGraceSeconds + 2)));
  if (kill(-group, 0) == 0) {
    kill(-group, SIGKILL);
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

// The real platform, with a stand-in for ssh that prints its group (the
// supervisor's pid) first.
class KoanSshTunnelPlatformPosixProcessTest : public testing::Test {
 protected:
  ~KoanSshTunnelPlatformPosixProcessTest() override {
    if (group_ > 0 && kill(-group_, 0) == 0) {
      kill(-group_, SIGKILL);
    }
  }

  // Launches `script` as ssh and waits for its first line, the group.
  void Launch(const std::string& script) {
    platform_.set_ssh_argv_for_testing(
        {"/bin/sh", "-c", "echo $PPID >&2; " + script});
    SshDestination destination;
    destination.host = "box";
    SshTunnelPlatform::ProcessCallbacks callbacks;
    callbacks.on_stderr_line = lines_.GetCallback();
    callbacks.on_prompt = base::DoNothing();
    callbacks.on_exit = exit_.GetCallback();
    process_ = platform_.Launch(destination, 41000, std::move(callbacks));
    ASSERT_TRUE(base::StringToInt(lines_.Take(), &group_));
    ASSERT_GT(group_, 1);
  }

  base::test::TaskEnvironment task_environment_;
  SshTunnelPlatformPosix platform_;
  base::test::RepeatingTestFuture<std::string> lines_;
  base::test::TestFuture<int> exit_;
  std::unique_ptr<SshTunnelPlatform::Process> process_;
  int group_ = 0;
};

// ssh exits, but a member of its group that ignores SIGTERM (a ProxyCommand
// that traps it) keeps stderr open: the exit is still reported, with ssh's
// last line and status, and that member is killed.
TEST_F(KoanSshTunnelPlatformPosixProcessTest, ReportsTheExitWhileStderrIsHeld) {
  Launch("(trap '' TERM; exec sleep 60) & echo bye >&2; exit 3");
  EXPECT_EQ(exit_.Get(), 3);
  EXPECT_EQ(lines_.Take(), "bye");
  EXPECT_TRUE(GroupGoneWithin(group_, base::Seconds(1)));
}

// Stopping the tunnel kills such a member too, as soon as the supervisor is
// gone (before it's reaped).
TEST_F(KoanSshTunnelPlatformPosixProcessTest, StoppingKillsWhatIgnoresTerm) {
  Launch("(trap '' TERM; exec sleep 60) & exec sleep 60");
  process_.reset();
  EXPECT_TRUE(GroupGoneWithin(group_, base::Seconds(kSshKillGraceSeconds - 1)));
  EXPECT_FALSE(exit_.IsReady());
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
