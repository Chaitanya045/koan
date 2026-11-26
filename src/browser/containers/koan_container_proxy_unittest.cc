// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_proxy.h"

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/values.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_host_matching_rules.h"
#include "net/proxy_resolution/proxy_info.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::containers {
namespace {

const ContainerId kLinux{.kind = ContainerKind::kUser, .id = "slinux"};

// What a request for `url` goes through with `proxy`.
net::ProxyInfo Resolve(const RuntimeProxy& proxy, const char* url) {
  net::ProxyInfo info;
  ContainerProxyConfig(proxy).value().proxy_rules().Apply(GURL(url), &info);
  return info;
}

TEST(ContainerProxyTest, CanonicalServerNeedsASchemeThatSendsTheHostName) {
  EXPECT_EQ("socks5://127.0.0.1:1080",
            CanonicalProxyServer(" socks5://127.0.0.1:1080 "));
  // "socks" is SOCKS5 in a proxy URI.
  EXPECT_EQ("socks5://127.0.0.1:1080",
            CanonicalProxyServer("socks://127.0.0.1:1080"));
  EXPECT_EQ("socks5://localhost:1080",
            CanonicalProxyServer("socks5://localhost"));
  EXPECT_EQ("http://proxy.example:8080",
            CanonicalProxyServer("HTTP://proxy.example:8080"));
  EXPECT_EQ("https://proxy.example:443",
            CanonicalProxyServer("https://proxy.example"));
  EXPECT_EQ("socks5://[::1]:1080", CanonicalProxyServer("socks5://[::1]:1080"));

  for (const char* text :
       {"", "127.0.0.1:1080", "socks4://127.0.0.1:1080", "direct://",
        "quic://proxy.example:443", "ftp://proxy.example:21",
        "socks5://user:pass@127.0.0.1:1080", "socks5://127.0.0.1:", "socks5://",
        "socks5://127.0.0.1:99999", "garbage"}) {
    EXPECT_FALSE(CanonicalProxyServer(text)) << text;
  }
}

TEST(ContainerProxyTest, SettingRoundTripsAndInvalidIsNone) {
  const ProxySetting setting{.server = "socks5://127.0.0.1:1080"};
  const base::Value value(ProxySettingToValue(setting));
  EXPECT_EQ(setting, ProxySettingFromValue(&value));

  // Read canonical.
  const base::Value loose(base::DictValue().Set("server", "SOCKS5://h:1"));
  EXPECT_EQ((ProxySetting{.server = "socks5://h:1"}),
            ProxySettingFromValue(&loose));

  EXPECT_FALSE(ProxySettingFromValue(nullptr));
  const base::Value not_a_dict("socks5://127.0.0.1:1080");
  EXPECT_FALSE(ProxySettingFromValue(&not_a_dict));
  const base::Value no_server(base::DictValue().Set("remote", "linuxbox"));
  EXPECT_FALSE(ProxySettingFromValue(&no_server));
  const base::Value bad_server(
      base::DictValue().Set("server", "socks4://127.0.0.1:1080"));
  EXPECT_FALSE(ProxySettingFromValue(&bad_server));
  const base::Value not_a_string(base::DictValue().Set("server", 1080));
  EXPECT_FALSE(ProxySettingFromValue(&not_a_string));
}

TEST(ContainerProxyTest, RuntimeStateCountsOnlyWithASetting) {
  const ProxySetting setting{.server = "socks5://127.0.0.1:1080"};
  const RuntimeProxy tunnel = RuntimeProxy::LocalSocks5(40123);
  EXPECT_EQ("socks5://127.0.0.1:40123", tunnel.server);
  EXPECT_TRUE(tunnel.connected());
  EXPECT_FALSE(RuntimeProxy::Disconnected().connected());

  // No setting: the profile's proxy settings, whatever the runtime says.
  EXPECT_FALSE(EffectiveProxy(std::nullopt, std::nullopt));
  EXPECT_FALSE(EffectiveProxy(std::nullopt, tunnel));
  EXPECT_FALSE(EffectiveProxy(std::nullopt, RuntimeProxy::Disconnected()));
  // A setting: its server, unless a runtime state is set.
  EXPECT_EQ(RuntimeProxy::Through(setting.server),
            EffectiveProxy(setting, std::nullopt));
  EXPECT_EQ(tunnel, EffectiveProxy(setting, tunnel));
  EXPECT_EQ(RuntimeProxy::Disconnected(),
            EffectiveProxy(setting, RuntimeProxy::Disconnected()));
}

// Upstream guard: net bypasses the proxy for loopback implicitly, which is
// why ContainerProxyConfig() needs <-loopback>. If this ever fails, net
// changed its implicit rules: re-check ContainerProxyConfig().
TEST(ContainerProxyTest, NetBypassesLoopbackUnlessTold) {
  EXPECT_TRUE(net::ProxyHostMatchingRules::MatchesImplicitRules(
      GURL("http://localhost:3000")));
  EXPECT_TRUE(net::ProxyHostMatchingRules::MatchesImplicitRules(
      GURL("http://127.0.0.1:3000")));
  net::ProxyConfig::ProxyRules plain;
  plain.ParseFromString("socks5://127.0.0.1:1080");
  net::ProxyInfo info;
  plain.Apply(GURL("http://localhost:3000"), &info);
  EXPECT_TRUE(info.is_direct());
}

TEST(ContainerProxyTest, EverythingGoesThroughTheProxyLoopbackToo) {
  const RuntimeProxy proxy = RuntimeProxy::Through("socks5://127.0.0.1:1080");
  for (const char* url :
       {"http://localhost:3000/", "http://127.0.0.1:3000/",
        "http://[::1]:3000/", "http://dev.localhost/", "http://169.254.1.1/",
        "https://example.com/", "ws://localhost:3000/socket",
        "wss://example.com/socket"}) {
    const net::ProxyInfo info = Resolve(proxy, url);
    EXPECT_FALSE(info.is_direct()) << url;
    EXPECT_FALSE(info.did_bypass_proxy()) << url;
    // One proxy and no DIRECT to fall back to.
    EXPECT_EQ("SOCKS5 127.0.0.1:1080", info.proxy_list().ToPacString()) << url;
  }
  const net::ProxyConfig& config = ContainerProxyConfig(proxy).value();
  EXPECT_FALSE(config.HasAutomaticSettings());
  EXPECT_FALSE(config.pac_mandatory());
}

TEST(ContainerProxyTest, DisconnectedHasNothingToConnectThrough) {
  for (const char* url : {"http://localhost:3000/", "https://example.com/",
                          "ws://localhost:3000/socket"}) {
    const net::ProxyInfo info = Resolve(RuntimeProxy::Disconnected(), url);
    // The stream factory's "no proxies/direct to choose from"
    // (ERR_NO_SUPPORTED_PROXIES).
    EXPECT_TRUE(info.is_empty()) << url;
    EXPECT_FALSE(info.is_direct()) << url;
  }
  // Not the empty rules, which mean direct.
  EXPECT_FALSE(ContainerProxyConfig(RuntimeProxy::Disconnected())
                   .value()
                   .proxy_rules()
                   .empty());
}

TEST(ContainerProxyTest, ParsesTheSwitch) {
  std::optional<ProxySwitch> parsed =
      ParseProxySwitch("Linux box=socks5://127.0.0.1:1080");
  ASSERT_TRUE(parsed);
  EXPECT_EQ("Linux box", parsed->container_name);
  EXPECT_EQ(ProxySwitch::Action::kSave, parsed->action);
  EXPECT_EQ((ProxySetting{.server = "socks5://127.0.0.1:1080"}),
            parsed->setting);

  parsed = ParseProxySwitch("a=b=none");
  ASSERT_TRUE(parsed);
  EXPECT_EQ("a=b", parsed->container_name);
  EXPECT_EQ(ProxySwitch::Action::kRemove, parsed->action);

  parsed = ParseProxySwitch("Linux=disconnected");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(ProxySwitch::Action::kDisconnect, parsed->action);

  for (const char* value : {"", "Linux", "=socks5://127.0.0.1:1080",
                            "Linux=", "Linux=socks4://127.0.0.1:1080"}) {
    EXPECT_FALSE(ParseProxySwitch(value)) << value;
  }
}

TEST(ContainerProxyTest, OnlyUserContainerPathsAreContainers) {
  EXPECT_EQ(kLinux, UserContainerFromPartitionPath(
                        base::FilePath(FILE_PATH_LITERAL("Storage"))
                            .Append(FILE_PATH_LITERAL("ext"))
                            .Append(FILE_PATH_LITERAL("koanuserslinux"))
                            .Append(FILE_PATH_LITERAL("def"))));
  for (const base::FilePath::CharType* path : {
           FILE_PATH_LITERAL(""),
           FILE_PATH_LITERAL("Storage/ext/koanagentslinux/def"),
           FILE_PATH_LITERAL("Storage/ext/koanuserslinux/0123456789ab"),
           FILE_PATH_LITERAL("Storage/ext/koanuser/def"),
           FILE_PATH_LITERAL("Storage/ext/koanuserSLinux/def"),
           FILE_PATH_LITERAL(
               "Storage/ext/abcdefghijklmnopabcdefghijklmnop/def"),
           FILE_PATH_LITERAL("Storage/koanuserslinux/def"),
           FILE_PATH_LITERAL("x/Storage/ext/koanuserslinux/def"),
       }) {
    EXPECT_FALSE(UserContainerFromPartitionPath(base::FilePath(path))) << path;
  }
}

class ContainerProxyPathTest : public testing::Test {
 protected:
  // `config`'s partition's path relative to the profile's, as content passes
  // it to ConfigureNetworkContextParams().
  base::FilePath RelativePathOf(const content::StoragePartitionConfig& config) {
    content::StoragePartition* partition =
        context_.GetStoragePartition(config, /*can_create=*/true);
    base::FilePath relative;
    if (partition->GetPath() != context_.GetPath()) {
      EXPECT_TRUE(context_.GetPath().AppendRelativePath(partition->GetPath(),
                                                        &relative));
    }
    return relative;
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext context_;
};

// Upstream guard: content still lays out a user container's partition where
// UserContainerFromPartitionPath() looks for it. If this fails, content moved
// partitions and containers would lose their proxy.
TEST_F(ContainerProxyPathTest, ContentPutsAContainerWhereTheHookLooks) {
  EXPECT_EQ(kLinux, UserContainerFromPartitionPath(
                        RelativePathOf(PartitionConfigFor(&context_, kLinux))));
  EXPECT_FALSE(UserContainerFromPartitionPath(RelativePathOf(PartitionConfigFor(
      &context_, {.kind = ContainerKind::kAgent, .id = "slinux"}))));
  EXPECT_FALSE(UserContainerFromPartitionPath(RelativePathOf(
      content::StoragePartitionConfig::CreateDefault(&context_))));
}

}  // namespace
}  // namespace koan::containers
