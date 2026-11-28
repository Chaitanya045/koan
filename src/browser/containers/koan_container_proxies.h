// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXIES_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXIES_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_container_proxy.h"

namespace network::mojom {
class NetworkContextParams;
}  // namespace network::mojom

namespace koan::containers {

// The proxies of a profile's user containers (#445, koan_container_proxy.h):
// it sits between upstream's ProxyConfigMonitor and each user container's
// network context (InterposeContainerProxy(), koan_container_proxy_hook.h)
// and sends the context the container's own proxy while it has one, the
// profile's proxy settings otherwise. A change (a setting, a runtime state)
// reaches the contexts at once through ProxyConfigClient, so open tabs
// switch without a restart. While a container has a proxy, upstream's
// configuration (system settings, the proxy policy, an extension's
// chrome.proxy) doesn't reach it.
//
// Lives on the profile (user data), created on first use. Not a
// KeyedService: it has nothing to offer agent containers and no other
// service depends on it.
class ContainerProxies : public base::SupportsUserData::Data,
                         public ProfileObserver {
 public:
  // `profile`'s, created on first use.
  static ContainerProxies* For(Profile* profile);
  // `profile`'s if it was made (its first user container network context
  // makes it), else null. For a reader that mustn't make it: making it applies
  // --koan-container-proxy, which writes koan.containers.user.
  static ContainerProxies* Find(Profile* profile);

  explicit ContainerProxies(Profile* profile);
  ContainerProxies(const ContainerProxies&) = delete;
  ContainerProxies& operator=(const ContainerProxies&) = delete;
  ~ContainerProxies() override;

  // Saves `setting` (nullopt: none) as the proxy of `container`, one of the
  // user's containers (koan.containers.user). False if there is no such
  // container. Its tabs switch at once.
  bool SetProxySetting(const ContainerId& container,
                       const std::optional<ProxySetting>& setting);

  // The runtime state of `container`'s proxy (nullopt: none, so the saved
  // server is used), for a proxy whose server is only known at runtime: the
  // SSH tunnel manager sets RuntimeProxy::LocalSocks5(port) once its tunnel
  // is up and RuntimeProxy::Disconnected() while it is down. Not saved. It
  // only counts while the container has a proxy setting (EffectiveProxy()).
  // Its tabs switch at once.
  void SetRuntimeProxy(const ContainerId& container,
                       std::optional<RuntimeProxy> proxy);

  // The proxy `container`'s tabs go through now (EffectiveProxy()), nullopt
  // for the profile's proxy settings.
  std::optional<RuntimeProxy> ProxyFor(const ContainerId& container) const;

  // Waits until every network context has taken the proxy it was last sent.
  void FlushForTesting();

  // InterposeContainerProxy() for `container`'s partition.
  void Interpose(const ContainerId& container,
                 network::mojom::NetworkContextParams* params);

 private:
  class Relay;

  // --koan-container-proxy (koan_switches.h), once per profile.
  void ApplySwitch();
  // Sends every network context its container's proxy, where it changed.
  void Update();
  void OnRelayDisconnected(Relay* relay);

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  raw_ptr<Profile> profile_;
  PrefChangeRegistrar registrar_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  // By ContainerId::id.
  std::map<std::string, RuntimeProxy> runtime_;
  // The containers --koan-container-proxy disconnected for this run, by
  // ContainerId::id (EffectiveProxy()'s `disconnected_for_run`).
  std::set<std::string> disconnected_for_run_;
  // One per user container network context (a partition gets a new one when
  // the network service restarts).
  std::vector<std::unique_ptr<Relay>> relays_;
};

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXIES_H_
