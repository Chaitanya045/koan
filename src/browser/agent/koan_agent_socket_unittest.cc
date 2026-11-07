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
      "/Users/someone/Library/Application Support/Koan/a/very/long/path/"
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

TEST_F(KoanAgentSocketTest, NothingFitsMeansNoPath) {
  const base::FilePath deep(std::string("/") + std::string(200, 'a'));
  EXPECT_FALSE(
      ChooseSocketPath(deep, 501, base::FilePath("/" + std::string(120, 'b'))));
}

TEST_F(KoanAgentSocketTest, ADirectoryIsMadePrivate) {
  const base::FilePath root = ShortRoot();
  const base::FilePath socket = root.Append("AgentControl").Append("a.sock");
  ASSERT_EQ(PrepareSocketPath(socket, getuid()), SocketPrepareResult::kOk);
  EXPECT_EQ(ModeOf(socket.DirName()), 0700u);
}

TEST_F(KoanAgentSocketTest, ALooseDirectoryOfOursIsTightened) {
  const base::FilePath root = ShortRoot();
  const base::FilePath directory = root.Append("d");
  ASSERT_TRUE(base::CreateDirectory(directory));
  ASSERT_EQ(chmod(directory.value().c_str(), 0755), 0);
  ASSERT_EQ(PrepareSocketPath(directory.Append("a.sock"), getuid()),
            SocketPrepareResult::kOk);
  EXPECT_EQ(ModeOf(directory), 0700u);
}

TEST_F(KoanAgentSocketTest, ASymlinkInPlaceOfTheDirectoryIsRefused) {
  const base::FilePath root = ShortRoot();
  const base::FilePath real = root.Append("real");
  const base::FilePath link = root.Append("link");
  ASSERT_TRUE(base::CreateDirectory(real));
  ASSERT_TRUE(base::CreateSymbolicLink(real, link));
  EXPECT_EQ(PrepareSocketPath(link.Append("a.sock"), getuid()),
            SocketPrepareResult::kDirectoryUnsafe);
  // And nothing was written through it.
  EXPECT_TRUE(base::IsDirectoryEmpty(real));
}

TEST_F(KoanAgentSocketTest, ADirectoryOfAnotherUserIsRefused) {
  const base::FilePath root = ShortRoot();
  const base::FilePath directory = root.Append("d");
  ASSERT_TRUE(base::CreateDirectory(directory));
  // As if `uid` were someone else: our directory is not theirs.
  EXPECT_EQ(PrepareSocketPath(directory.Append("a.sock"), getuid() + 1),
            SocketPrepareResult::kDirectoryUnsafe);
}

TEST_F(KoanAgentSocketTest, ARegularFileAtThePathIsLeftAlone) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  ASSERT_TRUE(base::WriteFile(path, "precious"));
  EXPECT_EQ(PrepareSocketPath(path, getuid()), SocketPrepareResult::kNotOurs);
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ(contents, "precious");
}

TEST_F(KoanAgentSocketTest, ADeadSocketIsRemovedAndALiveOneIsNot) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path = root.Append("a.sock");
  {
    net::UnixDomainServerSocket server(base::BindRepeating(&AllowAll), false);
    ASSERT_EQ(server.BindAndListen(path.value(), 1), net::OK);
    // A browser is listening: its socket is not ours to remove.
    EXPECT_EQ(PrepareSocketPath(path, getuid()), SocketPrepareResult::kInUse);
    EXPECT_TRUE(base::PathExists(path));
  }
  // The listener is gone but bind() left the file: that is a stale socket.
  ASSERT_TRUE(base::PathExists(path));
  EXPECT_EQ(PrepareSocketPath(path, getuid()), SocketPrepareResult::kOk);
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
}

TEST_F(KoanAgentSocketTest, ALongPathIsRefusedNotTruncated) {
  const base::FilePath root = ShortRoot();
  const base::FilePath path =
      root.Append(std::string(120, 'x')).Append("a.sock");
  EXPECT_EQ(PrepareSocketPath(path, getuid()), SocketPrepareResult::kError);
}

}  // namespace koan::agent
