// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_command.h"

#include <algorithm>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

using ::testing::ElementsAre;
using ::testing::Optional;

SshDestination Destination(std::string user,
                           std::string host,
                           std::optional<uint16_t> port) {
  SshDestination destination;
  destination.user = std::move(user);
  destination.host = std::move(host);
  destination.port = port;
  return destination;
}

TEST(KoanSshCommandTest, ParsesDestinations) {
  EXPECT_THAT(ParseSshDestination("box"),
              Optional(Destination("", "box", std::nullopt)));
  EXPECT_THAT(ParseSshDestination("  me@dev.example.com  "),
              Optional(Destination("me", "dev.example.com", std::nullopt)));
  EXPECT_THAT(ParseSshDestination("me@192.0.2.10:2222"),
              Optional(Destination("me", "192.0.2.10", 2222)));
  EXPECT_THAT(ParseSshDestination("box:22"),
              Optional(Destination("", "box", 22)));
  EXPECT_THAT(ParseSshDestination("first.last+ci@box"),
              Optional(Destination("first.last+ci", "box", std::nullopt)));
  EXPECT_THAT(ParseSshDestination("me@[fe80::1]:2200"),
              Optional(Destination("me", "fe80::1", 2200)));
  EXPECT_THAT(ParseSshDestination("[::1]"),
              Optional(Destination("", "::1", std::nullopt)));
  EXPECT_THAT(ParseSshDestination("::1"),
              Optional(Destination("", "::1", std::nullopt)));
}

TEST(KoanSshCommandTest, RejectsAnythingSshCouldReadAsAnOption) {
  for (const char* bad : {
           "",
           "-oProxyCommand=touch /tmp/x",
           "-p",
           "me@-oProxyCommand=x",
           "-me@box",
           "box -oProxyCommand=x",
           "box\t-v",
           "box\n-v",
           "box;reboot",
           "box$(id)",
           "`id`@box",
           "ssh://me@box",
           "me@",
           "@box",
           "me@box:",
           "me@box:0",
           "me@box:65536",
           "me@box:22x",
           "me@box:+22",
           ".box",
           "[box]",
           "[::1",
           "[::1]x",
           "[::1]:",
           "a@b@c",
           "box/path",
           "b\xC3\xA9te",
       }) {
    EXPECT_FALSE(ParseSshDestination(bad)) << bad;
  }
  EXPECT_FALSE(IsValidSshHost(std::string(254, 'a')));
  EXPECT_FALSE(IsValidSshUser(std::string(65, 'a')));
}

TEST(KoanSshCommandTest, RoundTripsDestinations) {
  for (const char* text :
       {"box", "me@box", "me@box:2222", "me@[fe80::1]:22", "[::1]"}) {
    const std::optional<SshDestination> destination = ParseSshDestination(text);
    ASSERT_TRUE(destination) << text;
    EXPECT_EQ(SshDestinationToString(*destination), text);
  }
}

TEST(KoanSshCommandTest, BuildsTheTunnelCommand) {
  const std::vector<std::string> argv = BuildSshTunnelArgv(
      base::FilePath("/usr/bin/ssh"), Destination("me", "box", 2222), 41234);
  EXPECT_THAT(
      argv, ElementsAre(
                "/usr/bin/ssh", "-N", "-n", "-T", "-D", "127.0.0.1:41234", "-o",
                "ExitOnForwardFailure=yes", "-o", "ServerAliveInterval=10",
                "-o", "ServerAliveCountMax=3", "-o", "ConnectTimeout=15", "-o",
                "ControlMaster=no", "-o", "ControlPath=none", "-o",
                "ForkAfterAuthentication=no", "-o", "RemoteCommand=none", "-o",
                "RequestTTY=no", "-o", "PermitLocalCommand=no", "-o",
                "LogLevel=INFO", "-p", "2222", "-l", "me", "--", "box"));
}

TEST(KoanSshCommandTest, TheHostIsAlwaysLastAfterDoubleDash) {
  const std::vector<std::string> argv =
      BuildSshTunnelArgv(base::FilePath("/usr/bin/ssh"),
                         Destination("", "box", std::nullopt), 1080);
  ASSERT_GE(argv.size(), 2u);
  EXPECT_EQ(argv[argv.size() - 2], "--");
  EXPECT_EQ(argv.back(), "box");
  EXPECT_EQ(std::ranges::count(argv, "--"), 1);
  EXPECT_EQ(std::ranges::count(argv, "-l"), 0);
  EXPECT_EQ(std::ranges::count(argv, "-p"), 0);
}

TEST(KoanSshCommandTest, SupervisesSsh) {
  const std::vector<std::string> argv =
      BuildSupervisedArgv(base::FilePath("/bin/sh"), {"/usr/bin/ssh", "box"});
  EXPECT_THAT(argv, ElementsAre("/bin/sh", "-c", SshSupervisorScript(),
                                "koan-ssh-tunnel", "/usr/bin/ssh", "box"));
}

TEST(KoanSshCommandTest, ForcesTheAskpassHelper) {
  const base::EnvironmentMap environment =
      BuildSshEnvironment(base::FilePath("/tmp/koan-x/askpass"));
  EXPECT_EQ(environment.at("SSH_ASKPASS"), "/tmp/koan-x/askpass");
  EXPECT_EQ(environment.at("SSH_ASKPASS_REQUIRE"), "force");
  // Empty unsets it for the child.
  EXPECT_EQ(environment.at("SSH_ASKPASS_PROMPT"), "");
  // The agent comes from Koan's own environment.
  EXPECT_FALSE(environment.contains("SSH_AUTH_SOCK"));
}

}  // namespace
}  // namespace koan::remote
