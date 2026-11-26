// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_tunnel_system_events.h"

#include "base/power_monitor/power_monitor.h"
#include "koan/browser/remote/koan_ssh_tunnel.h"

namespace koan::remote {

SshTunnelSystemEvents::SshTunnelSystemEvents(SshTunnel* tunnel)
    : tunnel_(tunnel) {
  if (base::PowerMonitor::GetInstance()
          ->AddPowerSuspendObserverAndReturnSuspendedState(this)) {
    tunnel_->OnSuspend();
  }
  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
}

SshTunnelSystemEvents::~SshTunnelSystemEvents() {
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
  base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
}

void SshTunnelSystemEvents::OnSuspend() {
  tunnel_->OnSuspend();
}

void SshTunnelSystemEvents::OnResume() {
  tunnel_->OnResume();
}

void SshTunnelSystemEvents::OnNetworkChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  // A change comes as CONNECTION_NONE, then the new network's type
  // (network_change_notifier.h): act once the new one is up.
  if (type != net::NetworkChangeNotifier::CONNECTION_NONE) {
    tunnel_->OnNetworkChanged();
  }
}

}  // namespace koan::remote
