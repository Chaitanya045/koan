// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_proxy.h"

#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace koan::containers {

namespace {

constexpr char kServerKey[] = "server";

constexpr char kRemoveValue[] = "none";
constexpr char kDisconnectValue[] = "disconnected";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("koan_container_proxy", R"(
      semantics {
        sender: "Koan proxy per Space"
        description:
          "The proxy server the user chose for one of their containers (the "
          "profile of a Space). Every request of that container's tabs goes "
          "through it, e.g. an SSH tunnel to the user's own machine."
        trigger: "Any request from a tab in that container."
        data: "Whatever the tab sends; the proxy sees the destination host."
        destination: OTHER
        destination_other: "The proxy server the user set for the container."
      }
      policy {
        cookies_allowed: YES
        cookies_store: "The container's own cookie store."
        setting: "Set per container by the user; none by default."
        policy_exception_justification: "Not implemented."
      })");

// content's layout of a named-domain partition: Storage/ext/<domain>/def
// (StoragePartitionImplMap::GetStoragePartitionPath()).
constexpr base::FilePath::CharType kStorageDirname[] =
    FILE_PATH_LITERAL("Storage");
constexpr base::FilePath::CharType kExtDirname[] = FILE_PATH_LITERAL("ext");
constexpr base::FilePath::CharType kDefaultPartitionDirname[] =
    FILE_PATH_LITERAL("def");

}  // namespace

std::optional<std::string> CanonicalProxyServer(std::string_view text) {
  // No default scheme: "host:port" alone is refused.
  const net::ProxyServer server =
      net::ProxyUriToProxyServer(text, net::ProxyServer::SCHEME_INVALID);
  if (!server.is_valid()) {
    return std::nullopt;
  }
  switch (server.scheme()) {
    case net::ProxyServer::SCHEME_SOCKS5:
    case net::ProxyServer::SCHEME_HTTPS:
      return net::ProxyServerToProxyUri(server);
    case net::ProxyServer::SCHEME_HTTP:
      // ProxyServerToProxyUri() leaves off "http://", its default scheme.
      return base::StrCat({"http://", net::ProxyServerToProxyUri(server)});
    default:
      return std::nullopt;
  }
}

std::optional<ProxySetting> ProxySettingFromValue(const base::Value* value) {
  const base::DictValue* dict = value ? value->GetIfDict() : nullptr;
  const std::string* server = dict ? dict->FindString(kServerKey) : nullptr;
  if (!server) {
    return std::nullopt;
  }
  std::optional<std::string> canonical = CanonicalProxyServer(*server);
  if (!canonical) {
    return std::nullopt;
  }
  return ProxySetting{.server = std::move(*canonical)};
}

base::DictValue ProxySettingToValue(const ProxySetting& setting) {
  return base::DictValue().Set(kServerKey, setting.server);
}

// static
RuntimeProxy RuntimeProxy::Through(std::string server) {
  return RuntimeProxy{.server = std::move(server)};
}

// static
RuntimeProxy RuntimeProxy::LocalSocks5(uint16_t port) {
  return Through(
      base::StrCat({"socks5://127.0.0.1:", base::NumberToString(port)}));
}

// static
RuntimeProxy RuntimeProxy::Disconnected() {
  return RuntimeProxy();
}

std::optional<RuntimeProxy> EffectiveProxy(
    const std::optional<ProxySetting>& saved,
    const std::optional<RuntimeProxy>& runtime) {
  if (!saved) {
    return std::nullopt;
  }
  if (runtime) {
    return runtime;
  }
  return RuntimeProxy::Through(saved->server);
}

net::ProxyConfigWithAnnotation ContainerProxyConfig(const RuntimeProxy& proxy) {
  net::ProxyConfig config;
  net::ProxyConfig::ProxyRules& rules = config.proxy_rules();
  rules.type = net::ProxyConfig::ProxyRules::Type::PROXY_LIST;
  if (proxy.server) {
    // Canonical, so it parses; an invalid server would be dropped by
    // AddProxyServer() and leave the list empty, i.e. fail closed too.
    rules.single_proxies.AddProxyServer(net::ProxyUriToProxyServer(
        *proxy.server, net::ProxyServer::SCHEME_INVALID));
  }
  rules.bypass_rules.AddRulesToSubtractImplicit();
  return net::ProxyConfigWithAnnotation(config, kTrafficAnnotation);
}

std::optional<ContainerId> UserContainerFromPartitionPath(
    const base::FilePath& relative_path) {
  const std::vector<base::FilePath::StringType> parts =
      relative_path.GetComponents();
  if (parts.size() != 4 || parts.front() != kStorageDirname ||
      parts[1] != kExtDirname || parts.back() != kDefaultPartitionDirname) {
    return std::nullopt;
  }
  const std::string domain = base::FilePath(parts[2]).MaybeAsASCII();
  if (!domain.starts_with(kUserPartitionDomainPrefix)) {
    return std::nullopt;
  }
  ContainerId container{
      .kind = ContainerKind::kUser,
      .id = domain.substr(std::string_view(kUserPartitionDomainPrefix).size())};
  if (!IsValidContainerId(container.id)) {
    return std::nullopt;
  }
  return container;
}

std::optional<ProxySwitch> ParseProxySwitch(std::string_view value) {
  // Proxy servers have no '=', names might.
  const size_t equals = value.rfind('=');
  if (equals == std::string_view::npos || equals == 0) {
    return std::nullopt;
  }
  ProxySwitch parsed{.container_name = std::string(value.substr(0, equals))};
  const std::string_view action = value.substr(equals + 1);
  if (action == kRemoveValue) {
    parsed.action = ProxySwitch::Action::kRemove;
  } else if (action == kDisconnectValue) {
    parsed.action = ProxySwitch::Action::kDisconnect;
  } else if (std::optional<std::string> server = CanonicalProxyServer(action)) {
    parsed.setting = ProxySetting{.server = std::move(*server)};
  } else {
    return std::nullopt;
  }
  return parsed;
}

}  // namespace koan::containers
