// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_command.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "base/strings/cstring_view.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

using ::testing::ElementsAre;
using ::testing::Optional;

// Koan's own environment, as the test sets it.
class FakeEnvironment : public base::Environment {
 public:
  explicit FakeEnvironment(std::map<std::string, std::string> vars)
      : vars_(std::move(vars)) {}

  std::optional<std::string> GetVar(base::cstring_view name) override {
    const auto it = vars_.find(std::string(name));
    if (it == vars_.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  bool SetVar(base::cstring_view name, const std::string& value) override {
    vars_[std::string(name)] = value;
    return true;
  }
  bool UnSetVar(base::cstring_view name) override {
    vars_.erase(std::string(name));
    return true;
  }

 private:
  std::map<std::string, std::string> vars_;
};

base::EnvironmentMap SshEnvironment(std::map<std::string, std::string> vars) {
  FakeEnvironment current(std::move(vars));
  return BuildSshEnvironment(base::FilePath("/tmp/koan-x/askpass"), current);
}

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
      argv,
      ElementsAre("/usr/bin/ssh", "-N", "-n", "-T", "-D", "127.0.0.1:41234",
                  "-o", "ExitOnForwardFailure=no", "-o",
                  "ClearAllForwardings=no", "-o", "ServerAliveInterval=10",
                  "-o", "ServerAliveCountMax=3", "-o", "ConnectTimeout=15",
                  "-o", "ControlMaster=no", "-o", "ControlPath=none", "-o",
                  "ForkAfterAuthentication=no", "-o", "RemoteCommand=none",
                  "-o", "RequestTTY=no", "-o", "PermitLocalCommand=no", "-o",
                  "LogLevel=DEBUG1", "-p", "2222", "-l", "me", "--", "box"));
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
  // The watcher's grace before SIGKILL is the platform's.
  EXPECT_NE(SshSupervisorScript().find(base::StrCat(
                {"sleep ", base::NumberToString(kSshKillGraceSeconds), ";"})),
            std::string_view::npos);
}

TEST(KoanSshCommandTest, ForcesTheAskpassHelper) {
  const base::EnvironmentMap environment =
      SshEnvironment({{"SSH_ASKPASS_PROMPT", "confirm"}});
  EXPECT_EQ(environment.at("SSH_ASKPASS"), "/tmp/koan-x/askpass");
  EXPECT_EQ(environment.at("SSH_ASKPASS_REQUIRE"), "force");
  // Empty unsets it for the child.
  EXPECT_EQ(environment.at("SSH_ASKPASS_PROMPT"), "");
  // The agent comes from Koan's own environment.
  EXPECT_FALSE(environment.contains("SSH_AUTH_SOCK"));
}

// A GUI app's PATH is launchd's: Homebrew's tools for a ProxyCommand
// (cloudflared, tailscale) come after the system's.
TEST(KoanSshCommandTest, PathFindsHomebrewToolsAfterTheSystemOnes) {
  EXPECT_EQ(
      SshEnvironment({{"PATH", "/usr/bin:/bin:/usr/sbin:/sbin"}}).at("PATH"),
      "/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin");
  EXPECT_EQ(SshEnvironment({}).at("PATH"),
            "/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin");
  EXPECT_EQ(SshEnvironment({{"PATH", ""}}).at("PATH"),
            "/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin");
  // Started from a terminal: the user's order stays, nothing is repeated.
  EXPECT_EQ(
      SshEnvironment({{"PATH", "/opt/homebrew/bin:/usr/bin:/bin"}}).at("PATH"),
      "/opt/homebrew/bin:/usr/bin:/bin:/usr/local/bin");
  EXPECT_EQ(
      SshEnvironment({{"PATH", "/usr/local/bin:/usr/bin:/opt/homebrew/bin"}})
          .at("PATH"),
      "/usr/local/bin:/usr/bin:/opt/homebrew/bin");
}

TEST(KoanSshCommandTest, SetsAUtf8LocaleOnlyWhenThereIsNone) {
  EXPECT_EQ(SshEnvironment({}).at("LC_CTYPE"), "UTF-8");
  EXPECT_EQ(SshEnvironment({{"LANG", ""}}).at("LC_CTYPE"), "UTF-8");
  for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
    EXPECT_FALSE(SshEnvironment({{name, "de_DE.UTF-8"}}).contains("LC_CTYPE"))
        << name;
  }
}

}  // namespace
}  // namespace koan::remote
