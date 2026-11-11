// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_relay_gate.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

constexpr char kSecret[] = "0123456789abcdef0123456789abcdef";
constexpr char kHost[] = "127.0.0.1:4711";

class KoanRelayGateTest : public testing::Test {
 protected:
  base::TimeTicks Start() const { return start_; }
  base::TimeTicks After(base::TimeDelta delta) const { return start_ + delta; }

  const base::TimeTicks start_ = base::TimeTicks() + base::Hours(1);
  RelayGate gate_{kSecret, 4711, start_};
  const std::string path_ = std::string("/cdp/") + kSecret;
};

TEST_F(KoanRelayGateTest, TheOneWebSocketWithTheSecret) {
  EXPECT_EQ(gate_.Url(), std::string("ws://127.0.0.1:4711/cdp/") + kSecret);
  EXPECT_EQ(gate_.OnWebSocketRequest(1, path_, kHost, std::nullopt,
                                     After(base::Seconds(2))),
            RelayGate::Decision::kAccept);
  EXPECT_EQ(gate_.active(), 1);
  // One at a time.
  EXPECT_EQ(gate_.OnWebSocketRequest(2, path_, kHost, std::nullopt,
                                     After(base::Seconds(3))),
            RelayGate::Decision::kBusy);
  // The same daemon reconnects later, while the relay lives.
  gate_.OnClosed(1);
  EXPECT_FALSE(gate_.active());
  EXPECT_EQ(gate_.OnWebSocketRequest(3, path_, kHost, std::nullopt,
                                     After(base::Minutes(10))),
            RelayGate::Decision::kAccept);
  EXPECT_FALSE(gate_.FirstConnectionMissed(After(base::Minutes(10))));
}

TEST_F(KoanRelayGateTest, PagesWrongHostsAndWrongSecretsAreRefused) {
  const base::TimeTicks now = After(base::Seconds(1));
  // A web page always sends an Origin, even an empty or "null" one.
  for (const char* origin : {"https://evil.example", "null", ""}) {
    EXPECT_EQ(
        gate_.OnWebSocketRequest(1, path_, kHost, std::string(origin), now),
        RelayGate::Decision::kOriginRefused)
        << origin;
  }
  // DNS rebinding and other names for loopback.
  for (const char* host : {"localhost:4711", "evil.example:4711", "127.0.0.1",
                           "127.0.0.1:4712", "[::1]:4711", ""}) {
    EXPECT_EQ(gate_.OnWebSocketRequest(1, path_, host, std::nullopt, now),
              RelayGate::Decision::kBadHost)
        << host;
  }
  for (std::string path :
       {std::string("/cdp/"), std::string("/cdp/") + kSecret + "0",
        std::string("/cdp/0123456789abcdef0123456789abcdee"),
        std::string("/json/version"), std::string("/")}) {
    EXPECT_EQ(gate_.OnWebSocketRequest(1, path, kHost, std::nullopt, now),
              RelayGate::Decision::kNotFound)
        << path;
  }
  EXPECT_FALSE(gate_.active());
  EXPECT_FALSE(gate_.used());
}

TEST_F(KoanRelayGateTest, TheFirstConnectionMustComeSoon) {
  EXPECT_FALSE(gate_.FirstConnectionMissed(After(kRelayFirstConnection)));
  const base::TimeTicks late = After(kRelayFirstConnection + base::Seconds(1));
  EXPECT_TRUE(gate_.FirstConnectionMissed(late));
  EXPECT_EQ(gate_.OnWebSocketRequest(1, path_, kHost, std::nullopt, late),
            RelayGate::Decision::kExpired);
  EXPECT_EQ(DecisionName(RelayGate::Decision::kExpired), "expired");
}

TEST(KoanRelaySessionTest, SessionsAndFiles) {
  std::string error;
  EXPECT_EQ(SessionFromArgs({"open", "https://a.test"}, nullptr, &error),
            "default");
  EXPECT_EQ(SessionFromArgs({"--session", "work", "snapshot"}, nullptr, &error),
            "work");
  EXPECT_EQ(SessionFromArgs({"--session=a.b-c_1", "tab"}, "env", &error),
            "a.b-c_1");
  EXPECT_EQ(SessionFromArgs({"snapshot"}, "from-env", &error), "from-env");
  EXPECT_FALSE(SessionFromArgs({"--session", "../x"}, nullptr, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(IsValidSessionName(std::string(65, 'a')));
  EXPECT_FALSE(IsValidSessionName("a b"));

  const base::FilePath home = RelayHome(base::FilePath("/home/u/.koan"));
  EXPECT_EQ(home.value(), "/home/u/.koan/relay");
  // Different sessions never share files, and the names stay short.
  EXPECT_NE(RelayControlSocket(home, "a"), RelayControlSocket(home, "b"));
  EXPECT_EQ(RelayControlSocket(home, "a").DirName(),
            RelayEnvFile(home, "a").DirName());
  EXPECT_LT(RelayControlSocket(home, std::string(64, 'x')).value().size(), 60u);
}

TEST(KoanRelaySessionTest, TheEnvFile) {
  const std::string url = std::string("ws://127.0.0.1:4711/cdp/") + kSecret;
  EXPECT_EQ(RelayEnvContents(url), "AGENT_BROWSER_CDP=" + url + "\n");
  EXPECT_EQ(ParseRelayEnv(RelayEnvContents(url)), url);
  EXPECT_FALSE(ParseRelayEnv("AGENT_BROWSER_CDP=ws://evil.example/cdp/x"));
  EXPECT_FALSE(ParseRelayEnv("PATH=/bin"));
  EXPECT_FALSE(ParseRelayEnv("AGENT_BROWSER_CDP=ws://127.0.0.1:1/a b"));
}

}  // namespace
}  // namespace koan::cli
