// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_socket.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "net/base/net_errors.h"
#include "net/socket/unix_domain_server_socket_posix.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

mode_t ModeOf(const base::FilePath& path) {
  struct stat info;
  EXPECT_EQ(lstat(path.value().c_str(), &info), 0) << path;
  return info.st_mode & 0777;
}

bool AllowAll(const net::UnixDomainServerSocket::Credentials&) {
  return true;
}

class KoanAgentSocketTest : public testing::Test {
 protected:
  // A short path: TMPDIR on macOS is already long.
  base::FilePath ShortRoot() {
    EXPECT_TRUE(
        short_dir_.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
    return short_dir_.GetPath();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  base::ScopedTempDir short_dir_;
};

}  // namespace

TEST_F(KoanAgentSocketTest, TheSocketLivesInTheProfileWhenThePathFits) {
  const base::FilePath profile("/tmp/koan-profile");
  EXPECT_EQ(*ChooseSocketPath(profile, 501),
            base::FilePath("/tmp/koan-profile/AgentControl/agent.sock"));
}

TEST_F(KoanAgentSocketTest, ALongProfilePathFallsBackToAHashedPath) {
  const base::FilePath deep(
      "/home/example/Library/Application Support/Koan/a/very/long/path/"
      "to/a/profile/directory/that/does/not/fit/in/sun_path");
  const base::FilePath other = deep.Append("2");
  const base::FilePath tmp("/tmp");
  const auto chosen = ChooseSocketPath(deep, 501, tmp);
  ASSERT_TRUE(chosen);
  EXPECT_EQ(chosen->DirName(), tmp.Append("koan-501"));
  EXPECT_LE(chosen->value().size(), kMaxSocketPathLength);
  EXPECT_EQ(chosen->Extension(), ".sock");
  // Stable for a profile, different between profiles and between users.
  EXPECT_EQ(*ChooseSocketPath(deep, 501, tmp), *chosen);
  EXPECT_NE(*ChooseSocketPath(other, 501, tmp), *chosen);
  EXPECT_NE(*ChooseSocketPath(deep, 502, tmp), *chosen);
}

TEST_F(KoanAgentSocketTest, WithoutARootThePerUserTemporaryDirectoryIsUsed) {
  const base::FilePath root = UserTempRoot();
  EXPECT_TRUE(root.IsAbsolute());
  EXPECT_EQ(root, root.StripTrailingSeparators());
  const base::FilePath deep(std::string("/") + std::string(150, 'a'));
  const auto chosen = ChooseSocketPath(deep, 501);
  ASSERT_TRUE(chosen);
  EXPECT_LE(chosen->value().size(), kMaxSocketPathLength);
  EXPECT_EQ(chosen->DirName().BaseName().value(), "koan-501");
  // Under the per-user directory when it is short enough, else /tmp.
  const base::FilePath parent = chosen->DirName().DirName();
  EXPECT_TRUE(parent == root || parent == base::FilePath("/tmp")) << parent;
}

TEST_F(KoanAgentSocketTest, NothingFitsMeansNoPath) {
  const base::FilePath deep(std::string("/") + std::string(200, 'a'));
  EXPECT_FALSE(
      ChooseSocketPath(deep, 501, base::FilePath("/" + std::string(120, 'b'))));
}

TEST_F(KoanAgentSocketTest, ADirectoryIsMadePrivate) {
  const base::FilePath root = ShortRoot();
  const base::FilePath socket = root.Append("AgentControl").Append("a.sock");
  base::ScopedFD lock;
  ASSERT_EQ(PrepareSocketPath(socket, getuid(), &lock),
            SocketPrepareResult::kOk);
  EXPECT_TRUE(lock.is_valid());
  EXPECT_EQ(ModeOf(socket.DirName()), 0700u);
  EXPECT_EQ(ModeOf(socket.DirName().Append("a.sock.lock")), 0600u);
}

TEST_F(KoanAgentSocketTest, ALooseDirectoryOfOursIsTightened) {
  const base::FilePath root = ShortRoot();
  const base::FilePath directory = root.Append("d");
  ASSERT_TRUE(base::CreateDirectory(directory));
  ASSERT_EQ(chmod(directory.value().c_str(), 0755), 0);
  base::ScopedFD lock;
  ASSERT_EQ(PrepareSocketPath(directory.Append("a.sock"), getuid(), &lock),
            SocketPrepareResult::kOk);
  EXPECT_EQ(ModeOf(directory), 0700u);
}

TEST_F(KoanAgentSocketTest, ASymlinkInPlaceOfTheDirectoryIsRefused) {
  const base::FilePath root = ShortRoot();
  const base::FilePath real = root.Append("real");
  const base::FilePath link = root.Append("link");
  ASSERT_TRUE(base::CreateDirectory(real));
  ASSERT_EQ(chmod(real.value().c_str(), 0755), 0);
  ASSERT_TRUE(base::CreateSymbolicLink(real, link));
  base::ScopedFD lock;
  EXPECT_EQ(PrepareSocketPath(link.Append("a.sock"), getuid(), &lock),
            SocketPrepareResult::kDirectoryUnsafe);
  // Nothing was written through it, and its mode was not touched.
  EXPECT_TRUE(base::IsDirectoryEmpty(real));
  EXPECT_EQ(ModeOf(real), 0755u);
  EXPECT_FALSE(lock.is_valid());
}

TEST_F(KoanAgentSocketTest, ADirectoryOfAnotherUserIsRefused) {
  const base::FilePath root = ShortRoot();
  const base::FilePath directory = root.Append("d");
  ASSERT_TRUE(base::CreateDirectory(directory));
  // As if `uid` were someone else: our directory is not theirs.
  base::ScopedFD lock;
  EXPECT_EQ(PrepareSocketPath(directory.Append("a.sock"), getuid() + 1, &lock),
            SocketPrepareResult::kDirectoryUnsafe);
}

TEST_F(KoanAgentSocketTest, ASymlinkInPlaceOfTheLockFileIsRefused) {
  const base::FilePath root = ShortRoot();
  const base::FilePath directory = root.Append("d");
  ASSERT_TRUE(base::CreateDirectory(directory));
  const base::FilePath target = root.Append("target");
  ASSERT_TRUE(base::WriteFile(target, "x"));
  ASSERT_TRUE(
      base::CreateSymbolicLink(target, directory.Append("a.sock.lock")));
  base::ScopedFD lock;
  EXPECT_EQ(PrepareSocketPath(directory.Append("a.sock"), getuid(), &lock),
            SocketPrepareResult::kDirectoryUnsafe);
}

TEST_F(KoanAgentSocketTest, ARegularFileAtThePathIsLeftAlone) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  ASSERT_TRUE(base::WriteFile(path, "precious"));
  base::ScopedFD lock;
  EXPECT_EQ(PrepareSocketPath(path, getuid(), &lock),
            SocketPrepareResult::kNotOurs);
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents, "precious");
}

TEST_F(KoanAgentSocketTest, TwoSocketsInOneDirectoryDoNotShareALock) {
  // The fallback layout puts every long-path profile of a user in one
  // directory.
  const base::FilePath root = ShortRoot();
  base::ScopedFD first, second, again;
  ASSERT_EQ(PrepareSocketPath(root.Append("a.sock"), getuid(), &first),
            SocketPrepareResult::kOk);
  EXPECT_EQ(PrepareSocketPath(root.Append("b.sock"), getuid(), &second),
            SocketPrepareResult::kOk);
  // The same socket is still one browser's.
  EXPECT_EQ(PrepareSocketPath(root.Append("a.sock"), getuid(), &again),
            SocketPrepareResult::kInUse);
  EXPECT_TRUE(base::PathExists(root.Append("a.sock.lock")));
  EXPECT_TRUE(base::PathExists(root.Append("b.sock.lock")));
}

TEST_F(KoanAgentSocketTest, WhoeverHoldsTheLockOwnsThePath) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  base::ScopedFD first_lock;
  ASSERT_EQ(PrepareSocketPath(path, getuid(), &first_lock),
            SocketPrepareResult::kOk);
  net::UnixDomainServerSocket server(base::BindRepeating(&AllowAll), false);
  ASSERT_EQ(server.BindAndListen(path.value(), 1), net::OK);
  const auto identity = SocketIdentityOf(path, getuid());
  ASSERT_TRUE(identity);

  // A second browser finds the path taken, and leaves the live socket alone,
  // however busy that socket is.
  base::ScopedFD second_lock;
  EXPECT_EQ(PrepareSocketPath(path, getuid(), &second_lock),
            SocketPrepareResult::kInUse);
  EXPECT_FALSE(second_lock.is_valid());
  EXPECT_EQ(SocketIdentityOf(path, getuid()), identity);
}

TEST_F(KoanAgentSocketTest, ADeadSocketIsRemovedOnceTheLockIsFree) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  base::ScopedFD lock;
  ASSERT_EQ(PrepareSocketPath(path, getuid(), &lock), SocketPrepareResult::kOk);
  {
    net::UnixDomainServerSocket server(base::BindRepeating(&AllowAll), false);
    ASSERT_EQ(server.BindAndListen(path.value(), 1), net::OK);
  }
  // The listener is gone but bind() left the file; the browser that bound it
  // crashed, which released its lock.
  ASSERT_TRUE(base::PathExists(path));
  lock.reset();
  base::ScopedFD next_lock;
  EXPECT_EQ(PrepareSocketPath(path, getuid(), &next_lock),
            SocketPrepareResult::kOk);
  EXPECT_FALSE(base::PathExists(path));
}

TEST_F(KoanAgentSocketTest, ABoundSocketIsRestrictedToItsOwner) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  net::UnixDomainServerSocket server(base::BindRepeating(&AllowAll), false);
  ASSERT_EQ(server.BindAndListen(path.value(), 1), net::OK);
  ASSERT_TRUE(RestrictSocketPermissions(path, getuid()));
  EXPECT_EQ(ModeOf(path), 0600u);
  EXPECT_FALSE(RestrictSocketPermissions(path, getuid() + 1));
  EXPECT_TRUE(SocketIdentityOf(path, getuid()));
  EXPECT_FALSE(SocketIdentityOf(path, getuid() + 1));
  EXPECT_FALSE(SocketIdentityOf(root.Append("missing"), getuid()));
}

TEST_F(KoanAgentSocketTest, OnlyTheSocketThatWasBoundIsRemoved) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  const base::FilePath moved = root.Append("b.sock");

  net::UnixDomainServerSocket first(base::BindRepeating(&AllowAll), false);
  ASSERT_EQ(first.BindAndListen(path.value(), 1), net::OK);
  const auto first_identity = SocketIdentityOf(path, getuid());
  ASSERT_TRUE(first_identity);

  // Another browser's socket is now at the path (the first file is kept under
  // another name so that its inode cannot be reused for the new one).
  ASSERT_EQ(rename(path.value().c_str(), moved.value().c_str()), 0);
  net::UnixDomainServerSocket second(base::BindRepeating(&AllowAll), false);
  ASSERT_EQ(second.BindAndListen(path.value(), 1), net::OK);
  const auto second_identity = SocketIdentityOf(path, getuid());
  ASSERT_TRUE(second_identity);
  ASSERT_NE(*first_identity, *second_identity);

  EXPECT_FALSE(RemoveSocketIfSame(path, *first_identity));
  EXPECT_TRUE(base::PathExists(path));
  EXPECT_TRUE(RemoveSocketIfSame(path, *second_identity));
  EXPECT_FALSE(base::PathExists(path));
  EXPECT_FALSE(RemoveSocketIfSame(path, *second_identity));  // already gone
}

TEST_F(KoanAgentSocketTest, ALongPathIsRefusedNotTruncated) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path =
      root.Append(std::string(120, 'x')).Append("a.sock");
  base::ScopedFD lock;
  EXPECT_EQ(PrepareSocketPath(path, getuid(), &lock),
            SocketPrepareResult::kError);
}

}  // namespace koan::agent
