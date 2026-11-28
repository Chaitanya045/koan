// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXY_HOOK_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXY_HOOK_H_

class Profile;

namespace base {
class FilePath;
}  // namespace base

namespace network::mojom {
class NetworkContextParams;
}  // namespace network::mojom

// A proxy per Space (#445): the hook in
// chrome/browser/net/profile_network_context_service.cc. Header-only target
// with no dependencies, so the upstream target can depend on it without a
// cycle; the definition is in koan_container_proxies.cc (:proxies, linked
// through :impl and //koan/browser/ui/views:impl).
namespace koan::containers {

// ProfileNetworkContextService::ConfigureNetworkContextParamsInternal(),
// right after upstream's ProxyConfigMonitor has given `params` the profile's
// proxy configuration: for a user container's partition
// (`relative_partition_path`), puts ContainerProxies between the monitor and
// the network context, so that the container's own proxy, when it has one,
// replaces the profile's, now and on every later change. Does nothing for
// any other partition.
void InterposeContainerProxy(Profile* profile,
                             const base::FilePath& relative_partition_path,
                             network::mojom::NetworkContextParams* params);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXY_HOOK_H_
