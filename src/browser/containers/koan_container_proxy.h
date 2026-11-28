// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXY_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/values.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"

// A proxy per Space (#445, .agent/notes/upstream/space-proxy.md): a user
// container's tabs can go through a proxy of their own, e.g. an SSH SOCKS
// tunnel (`ssh -D`) to a remote machine, so that localhost:3000 in that Space
// is the remote machine's. Pure logic over //base and //net; the browser side
// is koan_container_proxies.h.
//
// Two layers:
// - The setting (ProxySetting), saved with the container in
//   koan.containers.user: what the user chose.
// - The runtime proxy (RuntimeProxy): what the container's network context
//   uses right now. For a fixed server it is that server. A setting whose
//   server is only known at runtime (an SSH tunnel's local port) gets it from
//   whoever runs the tunnel, through ContainerProxies::SetRuntimeProxy(), and
//   is disconnected until then.
namespace koan::containers {

// A user container's proxy setting, as saved in its koan.containers.user
// entry ("proxy": {"server": ...}).
struct ProxySetting {
  // A fixed proxy server, canonical (CanonicalProxyServer()).
  std::string server;

  friend bool operator==(const ProxySetting&, const ProxySetting&) = default;
};

// `text` as a canonical proxy server ("socks5://host:port",
// "http://host:port" or "https://host:port"), or nullopt. The scheme must be
// given. SOCKS4 is refused: it resolves the host name on this machine, so
// localhost would be this machine's and every other name would be looked up
// in its DNS. SOCKS5 and HTTP(S) proxies send the host name to the proxy.
// direct://, QUIC and proxy chains are refused too.
std::optional<std::string> CanonicalProxyServer(std::string_view text);

// The setting saved in `value` (a koan.containers.user entry's "proxy"), or
// nullopt for none or anything invalid.
std::optional<ProxySetting> ProxySettingFromValue(const base::Value* value);
base::DictValue ProxySettingToValue(const ProxySetting& setting);

// What a container's network context routes through right now: `server`, or
// with none, nothing at all (disconnected: every request fails, nothing goes
// direct).
struct RuntimeProxy {
  // A canonical proxy server (CanonicalProxyServer()).
  std::optional<std::string> server;

  static RuntimeProxy Through(std::string server);
  // A SOCKS5 proxy on this machine's loopback at `port`: an `ssh -D` tunnel.
  static RuntimeProxy LocalSocks5(uint16_t port);
  static RuntimeProxy Disconnected();

  bool connected() const { return server.has_value(); }

  friend bool operator==(const RuntimeProxy&, const RuntimeProxy&) = default;
};

// The proxy a container with the setting `saved` uses, given the runtime
// state `runtime` that ContainerProxies::SetRuntimeProxy() last set (nullopt
// for none): nullopt for a container without a proxy (its tabs use the
// profile's proxy settings, as every other tab does); else the runtime state
// if one is set, else the saved server. A runtime state without a setting
// counts for nothing, so removing the setting always takes the proxy away.
//
// `disconnected_for_run` is --koan-container-proxy=<name>=disconnected
// (koan_switches.h), a test of the fail mode: the container is disconnected
// for this run whatever its setting and runtime state, with no setting too.
// Nothing saves it, so the next run without the switch is as before.
std::optional<RuntimeProxy> EffectiveProxy(
    const std::optional<ProxySetting>& saved,
    const std::optional<RuntimeProxy>& runtime,
    bool disconnected_for_run);

// The proxy configuration a container's network context gets for `proxy`.
// Everything goes through the proxy, loopback and link-local addresses too
// (net bypasses those implicitly; <-loopback> takes that away). There is no
// DIRECT entry, so a proxy that can't be reached fails the request instead of
// going direct.
//
// Disconnected is a proxy list with no entry at all. net reads it as "no
// proxies and no DIRECT to choose from": the HTTP stream factory (pages,
// subresources, service workers, WebSockets), ProxyResolvingClientSocket
// (WebRTC's TCP sockets) fail with ERR_NO_SUPPORTED_PROXIES, and the
// preconnect manager takes a non-direct result as "a proxy is in use" and
// skips its DNS lookups. That is net's own fail mode, so it holds whatever
// listens on this machine's ports, which a proxy to a closed port would not.
net::ProxyConfigWithAnnotation ContainerProxyConfig(const RuntimeProxy& proxy);

// The user container whose storage partition's path, relative to the
// profile's, is `relative_path`: content puts a partition with domain D and
// no name at Storage/ext/D/def (StoragePartitionImplMap), and a user
// container's domain is "koanuser<id>" (PartitionConfigFor()). Nullopt for
// any other partition. ContainerProxyPathTest guards content's layout.
std::optional<ContainerId> UserContainerFromPartitionPath(
    const base::FilePath& relative_path);

// The --koan-container-proxy switch (koan_switches.h): "<container
// name>=<value>", where the value is a proxy server (saved as the
// container's setting), "none" (removes the setting) or "disconnected"
// (disconnected for this run, saved proxy or not, as while a tunnel is down:
// EffectiveProxy()'s `disconnected_for_run`).
struct ProxySwitch {
  std::string container_name;
  enum class Action { kSave, kRemove, kDisconnect } action = Action::kSave;
  // For kSave.
  std::optional<ProxySetting> setting;
};
std::optional<ProxySwitch> ParseProxySwitch(std::string_view value);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PROXY_H_
