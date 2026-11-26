# A proxy per Space (#445): design and upstream audit

Date: 2026-11-26. Chromium 158 (pin 6449d9f9, pristine `git show HEAD:`). Builds on the containers per Space
(`space-containers.md`) and the container core (`containers.md`). Brave is MPL-2.0: it was read for behaviour only, and
none of its code is in Koan.

The user story: one Space browses "as if on my Linux box". In it, `localhost:3000` is the remote machine's dev server,
through an SSH SOCKS tunnel (`ssh -D`). Other Spaces stay direct.

Three PRs:
1. **This one:** the per-container proxy (setting, runtime state, the network hook, fail closed), a dev switch to set it.
2. An SSH tunnel manager (`src/browser/remote/`) that runs `ssh -D` for a container's remote host and reports its port.
3. The UI (a Space's profile gets a "Connect to" row).

## What upstream offers (158)

- **Where a partition gets its proxy.** `ChromeContentBrowserClient::ConfigureNetworkContextParams()` ->
  `ProfileNetworkContextService::ConfigureNetworkContextParamsInternal()` runs for every StoragePartition's network
  context, container partitions too, with `relative_partition_path` = `Storage/ext/<domain>/def` (content's
  `StoragePartitionImplMap::GetStoragePartitionPath()`, not public). It calls
  `proxy_config_monitor_->AddToNetworkContextParams()` (`proxy_config_monitor.cc:98`), which binds the context's
  `proxy_config_client_receiver` into the monitor's `RemoteSet` and sets `initial_proxy_config` (unless pending). One
  monitor per profile: system settings, the ProxySettings policy, the enterprise proxy service and extensions'
  `chrome.proxy` all reach every partition through it. There is no per-partition proxy hook.
- **Live updates.** `network::mojom::ProxyConfigClient::OnProxyConfigUpdated()` replaces a context's config; the
  resolution service applies it on its next resolve. `FlushProxyConfig()` is there for tests.
- **Loopback.** `ProxyHostMatchingRules::Matches()` falls back to `MatchesImplicitRules()` (localhost, `*.localhost`,
  127/8, [::1], IPv4-mapped loopback, link-local) when no explicit rule matches, so those always go direct. The
  `<-loopback>` rule (`AddRulesToSubtractImplicit()`, `SubtractImplicitBypassesRule`) answers kExclude for them first.
  PAC is worse: `ApplyPacBypassRules()` bypasses loopback even for a mandatory PAC, so a PAC can't do this.
- **SOCKS5 resolves remotely.** `SOCKS5ClientSocket` always sends `kEndPointDomain` with the host name
  (`socks5_client_socket.cc:346`); `SOCKSConnectJob` only resolves for SOCKS4. HTTP(S) proxies get the host in
  CONNECT / the absolute URL. So `localhost` is resolved by the remote end.
- **No DIRECT fallback.** A manual proxy list without a `direct://` entry never adds one: `ProxyList::Fallback()` drops
  bad chains and `ReconsiderProxyAfterError` fails once the list is empty; `DeprioritizeBadProxyChains()` only
  reorders. A connect failure to a SOCKS proxy is `ERR_PROXY_CONNECTION_FAILED` (`socks_connect_job.cc:166`).
- **An explicit fail mode.** `ProxyRules{type = PROXY_LIST, single_proxies = {}}` (not `EMPTY`, which is direct)
  resolves OK to an empty `ProxyInfo`. `HttpStreamFactory::JobController` (pages, subresources, service workers,
  WebSockets) and `ProxyResolvingClientSocket` (WebRTC's TCP sockets, `p2p/socket_manager.cc:436`) answer
  `ERR_NO_SUPPORTED_PROXIES` ("No proxies/direct to choose from"). `ProxyInfo::is_direct()` is false for it, so
  `NetworkServiceProxyDelegate` and IP Protection (which only act on direct results) leave it alone, and the
  preconnect manager's proxy lookup (`proxy_lookup_client_impl.cc:52`) reads it as "a proxy is in use" and skips its DNS
  pre-resolution. The mojo traits carry the type and the empty list as they are.
- **QUIC.** `HttpStreamFactory::Job` only speaks QUIC to QUIC proxies; with a SOCKS or HTTP proxy the QUIC job is
  `ERR_NO_SUPPORTED_PROXIES` and the TCP job is used. Nothing goes direct over UDP for page loads.

## What Zen and Brave do

- **Zen 1.24b** has no proxy per workspace (nothing in `src/zen`). Firefox does it per container only through
  extensions (`proxy.onRequest` with the request's `cookieStoreId`).
- **Brave's Tor windows** (`net/proxy_resolution/proxy_config_service_tor.cc`, a `chromium_src` override of
  `proxy_config_monitor.cc` for the Tor profile): a profile-wide `ProxyConfigService` with one SOCKS5 server,
  `PROXY_LIST`, `AddRulesToSubtractImplicit()` and no DIRECT, the same rules as Koan's. While Tor's port is unknown it
  reports `CONFIG_PENDING`, which holds requests. Koan can't: pending can't be sent over `ProxyConfigClient` once a
  context has a config, so Koan's disconnected state is the empty list, which fails at once (an error page to reload).

## Design

**The setting** (`ProxySetting`, `koan_container_proxy.h`) is saved with the container in `koan.containers.user`:
`{"id", "name", "color", "proxy": {"server": "socks5://127.0.0.1:1080"}}`. Only `socks5://`, `http://` and `https://`
with an explicit scheme are valid (SOCKS4 resolves names locally; `direct://`, QUIC and chains are refused); anything
else reads as no proxy. PR 2 adds an SSH remote to the same dict.

**The runtime state** (`RuntimeProxy`): a server, or disconnected. Not saved. `EffectiveProxy(saved, runtime)`: no
setting is no proxy (the profile's settings); a runtime state counts only while there is a setting, so removing the
setting always takes the proxy away; else the runtime state, else the saved server.

**The config** (`ContainerProxyConfig()`): `PROXY_LIST` of the one server, `<-loopback>`, nothing else. Disconnected is
the same with an empty list.

**The hook** (one call, after the monitor's): `koan::containers::InterposeContainerProxy(profile_,
relative_partition_path, params)`. For a user container's path (`UserContainerFromPartitionPath()`), Koan takes the
monitor's receiver and initial config into a relay (`ContainerProxies::Relay`, `koan_container_proxies.cc`) and gives
the context a pipe from the relay. The relay passes upstream's updates on while the container has no proxy and sends
the container's config while it has one. So every user container is interposed, a proxy set later reaches an open
context, and while a container has a proxy nothing upstream (system settings, policy, `chrome.proxy`) can override it.
Every other partition is untouched.

**`ContainerProxies`** lives on the Profile as user data (not a KeyedService: nothing else depends on it, and agent
containers don't need a row in the service policy). It watches `koan.containers.user`, keeps the runtime states, and
pushes changes to the relays. It drops its pref registrar in `OnProfileWillBeDestroyed()`, before the prefs go. A
relay goes when its context's pipe closes (network service restart: the partition makes a new context, which is
interposed again).

API for PR 2 and 3 (`koan_container_proxies.h`):

```cpp
auto* proxies = koan::containers::ContainerProxies::For(profile);
proxies->SetProxySetting(container, ProxySetting{...});            // the UI; saved
proxies->SetRuntimeProxy(container, RuntimeProxy::LocalSocks5(port)); // tunnel up
proxies->SetRuntimeProxy(container, RuntimeProxy::Disconnected());   // tunnel down
proxies->SetRuntimeProxy(container, std::nullopt);                   // forget
proxies->ProxyFor(container);                                        // what it uses now
```

**Dev switch** (until the UI): `--koan-container-proxy="<profile name>=<value>"`, applied when the profile's first
container network context is made: a server is saved as the setting, `none` removes it, `disconnected` sets the runtime
state for that run.

## What else could leave the container's proxy

| Path | Decision |
|---|---|
| Pages, subresources, service workers, shared workers, downloads, WebSockets | The partition's network context: through the proxy. |
| QUIC | Not spoken to SOCKS/HTTP proxies; the TCP job goes through the proxy. |
| DNS prefetch (`<link rel=dns-prefetch>`), preconnect | The preconnect manager looks up the proxy first and skips DNS when there is one (disconnected included). Preconnects go through the stream factory, so the proxy. The loading predictor is off in containers (B10). |
| Prerender, NoStatePrefetch | Off in containers (C4, B10). |
| Extensions' `chrome.proxy` | Can't reach a container with a proxy: the relay ignores upstream while it has one. Without a proxy, the container follows the profile as before. `onProxyError` still hears about the container's proxy errors (the error pipe stays the monitor's). |
| ProxySettings policy, enterprise proxy | Same as extensions: a container's own proxy wins. Documented; Koan has no managed users. |
| WebRTC | **Not covered.** UDP (host and srflx candidates, media) goes direct whatever the proxy; only TCP sockets go through it. The fix is `webrtc_ip_handling_policy = disable_non_proxied_udp` for container tabs, but renderer preferences are per profile (`renderer_preferences_util::UpdateFromSystemSettings(prefs, profile)`, rewritten by `PrefWatcher`), so it needs another patch. Follow-up. |
| Omnibox | Typed URLs open in the Space's container (rule 6). Omnibox suggestions, the intranet "did you mean" probe and favicons for history use the profile's default partition, as for every container. |
| Disconnected | Every new request fails with `ERR_NO_SUPPORTED_PROXIES`; open connections through the old proxy stay until they close (the proxy is gone anyway when its tunnel is). A page already loaded keeps running, and its WebRTC UDP (above) is the one way out. |

## Tests

- **Unit** (`koan_containers_unittests`): `ContainerProxyTest` (canonical servers, setting round trip, effective proxy,
  every URL through the proxy, loopback too, disconnected is empty, the switch, partition paths),
  `ContainerProxyPathTest` (content's partition layout, an upstream guard), `UserContainersTest.ProxyIsReadValidated`,
  `UserContainersTest.SetProxySavesAndRemoves`.
- **Browser** (`koan_browsertests`, `KoanContainerProxyBrowserTest`): a context made before the setting switches live;
  the runtime state replaces the server and goes back; other containers and the default partition stay direct; a dead
  proxy is `ERR_PROXY_CONNECTION_FAILED` and disconnected is `ERR_NO_SUPPORTED_PROXIES`, while the same page loads
  directly elsewhere.
- **Mac manual:** `ssh -D 1080 linuxbox`, a Space with a profile named "Linux", run with
  `--koan-container-proxy="Linux=socks5://127.0.0.1:1080"`; `localhost:3000` in that Space is the box's dev server, in
  another Space the Mac's. Kill the ssh: the Space's pages fail, nothing loads from the Mac. Relaunch with
  `"Linux=disconnected"`: every page in that Space fails at once.
