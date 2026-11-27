// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_peer.h"

#include <sys/socket.h>
#include <unistd.h>

#include "base/files/scoped_file.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_MAC)
#include <fcntl.h>
#include <poll.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/timer/elapsed_timer.h"
#include "net/base/sockaddr_storage.h"
#include "net/base/sockaddr_util_posix.h"
#elif BUILDFLAG(IS_LINUX)
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>

#include <array>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/posix/unix_domain_socket.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#endif

namespace koan::agent {

// `koan status` connects and closes at once: that is told apart from a peer
// that is there (or has sent something and gone), which the gateway must look
// up and warn about if it cannot.
TEST(KoanAgentPeerTest, APeerThatHungUpIsToldApart) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  base::ScopedFD ours(fds[0]);
  base::ScopedFD theirs(fds[1]);
  EXPECT_FALSE(PeerHasHungUp(ours.get()));
  theirs.reset();
  EXPECT_TRUE(PeerHasHungUp(ours.get()));

  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ours.reset(fds[0]);
  theirs.reset(fds[1]);
  ASSERT_EQ(write(theirs.get(), "x", 1), 1);
  theirs.reset();
  EXPECT_FALSE(PeerHasHungUp(ours.get()));
  // Peeking took nothing.
  char byte = 0;
  EXPECT_EQ(read(ours.get(), &byte, 1), 1);
  EXPECT_EQ(byte, 'x');
  EXPECT_FALSE(PeerHasHungUp(-1));
}

#if BUILDFLAG(IS_MAC)

namespace {

// A connected pair of unix sockets in a temporary directory, both ends in this
// process: the peer of `accepted` is this test.
struct ConnectedPair {
  base::ScopedFD client;
  base::ScopedFD accepted;
};

ConnectedPair Connect(const base::FilePath& path) {
  ConnectedPair pair;
  net::SockaddrStorage address;
  EXPECT_TRUE(net::FillUnixAddress(path.value(), false, &address));
  base::ScopedFD listener(socket(AF_UNIX, SOCK_STREAM, 0));
  EXPECT_EQ(bind(listener.get(), address.addr(), address.addr_len), 0);
  EXPECT_EQ(listen(listener.get(), 1), 0);
  pair.client.reset(socket(AF_UNIX, SOCK_STREAM, 0));
  EXPECT_EQ(connect(pair.client.get(), address.addr(), address.addr_len), 0);
  pair.accepted.reset(accept(listener.get(), nullptr, nullptr));
  EXPECT_TRUE(pair.accepted.is_valid());
  return pair;
}

}  // namespace

TEST(KoanAgentPeerTest, TheProcessThatConnectedIsResolvedFromItsAuditToken) {
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
  ConnectedPair pair = Connect(dir.GetPath().Append("s"));

  const std::optional<PeerToken> token =
      resolver->TokenForSocket(pair.accepted.get());
  ASSERT_TRUE(token);
  base::ElapsedTimer timer;
  const std::optional<ResolvedPeer> peer = resolver->Resolve(*token);
  const base::TimeDelta took = timer.Elapsed();
  ASSERT_TRUE(peer);
  // This test binary is tens of megabytes. The lookup validates the code
  // directory and the signature, not every resource, so it is far inside the
  // gateway's 5 s for the whole lookup (Gateway::kResolveDeadline); a change
  // back to full validation of a large bundle is what this guards.
  LOG(INFO) << "resolving the test binary took " << took.InMilliseconds()
            << " ms";
  EXPECT_LT(took, base::Seconds(2));

  EXPECT_EQ(peer->identity.uid, geteuid());
  base::FilePath exe;
  ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  EXPECT_EQ(
      base::MakeAbsoluteFilePath(base::FilePath(peer->identity.process.path)),
      base::MakeAbsoluteFilePath(exe));
  // A test binary may be unsigned or ad hoc signed: either is a described
  // state, never an unnamed one.
  const std::string& signing = peer->identity.process.signing_identity;
  EXPECT_TRUE(signing.empty() || signing == "apple" ||
              signing.starts_with("team:") || signing.starts_with("cdhash:"))
      << signing;
  // The chain has real paths in it (it may be short or empty when launched
  // straight from launchd, but it is never made of nameless entries).
  for (const ProcessInfo& ancestor : peer->identity.ancestors) {
    EXPECT_FALSE(ancestor.path.empty());
    EXPECT_FALSE(ancestor.signing_identity.empty() && ancestor.path.empty());
  }
  EXPECT_TRUE(resolver->Unchanged(*peer));
}

TEST(KoanAgentPeerTest, ARealChildIsResolvedAndIsGoneWhenItExits) {
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
  const base::FilePath path = dir.GetPath().Append("s");
  net::SockaddrStorage address;
  ASSERT_TRUE(net::FillUnixAddress(path.value(), false, &address));
  base::ScopedFD listener(socket(AF_UNIX, SOCK_STREAM, 0));
  ASSERT_EQ(bind(listener.get(), address.addr(), address.addr_len), 0);
  ASSERT_EQ(listen(listener.get(), 1), 0);

  // A program that is not this test connects: nc, which does not read its
  // standard input with -d and stays connected until the server closes.
  base::Process child = base::LaunchProcess(
      std::vector<std::string>{"/usr/bin/nc", "-d", "-U", path.value()},
      base::LaunchOptions());
  ASSERT_TRUE(child.IsValid());
  pollfd wait = {listener.get(), POLLIN, 0};
  ASSERT_EQ(poll(&wait, 1, 10000), 1) << "the child did not connect";
  base::ScopedFD accepted(accept(listener.get(), nullptr, nullptr));
  ASSERT_TRUE(accepted.is_valid());

  const std::optional<PeerToken> token =
      resolver->TokenForSocket(accepted.get());
  ASSERT_TRUE(token);
  base::ElapsedTimer timer;
  const std::optional<ResolvedPeer> peer = resolver->Resolve(*token);
  const base::TimeDelta took = timer.Elapsed();
  ASSERT_TRUE(peer);
  LOG(INFO) << "resolving a real child took " << took.InMilliseconds()
            << " ms; signing identity '"
            << peer->identity.process.signing_identity << "', "
            << peer->identity.ancestors.size() << " ancestors";
  // Well inside the gateway's 5 s for the whole lookup.
  EXPECT_LT(took, base::Seconds(2));

  EXPECT_EQ(peer->identity.uid, geteuid());
  EXPECT_EQ(
      base::MakeAbsoluteFilePath(base::FilePath(peer->identity.process.path)),
      base::MakeAbsoluteFilePath(base::FilePath("/usr/bin/nc")));
  // Its parent is this test.
  ASSERT_FALSE(peer->identity.ancestors.empty());
  base::FilePath exe;
  ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  EXPECT_EQ(base::MakeAbsoluteFilePath(
                base::FilePath(peer->identity.ancestors[0].path)),
            base::MakeAbsoluteFilePath(exe));
  EXPECT_TRUE(resolver->Unchanged(*peer));

  // Once it is gone it is not the peer any more, and cannot be looked up.
  ASSERT_TRUE(child.Terminate(0, /*wait=*/true));
  EXPECT_FALSE(resolver->Unchanged(*peer));
  EXPECT_FALSE(resolver->Resolve(*token));
}

TEST(KoanAgentPeerTest, AHandleThatIsNotThePeersIsNotUnchanged) {
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
  ConnectedPair pair = Connect(dir.GetPath().Append("s"));
  std::optional<ResolvedPeer> peer =
      resolver->Resolve(*resolver->TokenForSocket(pair.accepted.get()));
  ASSERT_TRUE(peer);

  ResolvedPeer tampered = *peer;
  tampered.handle[sizeof(audit_token_t)] ^= 1;  // started at another time
  EXPECT_FALSE(resolver->Unchanged(tampered));
  tampered = *peer;
  tampered.handle.back() ^= 1;  // started at another time (microseconds)
  EXPECT_FALSE(resolver->Unchanged(tampered));
  tampered = *peer;
  tampered.identity.process.path = "/bin/ls";  // another path
  EXPECT_FALSE(resolver->Unchanged(tampered));
  tampered = *peer;
  tampered.handle.clear();
  EXPECT_FALSE(resolver->Unchanged(tampered));
  tampered = *peer;
  tampered.handle.pop_back();
  EXPECT_FALSE(resolver->Unchanged(tampered));
}

// The gateway's quiet path for `koan status` relies on this (macOS): once the
// connecting end is closed, the accepted socket has no audit token.
TEST(KoanAgentPeerTest, NoAuditTokenOnceThePeerHasGone) {
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
  ConnectedPair pair = Connect(dir.GetPath().Append("s"));
  pair.client.reset();
  EXPECT_FALSE(resolver->TokenForSocket(pair.accepted.get()));
  EXPECT_TRUE(PeerHasHungUp(pair.accepted.get()));
}

TEST(KoanAgentPeerTest, WhatIsNotAPeerFailsClosed) {
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  // A descriptor that is not a socket.
  base::ScopedFD file(open("/dev/null", O_RDONLY));
  ASSERT_TRUE(file.is_valid());
  EXPECT_FALSE(resolver->TokenForSocket(file.get()));
  EXPECT_FALSE(resolver->TokenForSocket(-1));
  // Tokens that name nobody.
  EXPECT_FALSE(resolver->Resolve({}));
  EXPECT_FALSE(resolver->Resolve(PeerToken(3, 1)));
  EXPECT_FALSE(resolver->Resolve(PeerToken(sizeof(audit_token_t), 0)));
  EXPECT_FALSE(resolver->Resolve(PeerToken(sizeof(audit_token_t) + 1, 0)));
}

#elif BUILDFLAG(IS_LINUX)

namespace {

// SO_PEERPIDFD (Linux 6.5). Without it the resolver refuses every peer, which
// is the right answer there and nothing to test.
bool KernelHasPeerPidfd() {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return false;
  }
  base::ScopedFD a(fds[0]);
  base::ScopedFD b(fds[1]);
  int pidfd = -1;
  socklen_t length = sizeof(pidfd);
  if (getsockopt(a.get(), SOL_SOCKET, 77 /* SO_PEERPIDFD */, &pidfd, &length) !=
      0) {
    return false;
  }
  close(pidfd);
  return true;
}

base::FilePath ThisExecutable() {
  base::FilePath exe;
  EXPECT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  return base::MakeAbsoluteFilePath(exe);
}

void ExpectIsThisProcess(const ResolvedPeer& peer) {
  EXPECT_EQ(peer.identity.uid, geteuid());
  EXPECT_EQ(base::FilePath(peer.identity.process.path), ThisExecutable());
  // No signature on Linux: the file, by inode, ctime and size.
  struct stat info;
  ASSERT_EQ(stat(ThisExecutable().value().c_str(), &info), 0);
  EXPECT_EQ(peer.identity.process.signing_identity,
            FileIdentity(info.st_ino, info.st_ctim.tv_sec, info.st_ctim.tv_nsec,
                         info.st_size));
  for (const ProcessInfo& ancestor : peer.identity.ancestors) {
    EXPECT_FALSE(ancestor.signing_identity.empty());
  }
}

}  // namespace

TEST(KoanAgentPeerTest, PidfdInfoIsRead) {
  EXPECT_EQ(PidFromPidfdInfo("pos:\t0\nflags:\t02000002\nmnt_id:\t15\n"
                             "ino:\t1057\nPid:\t4242\nNSpid:\t4242\t7\n"),
            4242);
  // Not visible in that namespace, and exited.
  EXPECT_EQ(PidFromPidfdInfo("Pid:\t0\nNSpid:\t0\n"), 0);
  EXPECT_EQ(PidFromPidfdInfo("Pid:\t-1\n"), -1);
  EXPECT_FALSE(PidFromPidfdInfo("pos:\t0\nflags:\t02\n"));
  EXPECT_FALSE(PidFromPidfdInfo("Pid:\tsoon\n"));
  EXPECT_FALSE(PidFromPidfdInfo(""));
}

TEST(KoanAgentPeerTest, ProcStatIsReadPastTheCommandName) {
  // A name may hold spaces and ") ": the fields start after the last ')'.
  const std::optional<ProcStat> stat = ParseProcStat(
      "4242 (we ird) name) S 77 4242 4242 0 -1 4194560 100 0 0 0 1 2 0 0 20 "
      "0 1 0 987654 12345678 300 18446744073709551615\n");
  ASSERT_TRUE(stat);
  EXPECT_EQ(stat->parent, 77);
  EXPECT_EQ(stat->start_time, 987654u);
  EXPECT_FALSE(ParseProcStat("4242 (name) S 77 4242"));
  EXPECT_FALSE(ParseProcStat("no parenthesis here"));
  EXPECT_FALSE(ParseProcStat(""));
}

TEST(KoanAgentPeerTest, AFileIdentityNamesInodeCtimeAndSize) {
  EXPECT_EQ(FileIdentity(1234, 1700000000, 5, 99), "file:1234:1700000000.5:99");
  EXPECT_NE(FileIdentity(1234, 1700000000, 5, 99),
            FileIdentity(1234, 1700000000, 6, 99));
}

TEST(KoanAgentPeerTest, ThisProcessIsResolvedThroughItsOwnProc) {
  if (!KernelHasPeerPidfd()) {
    GTEST_SKIP() << "no SO_PEERPIDFD (Linux 6.5+)";
  }
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  base::ScopedFD ours(fds[0]);
  base::ScopedFD theirs(fds[1]);
  const std::optional<PeerToken> token = resolver->TokenForSocket(ours.get());
  ASSERT_TRUE(token);
  const std::optional<ResolvedPeer> peer = resolver->Resolve(*token);
  ASSERT_TRUE(peer);
  ExpectIsThisProcess(*peer);
  EXPECT_TRUE(resolver->Unchanged(*peer));
  // A token is resolved once.
  EXPECT_FALSE(resolver->Resolve(*token));
  // A copy of the peer keeps its descriptors.
  const ResolvedPeer copy = *peer;
  EXPECT_TRUE(resolver->Unchanged(copy));
  ResolvedPeer tampered = *peer;
  tampered.identity.process.path = "/bin/ls";
  EXPECT_FALSE(resolver->Unchanged(tampered));
  tampered = *peer;
  tampered.descriptors.reset();
  EXPECT_FALSE(resolver->Unchanged(tampered));
}

// The gateway's resolver: the client's hello carries its /proc, which is used
// once the kernel says it is a procfs in which this process is visible.
TEST(KoanAgentPeerTest, TheHelloIsTakenAndNothingElse) {
  if (!KernelHasPeerPidfd()) {
    GTEST_SKIP() << "no SO_PEERPIDFD (Linux 6.5+)";
  }
  const std::unique_ptr<PeerResolver> resolver =
      CreatePlatformPeerResolver(PeerHello::kExpected);
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  base::ScopedFD ours(fds[0]);
  base::ScopedFD theirs(fds[1]);
  ASSERT_TRUE(SendPeerHello(theirs.get()));
  ASSERT_EQ(write(theirs.get(), "\0\0", 2), 2);
  const std::optional<PeerToken> token = resolver->TokenForSocket(ours.get());
  ASSERT_TRUE(token);
  const std::optional<ResolvedPeer> peer = resolver->Resolve(*token);
  ASSERT_TRUE(peer);
  ExpectIsThisProcess(*peer);
  // The hello byte is gone; the first frame's bytes are not.
  char bytes[3] = {1, 1, 1};
  EXPECT_EQ(recv(ours.get(), bytes, sizeof(bytes), MSG_DONTWAIT), 2);
  EXPECT_EQ(bytes[0], 0);
  EXPECT_EQ(bytes[1], 0);
}

TEST(KoanAgentPeerTest, AClientWithoutAHelloKeepsItsFirstByte) {
  if (!KernelHasPeerPidfd()) {
    GTEST_SKIP() << "no SO_PEERPIDFD (Linux 6.5+)";
  }
  const std::unique_ptr<PeerResolver> resolver =
      CreatePlatformPeerResolver(PeerHello::kExpected);
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  base::ScopedFD ours(fds[0]);
  base::ScopedFD theirs(fds[1]);
  ASSERT_EQ(write(theirs.get(), "\0", 1), 1);
  const std::optional<ResolvedPeer> peer =
      resolver->Resolve(*resolver->TokenForSocket(ours.get()));
  // This process is visible in its own /proc: found without a hello.
  ASSERT_TRUE(peer);
  char byte = 1;
  EXPECT_EQ(recv(ours.get(), &byte, 1, MSG_DONTWAIT), 1);
  EXPECT_EQ(byte, 0);
}

TEST(KoanAgentPeerTest, AHelloThatIsNotAProcIsRefused) {
  if (!KernelHasPeerPidfd()) {
    GTEST_SKIP() << "no SO_PEERPIDFD (Linux 6.5+)";
  }
  const std::unique_ptr<PeerResolver> resolver =
      CreatePlatformPeerResolver(PeerHello::kExpected);
  const auto refused = [&](const std::vector<int>& sent) {
    int fds[2];
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    base::ScopedFD ours(fds[0]);
    base::ScopedFD theirs(fds[1]);
    const uint8_t byte = kPeerHelloByte;
    EXPECT_TRUE(base::UnixDomainSocket::SendMsg(
        theirs.get(), base::span_from_ref(byte), sent));
    return !resolver->Resolve(*resolver->TokenForSocket(ours.get()));
  };
  // A directory that is not a procfs root, a procfs entry that is not its
  // root, a file, no descriptor at all, and two of them.
  base::ScopedFD tmp(open("/tmp", O_PATH | O_DIRECTORY | O_CLOEXEC));
  base::ScopedFD self(open("/proc/self", O_PATH | O_DIRECTORY | O_CLOEXEC));
  base::ScopedFD null(open("/dev/null", O_RDONLY | O_CLOEXEC));
  base::ScopedFD proc(open("/proc", O_PATH | O_DIRECTORY | O_CLOEXEC));
  ASSERT_TRUE(tmp.is_valid() && self.is_valid() && null.is_valid() &&
              proc.is_valid());
  EXPECT_TRUE(refused({tmp.get()}));
  EXPECT_TRUE(refused({self.get()}));
  EXPECT_TRUE(refused({null.get()}));
  EXPECT_TRUE(refused({}));
  EXPECT_TRUE(refused({proc.get(), proc.get()}));
  // The real thing is accepted.
  EXPECT_FALSE(refused({proc.get()}));
}

TEST(KoanAgentPeerTest, ARealChildIsResolvedAndIsGoneWhenItExits) {
  if (!KernelHasPeerPidfd()) {
    GTEST_SKIP() << "no SO_PEERPIDFD (Linux 6.5+)";
  }
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  // A program that is not this test connects to a listening socket (the
  // kernel names the process that called connect(), not one that inherited
  // the socket).
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
  const base::FilePath path = dir.GetPath().Append("s");
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  ASSERT_LT(path.value().size(), sizeof(address.sun_path));
  base::span(address.sun_path).copy_prefix_from(base::span(path.value()));
  base::ScopedFD listener(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)),
            0);
  ASSERT_EQ(listen(listener.get(), 1), 0);
  // Python is on every Linux CI image and desktop; it connects and waits.
  base::LaunchOptions options;
  base::Process child = base::LaunchProcess(
      std::vector<std::string>{"/usr/bin/python3", "-c",
                               "import socket,sys,time\n"
                               "s=socket.socket(socket.AF_UNIX)\n"
                               "s.connect(sys.argv[1])\n"
                               "time.sleep(30)\n",
                               path.value()},
      options);
  ASSERT_TRUE(child.IsValid());
  pollfd wait = {listener.get(), POLLIN, 0};
  ASSERT_EQ(poll(&wait, 1, 10000), 1) << "the child did not connect";
  base::ScopedFD accepted(accept(listener.get(), nullptr, nullptr));
  ASSERT_TRUE(accepted.is_valid());

  const std::optional<PeerToken> token =
      resolver->TokenForSocket(accepted.get());
  ASSERT_TRUE(token);
  const std::optional<ResolvedPeer> peer = resolver->Resolve(*token);
  ASSERT_TRUE(peer);
  EXPECT_EQ(peer->identity.uid, geteuid());
  EXPECT_TRUE(base::FilePath(peer->identity.process.path)
                  .BaseName()
                  .value()
                  .starts_with("python3"));
  EXPECT_TRUE(peer->identity.process.signing_identity.starts_with("file:"));
  // Its parent is this test.
  ASSERT_FALSE(peer->identity.ancestors.empty());
  EXPECT_EQ(base::FilePath(peer->identity.ancestors[0].path), ThisExecutable());
  EXPECT_TRUE(resolver->Unchanged(*peer));

  // Once it is gone it is not the peer any more.
  ASSERT_TRUE(child.Terminate(0, /*wait=*/true));
  EXPECT_FALSE(resolver->Unchanged(*peer));
}

// The attack the review of #514 found: a same-user program in a user and
// mount namespace of its own (keeping the PID namespace, so that Koan stays
// visible) bind-mounts its own /proc/<pid> over Koan's and fills its
// descriptors with pidfds of another program, so that Koan's fdinfo, read
// through the /proc it sends, names that program. The control connection
// shows that a plain openat() is fooled; the resolver must refuse.
TEST(KoanAgentPeerTest, AProcWithMountsInItIsRefused) {
  if (!KernelHasPeerPidfd()) {
    GTEST_SKIP() << "no SO_PEERPIDFD (Linux 6.5+)";
  }
  if (!base::PathExists(base::FilePath("/usr/bin/python3"))) {
    GTEST_SKIP() << "no python3";
  }
  // The program the attacker wants to be taken for.
  base::Process victim = base::LaunchProcess(
      std::vector<std::string>{"/bin/sleep", "60"}, base::LaunchOptions());
  ASSERT_TRUE(victim.IsValid());
  base::ScopedFD victim_pidfd(
      static_cast<int>(syscall(__NR_pidfd_open, victim.Pid(), 0)));
  ASSERT_TRUE(victim_pidfd.is_valid());

  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
  const base::FilePath path = dir.GetPath().Append("s");
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  ASSERT_LT(path.value().size(), sizeof(address.sun_path));
  base::span(address.sun_path).copy_prefix_from(base::span(path.value()));
  base::ScopedFD listener(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)),
            0);
  ASSERT_EQ(listen(listener.get(), 4), 0);
  int out[2];
  ASSERT_EQ(pipe(out), 0);
  base::ScopedFD out_read(out[0]);
  base::ScopedFD out_write(out[1]);

  // The attacker: the victim's pidfd at every descriptor from 7 to 899, its
  // own /proc/<pid> mounted over this test's, then two connections, each
  // with its /proc (the first for the control, the second as the hello).
  constexpr char kAttacker[] = R"(
import ctypes, fcntl, os, socket, sys, time
def say(text):
    os.write(1, (text + "\n").encode())
libc = ctypes.CDLL(None, use_errno=True)
uid, gid, koan = os.getuid(), os.getgid(), os.getppid()
if libc.unshare(0x10000000 | 0x00020000) != 0:
    say("skip unshare"); sys.exit(0)
for name, text in (("setgroups", "deny"), ("uid_map", "%d %d 1" % (uid, uid)),
                   ("gid_map", "%d %d 1" % (gid, gid))):
    with open("/proc/self/" + name, "w") as f:
        f.write(text)
if libc.mount(None, b"/", None, 0x4000 | 0x40000, None) != 0:
    say("skip private"); sys.exit(0)
if libc.mount(("/proc/%d" % os.getpid()).encode(),
              ("/proc/%d" % koan).encode(), None, 0x1000, None) != 0:
    say("skip bind %d" % ctypes.get_errno()); sys.exit(0)
def high(fd):
    moved = fcntl.fcntl(fd, fcntl.F_DUPFD, 900)
    os.close(fd)
    return moved
proc = high(os.open("/proc", os.O_PATH | os.O_DIRECTORY))
for n in range(7, 900):
    if n != 3:
        os.dup2(3, n)
for _ in range(2):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sys.argv[1])
    s = socket.socket(fileno=high(s.detach()))
    socket.send_fds(s, [b"K"], [proc])
    s.detach()
say("sent")
time.sleep(30)
)";
  base::LaunchOptions options;
  options.fds_to_remap.emplace_back(victim_pidfd.get(), 3);
  options.fds_to_remap.emplace_back(out_write.get(), STDOUT_FILENO);
  base::Process attacker =
      base::LaunchProcess(std::vector<std::string>{"/usr/bin/python3", "-c",
                                                   kAttacker, path.value()},
                          options);
  ASSERT_TRUE(attacker.IsValid());
  out_write.reset();
  const auto stop = [&] {
    attacker.Terminate(0, /*wait=*/true);
    victim.Terminate(0, /*wait=*/true);
  };
  pollfd said = {out_read.get(), POLLIN, 0};
  ASSERT_EQ(poll(&said, 1, 10000), 1) << "the attacker said nothing";
  std::array<char, 256> line = {};
  const ssize_t length = read(out_read.get(), line.data(), line.size() - 1);
  const std::string what(line.data(), length > 0 ? length : 0);
  if (what.starts_with("skip")) {
    stop();
    GTEST_SKIP() << "no unprivileged user and mount namespace here: " << what;
  }
  ASSERT_TRUE(what.starts_with("sent")) << what;

  // The control: through the attacker's /proc a plain openat() reads the
  // attacker's descriptors as this process's.
  base::ScopedFD control(accept(listener.get(), nullptr, nullptr));
  ASSERT_TRUE(control.is_valid());
  uint8_t byte = 0;
  std::vector<base::ScopedFD> fds;
  ASSERT_EQ(base::UnixDomainSocket::RecvMsg(control.get(),
                                            base::span_from_ref(byte), &fds),
            1);
  ASSERT_EQ(fds.size(), 1u);
  base::ScopedFD attacker_pidfd(
      static_cast<int>(syscall(__NR_pidfd_open, attacker.Pid(), 0)));
  ASSERT_TRUE(attacker_pidfd.is_valid());
  ASSERT_GE(attacker_pidfd.get(), 7);
  ASSERT_LT(attacker_pidfd.get(), 900);
  base::ScopedFD info(openat(
      fds[0].get(),
      ("self/fdinfo/" + base::NumberToString(attacker_pidfd.get())).c_str(),
      O_RDONLY | O_CLOEXEC));
  std::string text;
  if (info.is_valid()) {
    std::array<char, 4096> buffer;
    const ssize_t got = read(info.get(), buffer.data(), buffer.size());
    if (got > 0) {
      text.assign(buffer.data(), static_cast<size_t>(got));
    }
  }
  EXPECT_EQ(PidFromPidfdInfo(text), victim.Pid())
      << "the attack did not take hold; the test proves nothing: " << text;

  // The resolver is not fooled.
  base::ScopedFD accepted(accept(listener.get(), nullptr, nullptr));
  ASSERT_TRUE(accepted.is_valid());
  const std::unique_ptr<PeerResolver> resolver =
      CreatePlatformPeerResolver(PeerHello::kExpected);
  const std::optional<PeerToken> token =
      resolver->TokenForSocket(accepted.get());
  ASSERT_TRUE(token);
  EXPECT_FALSE(resolver->Resolve(*token));
  stop();
}

TEST(KoanAgentPeerTest, WhatIsNotAPeerFailsClosed) {
  const std::unique_ptr<PeerResolver> resolver = CreatePlatformPeerResolver();
  ASSERT_TRUE(resolver);
  base::ScopedFD file(open("/dev/null", O_RDONLY));
  ASSERT_TRUE(file.is_valid());
  EXPECT_FALSE(resolver->TokenForSocket(file.get()));
  EXPECT_FALSE(resolver->TokenForSocket(-1));
  // Tokens this resolver never handed out.
  EXPECT_FALSE(resolver->Resolve({}));
  EXPECT_FALSE(resolver->Resolve(PeerToken(3, 1)));
  EXPECT_FALSE(resolver->Resolve(PeerToken(8, 0)));
  EXPECT_FALSE(resolver->Resolve(PeerToken(8, 0xff)));
}

#else

TEST(KoanAgentPeerTest, ThereIsNoResolverWhereThePeerCannotBeFound) {
  EXPECT_EQ(CreatePlatformPeerResolver(), nullptr);
}

#endif

}  // namespace koan::agent
