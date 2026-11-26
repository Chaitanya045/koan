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
  // The tunnel itself notices its own forward failing (ReadSshForwardLine());
  // when ssh exits, a bind error is the user's own forward's.
  EXPECT_EQ(Classify({"bind [127.0.0.1]:3000: Address already in use",
                      "channel_setup_fwd_listener_tcpip: cannot listen to "
                      "port: 3000",
                      "Connection to box closed by remote host."},
                     255, true),
            SshFailure::kConnectionLost);
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

// LogLevel=DEBUG1 prints a hundred lines or more: none of them is kept, so
// the errors stay, and so does the detail.
TEST(KoanSshFailureTest, DropsDebugChatter) {
  std::vector<std::string> lines;
  AddSshStderrLine(lines,
                   "ssh: connect to host box port 22: Connection "
                   "refused\r");
  for (int i = 0; i < 100; ++i) {
    AddSshStderrLine(lines,
                     "debug1: Reading configuration data "
                     "/Users/me/.ssh/config\r");
    AddSshStderrLine(lines,
                     "debug1: Executing proxy command: exec "
                     "cloudflared access ssh --hostname box\r");
    AddSshStderrLine(lines, "debug2: channel 0: open confirm rwindow 0\r");
    AddSshStderrLine(lines, "debug3: send packet: type 5\r");
    AddSshStderrLine(lines, "debug1: Remote: Agent forwarding disabled.\r");
  }
  AddSshStderrLine(lines,
                   "Authenticated to box ([192.0.2.10]:22) using "
                   "\"publickey\".\r");
  AddSshStderrLine(lines,
                   "Transferred: sent 2928, received 2580 bytes, in "
                   "0.1 seconds\r");
  AddSshStderrLine(lines,
                   "Bytes per second: sent 22834.8, received "
                   "20121.0\r");
  EXPECT_THAT(lines, ElementsAre("ssh: connect to host box port 22: Connection "
                                 "refused"));
  EXPECT_EQ(SshFailureDetail(lines),
            "ssh: connect to host box port 22: Connection refused");
  EXPECT_TRUE(IsSshChatterLine("debug1: Entering interactive session."));
  EXPECT_FALSE(IsSshChatterLine("Timeout, server box not responding."));
  EXPECT_FALSE(
      IsSshChatterLine("bind [127.0.0.1]:41000: Address already in "
                       "use"));
}

// As OpenSSH 9.9 (channels.c, ssh.c, clientloop.c) and 10.4 print them.
TEST(KoanSshFailureTest, ReadsItsOwnForwardsLines) {
  constexpr uint16_t kPort = 41000;
  EXPECT_EQ(ReadSshForwardLine(
                "debug1: Local forwarding listening on 127.0.0.1 port 41000.\r",
                kPort),
            SshForwardLine::kBinding);
  EXPECT_EQ(
      ReadSshForwardLine("debug1: Entering interactive session.\r", kPort),
      SshForwardLine::kSessionStarted);
  for (const char* line :
       {"bind [127.0.0.1]:41000: Address already in use\r",
        "listen [127.0.0.1]:41000: Invalid argument",
        "socket [127.0.0.1]:41000: Too many open files",
        "channel_setup_fwd_listener_tcpip: cannot listen to port: 41000",
        "channel_setup_fwd_listener_tcpip: getaddrinfo(127.0.0.1): Address "
        "family for hostname not supported",
        "Could not request local forwarding."}) {
    EXPECT_EQ(ReadSshForwardLine(line, kPort), SshForwardLine::kFailed) << line;
  }
  for (const char* line :
       {"debug1: Local forwarding listening on 127.0.0.1 port 41001.",
        "debug1: Local forwarding listening on 127.0.0.1 port 410001.",
        "debug1: Local forwarding listening on ::1 port 41000.",
        "debug1: Local forwarding listening on 127.0.0.10 port 41000.",
        "Local forwarding listening on 127.0.0.1 port 41000.",
        "x debug1: Entering interactive session.",
        "bind [127.0.0.1]:4100: Address already in use",
        "bind [127.0.0.1]:410001: Address already in use",
        "bind [::1]:41000: Address already in use",
        "channel_setup_fwd_listener_tcpip: cannot listen to port: 410001",
        "channel_setup_fwd_listener_tcpip: cannot listen to port: 1000",
        "Timeout, server box not responding."}) {
    EXPECT_EQ(ReadSshForwardLine(line, kPort), SshForwardLine::kOther) << line;
  }
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
