// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_config.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(KoanSshConfigTest, TokenizesLikeReadconf) {
  const SshConfigFile file = ParseSshConfig(
      "# a comment\n"
      "Host box\n"
      "  HostName 192.0.2.10\n"
      "host=alias-1 alias2   # trailing comment\n"
      "HOST \"quoted name\" 'single'\n"
      "\tHost\ttabbed\r\n"
      "Host a\\\"b\n"
      "Host \"unterminated\n"
      "Hostname not-a-host-line\n"
      "Include config.d/* ~/more/extra\n"
      "Match host x\n"
      "Host after-match\n");
  EXPECT_THAT(file.host_patterns,
              ElementsAre("box", "alias-1", "alias2", "quoted name", "single",
                          "tabbed", "a\"b", "after-match"));
  EXPECT_THAT(file.includes, ElementsAre("config.d/*", "~/more/extra"));
}

TEST(KoanSshConfigTest, OffersOnlyConcreteValidHosts) {
  EXPECT_TRUE(IsConcreteSshHost("box"));
  EXPECT_TRUE(IsConcreteSshHost("dev.example.com"));
  EXPECT_TRUE(IsConcreteSshHost("my_box-2"));
  EXPECT_FALSE(IsConcreteSshHost("*"));
  EXPECT_FALSE(IsConcreteSshHost("*.example.com"));
  EXPECT_FALSE(IsConcreteSshHost("web?"));
  EXPECT_FALSE(IsConcreteSshHost("!bastion"));
  EXPECT_FALSE(IsConcreteSshHost("-oProxyCommand=x"));
  EXPECT_FALSE(IsConcreteSshHost("quoted name"));
  EXPECT_FALSE(IsConcreteSshHost("user@box"));
  EXPECT_FALSE(IsConcreteSshHost(""));
}

TEST(KoanSshConfigTest, ResolvesIncludesLikeTheUserConfig) {
  const base::FilePath home("/Users/me");
  EXPECT_EQ(ResolveSshInclude("config.d/*", home),
            home.Append(".ssh").Append("config.d/*"));
  EXPECT_EQ(ResolveSshInclude("~/work/ssh.conf", home),
            base::FilePath("/Users/me/work/ssh.conf"));
  EXPECT_EQ(ResolveSshInclude("/etc/ssh/extra", home),
            base::FilePath("/etc/ssh/extra"));
  EXPECT_TRUE(ResolveSshInclude("~other/config", home).empty());
  EXPECT_TRUE(ResolveSshInclude("%d/config", home).empty());
  EXPECT_TRUE(ResolveSshInclude("${HOME}/config", home).empty());
}

TEST(KoanSshConfigTest, ReadsIncludedFilesInOrder) {
  base::ScopedTempDir home;
  ASSERT_TRUE(home.CreateUniqueTempDir());
  const base::FilePath ssh = home.GetPath().Append(".ssh");
  ASSERT_TRUE(base::CreateDirectory(ssh.Append("config.d")));
  ASSERT_TRUE(base::WriteFile(ssh.Append("config"),
                              "Host first *.wild\n"
                              "Include config.d/*.conf\n"
                              "Include config\n"  // A loop: read once.
                              "Host last first\n"));
  ASSERT_TRUE(base::WriteFile(ssh.Append("config.d/b.conf"), "Host bee\n"));
  ASSERT_TRUE(base::WriteFile(ssh.Append("config.d/a.conf"),
                              "Host ay !neg\nInclude ~/nested\n"));
  ASSERT_TRUE(
      base::WriteFile(ssh.Append("config.d/ignored.txt"), "Host nope\n"));
  ASSERT_TRUE(
      base::WriteFile(home.GetPath().Append("nested"), "Host nested\n"));
  EXPECT_THAT(ReadSshConfigHosts(home.GetPath()),
              ElementsAre("first", "ay", "nested", "bee", "last"));
}

TEST(KoanSshConfigTest, NoConfigNoHosts) {
  base::ScopedTempDir home;
  ASSERT_TRUE(home.CreateUniqueTempDir());
  EXPECT_THAT(ReadSshConfigHosts(home.GetPath()), IsEmpty());
}

}  // namespace
}  // namespace koan::remote
