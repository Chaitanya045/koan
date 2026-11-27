// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_SYSTEM_EVENTS_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_SYSTEM_EVENTS_H_

#include "base/memory/raw_ptr.h"
#include "base/power_monitor/power_observer.h"
#include "net/base/network_change_notifier.h"

namespace koan::remote {

class SshTunnel;

// Tells a tunnel about sleep, wake and network changes (base::PowerMonitor,
// net::NetworkChangeNotifier, both set up by the browser process). Create it
// on the tunnel's sequence, after the tunnel; destroy it before.
class SshTunnelSystemEvents
    : public base::PowerSuspendObserver,
      public net::NetworkChangeNotifier::NetworkChangeObserver {
 public:
  explicit SshTunnelSystemEvents(SshTunnel* tunnel);
  SshTunnelSystemEvents(const SshTunnelSystemEvents&) = delete;
  SshTunnelSystemEvents& operator=(const SshTunnelSystemEvents&) = delete;
  ~SshTunnelSystemEvents() override;

  // base::PowerSuspendObserver:
  void OnSuspend() override;
  void OnResume() override;

  // net::NetworkChangeNotifier::NetworkChangeObserver:
  void OnNetworkChanged(
      net::NetworkChangeNotifier::ConnectionType type) override;

 private:
  const raw_ptr<SshTunnel> tunnel_;
};

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_TUNNEL_SYSTEM_EVENTS_H_
