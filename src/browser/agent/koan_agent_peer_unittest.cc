// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_peer.h"

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_MAC)
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/timer/elapsed_timer.h"
#include "net/base/sockaddr_storage.h"
#include "net/base/sockaddr_util_posix.h"
#endif

namespace koan::agent {

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

#else

TEST(KoanAgentPeerTest, ThereIsNoResolverWhereThePeerCannotBeFound) {
  EXPECT_EQ(CreatePlatformPeerResolver(), nullptr);
}

#endif

}  // namespace koan::agent
