// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_tunnel_system_events.h"

#include "base/test/task_environment.h"
#include "koan/browser/remote/koan_ssh_test_fakes.h"
#include "koan/browser/remote/koan_ssh_tunnel.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

TEST(KoanSshTunnelSystemEventsTest, ForwardsSleepWakeAndNewNetworks) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakeSshTunnelPlatform platform;
  FakeSshPromptDelegate prompts;
  SshDestination destination;
  destination.host = "box";
  SshTunnel tunnel(destination, &platform, &prompts);
  SshTunnelSystemEvents events(&tunnel);

  tunnel.Start();
  platform.set_socks_ready(true);
  task_environment.FastForwardBy(SshTunnel::kProbeInterval);
  ASSERT_EQ(tunnel.status().state, SshTunnelState::kConnected);

  // The old network going away isn't a reason to reconnect yet...
  events.OnNetworkChanged(net::NetworkChangeNotifier::CONNECTION_NONE);
  EXPECT_EQ(platform.launched().size(), 1u);
  // ...the new one coming up is.
  events.OnNetworkChanged(net::NetworkChangeNotifier::CONNECTION_WIFI);
  EXPECT_EQ(platform.launched().size(), 2u);

  platform.Exit(255);
  ASSERT_EQ(tunnel.status().state, SshTunnelState::kRetrying);
  events.OnSuspend();
  task_environment.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(platform.launched().size(), 2u);
  events.OnResume();
  EXPECT_EQ(platform.launched().size(), 3u);
}

}  // namespace
}  // namespace koan::remote
