// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_tunnel.h"

#include <memory>
#include <vector>

#include "base/scoped_observation.h"
#include "base/test/task_environment.h"
#include "koan/browser/remote/koan_ssh_test_fakes.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

using ::testing::ElementsAre;

class StateRecorder : public SshTunnel::Observer {
 public:
  void OnSshTunnelStatusChanged(const SshTunnelStatus& status) override {
    states.push_back(status.state);
  }
  std::vector<SshTunnelState> states;
};

SshPrompt Prompt(SshPromptKind kind, std::string text) {
  SshPrompt prompt;
  prompt.kind = kind;
  prompt.text = std::move(text);
  return prompt;
}

class KoanSshTunnelTest : public testing::Test {
 protected:
  KoanSshTunnelTest() {
    SshDestination destination;
    destination.user = "me";
    destination.host = "box";
    tunnel_ = std::make_unique<SshTunnel>(destination, &platform_, &prompts_);
    observation_.Observe(tunnel_.get());
  }

  const SshTunnelStatus& status() { return tunnel_->status(); }

  // Starts, and ssh listens.
  void Connect() {
    tunnel_->Start();
    platform_.set_socks_ready(true);
    platform_.EmitListening();
    ASSERT_EQ(status().state, SshTunnelState::kConnected);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakeSshTunnelPlatform platform_;
  FakeSshPromptDelegate prompts_;
  StateRecorder recorder_;
  std::unique_ptr<SshTunnel> tunnel_;
  base::ScopedObservation<SshTunnel, SshTunnel::Observer> observation_{
      &recorder_};
};

TEST_F(KoanSshTunnelTest, ConnectsOnceSshListensAndThePortAnswers) {
  platform_.set_free_ports({41000});
  tunnel_->Start();
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  ASSERT_EQ(platform_.launched().size(), 1u);
  EXPECT_EQ(platform_.launched()[0].destination.host, "box");
  EXPECT_EQ(platform_.launched()[0].port, 41000);

  // ssh authenticates: nothing is probed yet.
  platform_.EmitStderr("debug1: Connecting to box [192.0.2.10] port 22.\r");
  task_environment_.FastForwardBy(base::Seconds(10));
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  EXPECT_EQ(platform_.probes(), 0);

  // ssh listens, but the SOCKS greeting isn't answered yet: probe again.
  platform_.EmitListening();
  EXPECT_EQ(platform_.probes(), 1);
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  platform_.set_socks_ready(true);
  task_environment_.FastForwardBy(SshTunnel::kProbeInterval);
  EXPECT_EQ(status().state, SshTunnelState::kConnected);
  EXPECT_EQ(status().port, 41000);
  EXPECT_EQ(status().failure, SshFailure::kNone);
  // No more probes once connected.
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_EQ(platform_.probes(), 2);
  EXPECT_THAT(recorder_.states, ElementsAre(SshTunnelState::kConnecting,
                                            SshTunnelState::kConnected));
}

// Something else answering on the port (another user's process that took it
// before ssh could bind it) must never count.
TEST_F(KoanSshTunnelTest, AnAnswerWithoutSshListeningNeverConnects) {
  platform_.set_free_ports({41000});
  platform_.set_socks_ready(true);
  tunnel_->Start();
  task_environment_.FastForwardBy(base::Minutes(1));
  // A ProxyJump's ssh entering its session, or ssh's own without the line.
  platform_.EmitStderr("debug1: Entering interactive session.\r");
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  EXPECT_EQ(platform_.probes(), 0);
}

TEST_F(KoanSshTunnelTest, OnlyTheLineForItsOwnPortCounts) {
  platform_.set_free_ports({41000});
  platform_.set_socks_ready(true);
  tunnel_->Start();
  for (const char* line :
       {"debug1: Local forwarding listening on 127.0.0.1 port 41001.",
        "debug1: Local forwarding listening on 127.0.0.1 port 4100.",
        "debug1: Local forwarding listening on 127.0.0.1 port 410000.",
        "debug1: Local forwarding listening on ::1 port 41000.",
        "debug1: Local forwarding listening on 0.0.0.0 port 41000.",
        "debug1: Local forwarding listening on 127.0.0.2 port 41000.",
        "Local forwarding listening on 127.0.0.1 port 41000.",
        "debug1: Entering interactive session."}) {
    platform_.EmitStderr(line);
  }
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  EXPECT_EQ(platform_.probes(), 0);
}

// The user's config has "LocalForward 3000 localhost:3000", and a terminal's
// ssh holds port 3000: ssh carries on without it (as verified with OpenSSH
// 10.4), and so does the tunnel.
TEST_F(KoanSshTunnelTest, TheUsersOwnFailingForwardIsHarmless) {
  platform_.set_free_ports({41000});
  platform_.set_socks_ready(true);
  tunnel_->Start();
  for (const char* line : {
           "debug1: Local forwarding listening on 127.0.0.1 port 41000.\r",
           "debug1: channel 0: new port-listener [port listener] (inactive "
           "timeout: 0)\r",
           "debug1: Local connections to LOCALHOST:3000 forwarded to remote "
           "address localhost:3000\r",
           "debug1: Local forwarding listening on ::1 port 3000.\r",
           "bind [::1]:3000: Address already in use\r",
           "debug1: Local forwarding listening on 127.0.0.1 port 3000.\r",
           "bind [127.0.0.1]:3000: Address already in use\r",
           "channel_setup_fwd_listener_tcpip: cannot listen to port: 3000\r",
           "debug1: Requesting no-more-sessions@openssh.com\r",
           "debug1: Entering interactive session.\r",
       }) {
    platform_.EmitStderr(line);
  }
  EXPECT_EQ(status().state, SshTunnelState::kConnected);
  EXPECT_EQ(status().port, 41000);
  // Its bind error doesn't make a later drop a port clash.
  platform_.EmitStderr("Timeout, server box not responding.\r");
  platform_.Exit(255);
  EXPECT_EQ(status().state, SshTunnelState::kRetrying);
  EXPECT_EQ(status().failure, SshFailure::kConnectionLost);
  EXPECT_EQ(status().detail, "Timeout, server box not responding.");
}

TEST_F(KoanSshTunnelTest, PromptsGoToTheDelegateAndBack) {
  tunnel_->Start();
  platform_.EmitPrompt(Prompt(SshPromptKind::kSecret, "me@box's password: "));
  EXPECT_EQ(status().state, SshTunnelState::kNeedsInput);
  ASSERT_TRUE(status().prompt);
  EXPECT_EQ(status().prompt->text, "me@box's password: ");
  ASSERT_EQ(prompts_.shown().size(), 1u);
  EXPECT_EQ(prompts_.shown()[0].destination, "me@box");

  prompts_.Answer(SshPromptReply::WithSecret(SshSecret("hunter2")));
  ASSERT_EQ(platform_.replies().size(), 1u);
  EXPECT_TRUE(platform_.replies()[0].accepted);
  EXPECT_EQ(platform_.replies()[0].secret.value(), "hunter2");
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  EXPECT_FALSE(status().prompt);
}

TEST_F(KoanSshTunnelTest, CancellingASecretStopsTheAttempt) {
  tunnel_->Start();
  platform_.EmitPrompt(Prompt(SshPromptKind::kSecret, "Verification code: "));
  prompts_.Answer(SshPromptReply::Cancel());
  ASSERT_EQ(platform_.replies().size(), 1u);
  EXPECT_FALSE(platform_.replies()[0].accepted);
  EXPECT_FALSE(platform_.launched()[0].alive);
  EXPECT_EQ(status().state, SshTunnelState::kFailed);
  EXPECT_EQ(status().failure, SshFailure::kCancelled);
  task_environment_.FastForwardBy(base::Minutes(10));
  EXPECT_EQ(platform_.launched().size(), 1u);
}

TEST_F(KoanSshTunnelTest, AChangedHostKeyIsNeverRetried) {
  tunnel_->Start();
  platform_.EmitStderr(
      "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @");
  platform_.EmitStderr("Host key verification failed.");
  platform_.Exit(255);
  EXPECT_EQ(status().state, SshTunnelState::kFailed);
  EXPECT_EQ(status().failure, SshFailure::kHostKeyChanged);
  EXPECT_EQ(status().detail, "Host key verification failed.");
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_EQ(platform_.launched().size(), 1u);

  // Only the user starts again.
  tunnel_->Reconnect();
  EXPECT_EQ(platform_.launched().size(), 2u);
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
}

TEST_F(KoanSshTunnelTest, ARejectedHostKeyFails) {
  tunnel_->Start();
  SshPrompt prompt = Prompt(SshPromptKind::kHostKey, "The authenticity ...");
  prompt.fingerprint = "SHA256:abc";
  platform_.EmitPrompt(prompt);
  ASSERT_EQ(prompts_.shown().size(), 1u);
  EXPECT_EQ(prompts_.shown()[0].prompt.fingerprint, "SHA256:abc");
  prompts_.Answer(SshPromptReply::No());
  ASSERT_EQ(platform_.replies().size(), 1u);
  EXPECT_FALSE(platform_.replies()[0].accepted);
  // ssh exits by itself.
  EXPECT_TRUE(platform_.launched()[0].alive);
  platform_.EmitStderr("Host key verification failed.");
  platform_.Exit(255);
  EXPECT_EQ(status().state, SshTunnelState::kFailed);
  EXPECT_EQ(status().failure, SshFailure::kHostKeyNotVerified);
}

TEST_F(KoanSshTunnelTest, RetriesWithBackoff) {
  tunnel_->Start();
  const base::TimeTicks start = base::TimeTicks::Now();
  platform_.EmitStderr("ssh: connect to host box port 22: Connection refused");
  platform_.Exit(255);
  EXPECT_EQ(status().state, SshTunnelState::kRetrying);
  EXPECT_EQ(status().failure, SshFailure::kHostUnreachable);
  EXPECT_EQ(status().detail,
            "ssh: connect to host box port 22: Connection refused");
  EXPECT_EQ(status().next_attempt, start + base::Seconds(1));
  EXPECT_EQ(status().failed_attempts, 1);

  for (int seconds : {1, 2, 4, 8, 16, 32, 60, 60}) {
    task_environment_.FastForwardBy(base::Seconds(seconds) -
                                    base::Milliseconds(1));
    const size_t launches = platform_.launched().size();
    task_environment_.FastForwardBy(base::Milliseconds(1));
    ASSERT_EQ(platform_.launched().size(), launches + 1) << seconds;
    platform_.Exit(255);
  }
}

TEST_F(KoanSshTunnelTest, APortClashIsRetriedAtOnceOnANewPort) {
  platform_.set_free_ports({41000, 41001});
  Connect();
  platform_.EmitStderr("Timeout, server box not responding.");
  platform_.Exit(255);
  EXPECT_EQ(status().failure, SshFailure::kConnectionLost);
  task_environment_.FastForwardBy(base::Seconds(1));
  // The same port again.
  ASSERT_EQ(platform_.launched().size(), 2u);
  EXPECT_EQ(platform_.preferred_ports().back(), 41000);
  EXPECT_EQ(platform_.launched()[1].port, 41000);

  // Someone took the port: ssh would run on without it, so the tunnel ends
  // the attempt itself.
  platform_.EmitStderr(
      "debug1: Local forwarding listening on 127.0.0.1 port 41000.\r");
  platform_.EmitStderr("bind [127.0.0.1]:41000: Address already in use\r");
  EXPECT_FALSE(platform_.launched()[1].alive);
  EXPECT_EQ(status().state, SshTunnelState::kRetrying);
  EXPECT_EQ(status().failure, SshFailure::kPortInUse);
  EXPECT_EQ(status().detail, "bind [127.0.0.1]:41000: Address already in use");
  EXPECT_EQ(status().next_attempt, base::TimeTicks::Now());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(platform_.launched().size(), 3u);
  EXPECT_EQ(platform_.preferred_ports().back(), std::nullopt);
  EXPECT_EQ(platform_.launched()[2].port, 41001);
}

TEST_F(KoanSshTunnelTest, ALongConnectionResetsTheBackoff) {
  tunnel_->Start();
  platform_.Exit(255);
  task_environment_.FastForwardBy(base::Seconds(1));
  platform_.Exit(255);
  EXPECT_EQ(status().failed_attempts, 2);
  task_environment_.FastForwardBy(base::Seconds(2));
  platform_.set_socks_ready(true);
  platform_.EmitListening();
  ASSERT_EQ(status().state, SshTunnelState::kConnected);
  task_environment_.FastForwardBy(SshTunnel::kStableConnection);
  platform_.Exit(255);
  EXPECT_EQ(status().failed_attempts, 1);
  EXPECT_EQ(status().next_attempt, base::TimeTicks::Now() + base::Seconds(1));
}

TEST_F(KoanSshTunnelTest, AFlappingConnectionKeepsBackingOff) {
  tunnel_->Start();
  platform_.Exit(255);
  task_environment_.FastForwardBy(base::Seconds(1));
  platform_.Exit(255);
  task_environment_.FastForwardBy(base::Seconds(2));
  platform_.set_socks_ready(true);
  platform_.EmitListening();
  ASSERT_EQ(status().state, SshTunnelState::kConnected);
  EXPECT_EQ(status().failed_attempts, 0);
  task_environment_.FastForwardBy(base::Seconds(1));
  platform_.Exit(255);
  EXPECT_EQ(status().next_attempt, base::TimeTicks::Now() + base::Seconds(4));
}

TEST_F(KoanSshTunnelTest, ANetworkChangeRestartsAKeyTunnel) {
  Connect();
  tunnel_->OnNetworkChanged();
  EXPECT_FALSE(platform_.launched()[0].alive);
  ASSERT_EQ(platform_.launched().size(), 2u);
  EXPECT_EQ(platform_.launched()[1].port, platform_.launched()[0].port);
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
  // Already reconnecting: a second change (or the resume) does nothing.
  tunnel_->OnNetworkChanged();
  tunnel_->OnResume();
  EXPECT_EQ(platform_.launched().size(), 2u);
}

TEST_F(KoanSshTunnelTest, ANetworkChangeKeepsATunnelThatNeededAPassword) {
  tunnel_->Start();
  platform_.EmitPrompt(Prompt(SshPromptKind::kSecret, "password: "));
  prompts_.Answer(SshPromptReply::WithSecret(SshSecret("pw")));
  platform_.set_socks_ready(true);
  platform_.EmitListening();
  ASSERT_EQ(status().state, SshTunnelState::kConnected);
  tunnel_->OnNetworkChanged();
  tunnel_->OnResume();
  EXPECT_EQ(platform_.launched().size(), 1u);
  EXPECT_EQ(status().state, SshTunnelState::kConnected);
}

TEST_F(KoanSshTunnelTest, ANetworkChangeRetriesAtOnce) {
  tunnel_->Start();
  for (int i = 0; i < 6; ++i) {
    platform_.Exit(255);
    task_environment_.FastForwardBy(status().next_attempt -
                                    base::TimeTicks::Now());
  }
  platform_.Exit(255);
  ASSERT_EQ(status().state, SshTunnelState::kRetrying);
  EXPECT_GT(status().next_attempt - base::TimeTicks::Now(), base::Seconds(30));
  const size_t launches = platform_.launched().size();
  tunnel_->OnNetworkChanged();
  EXPECT_EQ(platform_.launched().size(), launches + 1);
  EXPECT_EQ(status().failed_attempts, 0);
}

TEST_F(KoanSshTunnelTest, NoAttemptsWhileAsleep) {
  tunnel_->Start();
  platform_.Exit(255);
  tunnel_->OnSuspend();
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_EQ(platform_.launched().size(), 1u);
  tunnel_->OnNetworkChanged();
  EXPECT_EQ(platform_.launched().size(), 1u);
  tunnel_->OnResume();
  EXPECT_EQ(platform_.launched().size(), 2u);
  EXPECT_EQ(status().state, SshTunnelState::kConnecting);
}

TEST_F(KoanSshTunnelTest, StopKillsSshAndClosesThePrompt) {
  tunnel_->Start();
  platform_.EmitPrompt(Prompt(SshPromptKind::kSecret, "password: "));
  const int id = prompts_.shown()[0].id;
  tunnel_->Stop();
  EXPECT_FALSE(platform_.launched()[0].alive);
  EXPECT_THAT(prompts_.cancelled(), ElementsAre(id));
  EXPECT_EQ(status().state, SshTunnelState::kStopped);
  // A late answer goes nowhere.
  prompts_.Answer(SshPromptReply::WithSecret(SshSecret("pw")));
  EXPECT_TRUE(platform_.replies().empty());
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_EQ(platform_.launched().size(), 1u);
}

TEST_F(KoanSshTunnelTest, ANoticeEndsWhenTheTunnelConnects) {
  tunnel_->Start();
  platform_.EmitPrompt(
      Prompt(SshPromptKind::kNotice, "Confirm user presence for key"));
  ASSERT_EQ(prompts_.shown().size(), 1u);
  EXPECT_TRUE(prompts_.shown()[0].reply.is_null());
  EXPECT_EQ(status().state, SshTunnelState::kNeedsInput);
  platform_.set_socks_ready(true);
  platform_.EmitListening();
  EXPECT_EQ(status().state, SshTunnelState::kConnected);
  EXPECT_THAT(prompts_.cancelled(), ElementsAre(prompts_.shown()[0].id));
}

TEST_F(KoanSshTunnelTest, ExitWhilePromptingClosesThePrompt) {
  tunnel_->Start();
  platform_.EmitPrompt(Prompt(SshPromptKind::kSecret, "password: "));
  platform_.EmitStderr("me@box: Permission denied (publickey,password).");
  platform_.Exit(255);
  EXPECT_THAT(prompts_.cancelled(), ElementsAre(prompts_.shown()[0].id));
  EXPECT_EQ(status().state, SshTunnelState::kFailed);
  EXPECT_EQ(status().failure, SshFailure::kAuthFailed);
}

TEST_F(KoanSshTunnelTest, NoSshFails) {
  tunnel_->Start();
  platform_.Exit(SshTunnelPlatform::kLaunchFailedExitCode);
  EXPECT_EQ(status().state, SshTunnelState::kFailed);
  EXPECT_EQ(status().failure, SshFailure::kSshNotFound);
}

TEST_F(KoanSshTunnelTest, DestroyingTheTunnelStopsSsh) {
  tunnel_->Start();
  platform_.EmitPrompt(Prompt(SshPromptKind::kSecret, "password: "));
  observation_.Reset();
  tunnel_.reset();
  EXPECT_FALSE(platform_.launched()[0].alive);
  EXPECT_EQ(prompts_.cancelled().size(), 1u);
}

}  // namespace
}  // namespace koan::remote
