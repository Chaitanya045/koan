// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_failure.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

using ::testing::ElementsAre;

SshFailure Classify(std::vector<std::string> lines,
                    int exit_code = 255,
                    bool was_connected = false) {
  return ClassifySshExit(lines, exit_code, was_connected);
}

// The messages are OpenSSH 9.9's (see the design note for the sources).
TEST(KoanSshFailureTest, ClassifiesOpenSshMessages) {
  EXPECT_EQ(
      Classify({"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@",
                "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @",
                "Host key for box has changed and you have requested strict "
                "checking.",
                "Host key verification failed."}),
      SshFailure::kHostKeyChanged);
  EXPECT_EQ(Classify({"Host key verification failed."}),
            SshFailure::kHostKeyNotVerified);
  EXPECT_EQ(Classify({"No ED25519 host key is known for box and you have "
                      "requested strict checking.",
                      "Host key verification failed."}),
            SshFailure::kHostKeyNotVerified);
  EXPECT_EQ(Classify({"me@box: Permission denied (publickey,password)."}),
            SshFailure::kAuthFailed);
  EXPECT_EQ(Classify({"Received disconnect from 192.0.2.10 port 22:2: Too many "
                      "authentication failures"}),
            SshFailure::kAuthFailed);
  EXPECT_EQ(Classify({"ssh: Could not resolve hostname nobox: nodename nor "
                      "servname provided, or not known"}),
            SshFailure::kHostNotFound);
  EXPECT_EQ(Classify({"ssh: connect to host box port 22: Connection refused"}),
            SshFailure::kHostUnreachable);
  EXPECT_EQ(Classify({"ssh: connect to host box port 22: Operation timed out"}),
            SshFailure::kHostUnreachable);
  EXPECT_EQ(Classify({"Connection timed out during banner exchange"}),
            SshFailure::kHostUnreachable);
  EXPECT_EQ(Classify({"bind [127.0.0.1]:41234: Address already in use",
                      "channel_setup_fwd_listener_tcpip: cannot listen to "
                      "port: 41234",
                      "Could not request local forwarding."}),
            SshFailure::kPortInUse);
  EXPECT_EQ(Classify({"Timeout, server box not responding."}, 255, true),
            SshFailure::kConnectionLost);
  EXPECT_EQ(Classify({"Connection to box closed by remote host."}, 255, true),
            SshFailure::kConnectionLost);
  EXPECT_EQ(Classify({"Connection closed by 192.0.2.10 port 22"}, 255, true),
            SshFailure::kConnectionLost);
  EXPECT_EQ(Classify({"Connection closed by 192.0.2.10 port 22"}, 255, false),
            SshFailure::kHostUnreachable);
  EXPECT_EQ(Classify({"command-line: line 0: Bad configuration option: "
                      "forkafterauthentication"}),
            SshFailure::kSshTooOld);
  EXPECT_EQ(Classify({"/Users/me/.ssh/config: line 3: Bad configuration "
                      "option: hostnme",
                      "/Users/me/.ssh/config: terminating, 1 bad "
                      "configuration options"}),
            SshFailure::kBadConfig);
  EXPECT_EQ(Classify({}, 127), SshFailure::kSshNotFound);
  EXPECT_EQ(Classify({}, -1, true), SshFailure::kConnectionLost);
  EXPECT_EQ(Classify({"something new"}), SshFailure::kUnknown);
}

TEST(KoanSshFailureTest, RetriesOnlyWhatTimeCanFix) {
  EXPECT_TRUE(IsRetryableSshFailure(SshFailure::kHostUnreachable));
  EXPECT_TRUE(IsRetryableSshFailure(SshFailure::kHostNotFound));
  EXPECT_TRUE(IsRetryableSshFailure(SshFailure::kConnectionLost));
  EXPECT_TRUE(IsRetryableSshFailure(SshFailure::kPortInUse));
  EXPECT_TRUE(IsRetryableSshFailure(SshFailure::kUnknown));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kHostKeyChanged));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kHostKeyNotVerified));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kAuthFailed));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kCancelled));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kBadConfig));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kSshTooOld));
  EXPECT_FALSE(IsRetryableSshFailure(SshFailure::kSshNotFound));
}

TEST(KoanSshFailureTest, KeepsTheLastSanitizedLines) {
  std::vector<std::string> lines;
  AddSshStderrLine(lines, "  \r ");
  AddSshStderrLine(lines, "Received disconnect: \x1b[31mred\x1b[0m\tok\r");
  AddSshStderrLine(lines,
                   "bidi \xE2\x80\xAE"
                   "evil");
  AddSshStderrLine(lines, "bad \xFF byte");
  EXPECT_THAT(lines, ElementsAre("Received disconnect: [31mred[0m ok",
                                 "bidi evil", "bad \xEF\xBF\xBD byte"));
  for (int i = 0; i < 100; ++i) {
    AddSshStderrLine(lines, std::string(1000, 'x'));
  }
  EXPECT_EQ(lines.size(), kMaxSshStderrLines);
  EXPECT_EQ(lines.back().size(), kMaxSshStderrLineLength);
}

TEST(KoanSshFailureTest, DetailIsTheLastErrorLine) {
  EXPECT_EQ(SshFailureDetail({"ssh: connect to host box port 22: Connection "
                              "refused",
                              "some trailing note"}),
            "ssh: connect to host box port 22: Connection refused");
  EXPECT_EQ(SshFailureDetail({"just this"}), "just this");
  EXPECT_EQ(SshFailureDetail({}), "");
}

}  // namespace
}  // namespace koan::remote
