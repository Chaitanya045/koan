// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_proxies.h"

#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "koan/browser/containers/koan_container_proxy_hook.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_switches.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/proxy_config_with_annotation.mojom.h"

namespace koan::containers {

namespace {

const char kUserDataKey[] = "koan.container_proxies";

}  // namespace

// Between upstream's ProxyConfigMonitor and one user container's network
// context: it takes the monitor's updates and passes them on while the
// container has no proxy of its own, and sends the container's proxy while it
// has one.
class ContainerProxies::Relay : public network::mojom::ProxyConfigClient {
 public:
  Relay(ContainerId container,
        mojo::PendingReceiver<network::mojom::ProxyConfigClient> upstream,
        std::optional<net::ProxyConfigWithAnnotation> upstream_config,
        mojo::PendingRemote<network::mojom::ProxyConfigClient> network,
        std::optional<RuntimeProxy> proxy)
      : container_(std::move(container)),
        upstream_config_(std::move(upstream_config)),
        network_(std::move(network)),
        proxy_(std::move(proxy)) {
    // The monitor may not have given one (it always does in 158); then the
    // context just never hears from upstream.
    if (upstream) {
      upstream_.Bind(std::move(upstream));
    }
  }
  Relay(const Relay&) = delete;
  Relay& operator=(const Relay&) = delete;
  ~Relay() override = default;

  const ContainerId& container() const { return container_; }

  // Runs `handler` once the network context is gone.
  void SetDisconnectHandler(base::OnceClosure handler) {
    network_.set_disconnect_handler(std::move(handler));
  }

  // The container's proxy is now `proxy` (nullopt: the profile's settings).
  void SetProxy(const std::optional<RuntimeProxy>& proxy) {
    if (proxy == proxy_) {
      return;
    }
    proxy_ = proxy;
    if (proxy_) {
      network_->OnProxyConfigUpdated(ContainerProxyConfig(*proxy_));
    } else if (upstream_config_) {
      network_->OnProxyConfigUpdated(*upstream_config_);
    }
    // Else upstream's config is still pending: the context keeps the
    // container's last proxy (never direct) until upstream's first update.
  }

  void FlushForTesting() { network_.FlushForTesting(); }

  // network::mojom::ProxyConfigClient, from upstream:
  void OnProxyConfigUpdated(
      const net::ProxyConfigWithAnnotation& proxy_config) override {
    upstream_config_ = proxy_config;
    if (!proxy_) {
      network_->OnProxyConfigUpdated(proxy_config);
    }
  }
  void FlushProxyConfig(FlushProxyConfigCallback callback) override {
    network_->FlushProxyConfig(std::move(callback));
  }

 private:
  const ContainerId container_;
  mojo::Receiver<network::mojom::ProxyConfigClient> upstream_{this};
  // Upstream's latest, nullopt while it has none (CONFIG_PENDING).
  std::optional<net::ProxyConfigWithAnnotation> upstream_config_;
  mojo::Remote<network::mojom::ProxyConfigClient> network_;
  // What the context has now: the container's proxy, or upstream's (nullopt).
  std::optional<RuntimeProxy> proxy_;
};

// static
ContainerProxies* ContainerProxies::For(Profile* profile) {
  auto* proxies =
      static_cast<ContainerProxies*>(profile->GetUserData(kUserDataKey));
  if (!proxies) {
    auto owned = std::make_unique<ContainerProxies>(profile);
    proxies = owned.get();
    profile->SetUserData(kUserDataKey, std::move(owned));
  }
  return proxies;
}

ContainerProxies::ContainerProxies(Profile* profile) : profile_(profile) {
  ApplySwitch();
  registrar_.Init(profile_->GetPrefs());
  registrar_.Add(
      prefs::kUserContainers,
      base::BindRepeating(&ContainerProxies::Update, base::Unretained(this)));
  profile_observation_.Observe(profile_);
}

ContainerProxies::~ContainerProxies() = default;

bool ContainerProxies::SetProxySetting(
    const ContainerId& container,
    const std::optional<ProxySetting>& setting) {
  if (!profile_) {
    return false;
  }
  // The pref observer sends the change.
  ScopedListPrefUpdate update(profile_->GetPrefs(), prefs::kUserContainers);
  return SetUserContainerProxy(update.Get(), container, setting);
}

void ContainerProxies::SetRuntimeProxy(const ContainerId& container,
                                       std::optional<RuntimeProxy> proxy) {
  if (proxy) {
    runtime_.insert_or_assign(container.id, std::move(*proxy));
  } else {
    runtime_.erase(container.id);
  }
  Update();
}

std::optional<RuntimeProxy> ContainerProxies::ProxyFor(
    const ContainerId& container) const {
  if (!profile_ || container.kind != ContainerKind::kUser) {
    return std::nullopt;
  }
  std::optional<ProxySetting> saved;
  for (const UserContainer& user_container : ParseUserContainers(
           profile_->GetPrefs()->GetList(prefs::kUserContainers))) {
    if (user_container.id == container) {
      saved = user_container.proxy;
      break;
    }
  }
  std::optional<RuntimeProxy> runtime;
  if (auto it = runtime_.find(container.id); it != runtime_.end()) {
    runtime = it->second;
  }
  return EffectiveProxy(saved, runtime,
                        disconnected_for_run_.contains(container.id));
}

void ContainerProxies::FlushForTesting() {
  for (const std::unique_ptr<Relay>& relay : relays_) {
    relay->FlushForTesting();
  }
}

void ContainerProxies::Interpose(const ContainerId& container,
                                 network::mojom::NetworkContextParams* params) {
  // Upstream's ends go to the relay; the context gets a pipe from the relay.
  mojo::PendingReceiver<network::mojom::ProxyConfigClient> upstream =
      std::move(params->proxy_config_client_receiver);
  std::optional<net::ProxyConfigWithAnnotation> upstream_config =
      std::move(params->initial_proxy_config);
  mojo::PendingRemote<network::mojom::ProxyConfigClient> network;
  params->proxy_config_client_receiver =
      network.InitWithNewPipeAndPassReceiver();

  std::optional<RuntimeProxy> proxy = ProxyFor(container);
  params->initial_proxy_config =
      proxy ? std::make_optional(ContainerProxyConfig(*proxy))
            : upstream_config;

  auto relay = std::make_unique<Relay>(container, std::move(upstream),
                                       std::move(upstream_config),
                                       std::move(network), std::move(proxy));
  Relay* raw = relay.get();
  relays_.push_back(std::move(relay));
  // The context is gone (its partition's network service restarted, or the
  // profile is going away): so is its relay. Unretained: the relay owns the
  // remote whose handler this is, and this owns the relay.
  raw->SetDisconnectHandler(base::BindOnce(
      &ContainerProxies::OnRelayDisconnected, base::Unretained(this), raw));
}

void ContainerProxies::ApplySwitch() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kContainerProxy)) {
    return;
  }
  const std::string value =
      command_line.GetSwitchValueUTF8(switches::kContainerProxy);
  const std::optional<ProxySwitch> parsed = ParseProxySwitch(value);
  if (!parsed) {
    LOG(ERROR) << "--" << switches::kContainerProxy << "=" << value
               << ": expected <container name>=<socks5://host:port, "
                  "http(s)://host:port, none or disconnected>";
    return;
  }
  for (const UserContainer& container : ParseUserContainers(
           profile_->GetPrefs()->GetList(prefs::kUserContainers))) {
    if (container.name != parsed->container_name) {
      continue;
    }
    switch (parsed->action) {
      case ProxySwitch::Action::kSave:
        SetProxySetting(container.id, parsed->setting);
        break;
      case ProxySwitch::Action::kRemove:
        SetProxySetting(container.id, std::nullopt);
        break;
      case ProxySwitch::Action::kDisconnect:
        // With or without a saved proxy: the switch promises a failing
        // container for this run.
        disconnected_for_run_.insert(container.id.id);
        break;
    }
    return;
  }
  LOG(ERROR) << "--" << switches::kContainerProxy << ": no container named "
             << parsed->container_name;
}

void ContainerProxies::Update() {
  for (const std::unique_ptr<Relay>& relay : relays_) {
    relay->SetProxy(ProxyFor(relay->container()));
  }
}

void ContainerProxies::OnRelayDisconnected(Relay* relay) {
  std::erase_if(relays_, [relay](const std::unique_ptr<Relay>& entry) {
    return entry.get() == relay;
  });
}

void ContainerProxies::OnProfileWillBeDestroyed(Profile* profile) {
  // The prefs go before the profile's user data.
  registrar_.RemoveAll();
  profile_observation_.Reset();
  relays_.clear();
  profile_ = nullptr;
}

void InterposeContainerProxy(Profile* profile,
                             const base::FilePath& relative_partition_path,
                             network::mojom::NetworkContextParams* params) {
  if (!profile || !params) {
    return;
  }
  if (std::optional<ContainerId> container =
          UserContainerFromPartitionPath(relative_partition_path)) {
    ContainerProxies::For(profile)->Interpose(*container, params);
  }
}

}  // namespace koan::containers
