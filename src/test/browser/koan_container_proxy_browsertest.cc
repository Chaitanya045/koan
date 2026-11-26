// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A proxy per Space (#445, .agent/notes/upstream/space-proxy.md): a user
// container's network context goes through the container's proxy, switches
// live when the setting or the runtime state changes, and fails instead of
// going direct while disconnected. The default partition is never touched.

#include <memory>
#include <optional>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_container_proxies.h"
#include "koan/browser/containers/koan_container_proxy.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/base/net_errors.h"
#include "net/base/network_anonymization_key.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/proxy_lookup_client.mojom.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace koan {

namespace {

using containers::ContainerId;
using containers::ContainerProxies;
using containers::ProxySetting;
using containers::RuntimeProxy;

constexpr char kLocalDevServer[] = "http://localhost:3000/";
constexpr char kTunnel[] = "socks5://127.0.0.1:1080";

// What a request for `url` goes through in `partition`: a PAC string
// ("DIRECT", "SOCKS5 127.0.0.1:1080"), "" for nothing at all, or "error".
class ProxyLookup : public network::mojom::ProxyLookupClient {
 public:
  static std::string Run(content::StoragePartition* partition,
                         const GURL& url) {
    ProxyLookup lookup;
    partition->GetNetworkContext()->LookUpProxyForURL(
        url, net::NetworkAnonymizationKey(),
        lookup.receiver_.BindNewPipeAndPassRemote());
    return lookup.result_.Get();
  }

  // network::mojom::ProxyLookupClient:
  void OnProxyLookupComplete(
      int32_t net_error,
      const std::optional<net::ProxyInfo>& proxy_info) override {
    result_.SetValue(net_error != net::OK || !proxy_info
                         ? "error"
                         : proxy_info->proxy_list().ToPacString());
  }

 private:
  mojo::Receiver<network::mojom::ProxyLookupClient> receiver_{this};
  base::test::TestFuture<std::string> result_;
};

class KoanContainerProxyBrowserTest : public InProcessBrowserTest {
 protected:
  Profile* profile() { return browser()->GetProfile(); }
  ContainerProxies* proxies() { return ContainerProxies::For(profile()); }

  ContainerId AddContainer(const std::u16string& name) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    return containers::AddUserContainer(update.Get(), name)->id;
  }

  content::StoragePartition* PartitionOf(const ContainerId& container) {
    return profile()->GetStoragePartition(
        containers::PartitionConfigFor(profile(), container));
  }

  // What `url` goes through in `container`'s partition, once every change
  // has reached its network context.
  std::string LookUp(const ContainerId& container, const char* url) {
    proxies()->FlushForTesting();
    return ProxyLookup::Run(PartitionOf(container), GURL(url));
  }

  // The net error of loading `url` in a new tab in `container`.
  int LoadIn(const ContainerId& container, const GURL& url) {
    proxies()->FlushForTesting();
    std::unique_ptr<content::WebContents> contents =
        content::WebContents::Create(content::WebContents::CreateParams(
            profile(),
            containers::CreateSiteInstance(profile(), container, url)));
    content::TestNavigationObserver observer(contents.get());
    contents->GetController().LoadURL(url, content::Referrer(),
                                      ui::PAGE_TRANSITION_TYPED, std::string());
    observer.Wait();
    return observer.last_net_error_code();
  }
};

IN_PROC_BROWSER_TEST_F(KoanContainerProxyBrowserTest,
                       SettingAndRuntimeStateSwitchLive) {
  const ContainerId box = AddContainer(u"Linux box");
  const ContainerId other = AddContainer(u"Other");
  // The network context exists before the proxy is set.
  EXPECT_EQ("DIRECT", LookUp(box, kLocalDevServer));

  ASSERT_TRUE(proxies()->SetProxySetting(box, ProxySetting{kTunnel}));
  EXPECT_EQ("SOCKS5 127.0.0.1:1080", LookUp(box, kLocalDevServer));
  EXPECT_EQ("SOCKS5 127.0.0.1:1080", LookUp(box, "https://example.com/"));
  // Other containers and the default partition stay direct.
  EXPECT_EQ("DIRECT", LookUp(other, kLocalDevServer));
  EXPECT_EQ("DIRECT", ProxyLookup::Run(profile()->GetDefaultStoragePartition(),
                                       GURL(kLocalDevServer)));

  // A tunnel's runtime state replaces the saved server while it is set.
  proxies()->SetRuntimeProxy(box, RuntimeProxy::LocalSocks5(40123));
  EXPECT_EQ("SOCKS5 127.0.0.1:40123", LookUp(box, kLocalDevServer));
  proxies()->SetRuntimeProxy(box, RuntimeProxy::Disconnected());
  EXPECT_EQ("", LookUp(box, kLocalDevServer));
  proxies()->SetRuntimeProxy(box, std::nullopt);
  EXPECT_EQ("SOCKS5 127.0.0.1:1080", LookUp(box, kLocalDevServer));

  // Removing the setting takes the proxy away, runtime state or not.
  proxies()->SetRuntimeProxy(box, RuntimeProxy::Disconnected());
  ASSERT_TRUE(proxies()->SetProxySetting(box, std::nullopt));
  EXPECT_EQ("DIRECT", LookUp(box, kLocalDevServer));
}

IN_PROC_BROWSER_TEST_F(KoanContainerProxyBrowserTest,
                       ANewNetworkContextStartsWithTheProxy) {
  const ContainerId box = AddContainer(u"Linux box");
  ASSERT_TRUE(proxies()->SetProxySetting(box, ProxySetting{kTunnel}));
  EXPECT_EQ("SOCKS5 127.0.0.1:1080", LookUp(box, kLocalDevServer));
}

IN_PROC_BROWSER_TEST_F(KoanContainerProxyBrowserTest, NeverGoesDirect) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  const ContainerId box = AddContainer(u"Linux box");
  const ContainerId other = AddContainer(u"Other");

  // A proxy nobody listens on: the request fails rather than going direct.
  net::EmbeddedTestServer closed;
  ASSERT_TRUE(closed.Start());
  const std::string closed_proxy =
      "socks5://127.0.0.1:" + base::NumberToString(closed.port());
  ASSERT_TRUE(closed.ShutdownAndWaitUntilComplete());
  ASSERT_TRUE(
      proxies()->SetProxySetting(box, ProxySetting{.server = closed_proxy}));
  EXPECT_EQ(net::ERR_PROXY_CONNECTION_FAILED, LoadIn(box, page));

  // Disconnected: nothing to connect through.
  proxies()->SetRuntimeProxy(box, RuntimeProxy::Disconnected());
  EXPECT_EQ(net::ERR_NO_SUPPORTED_PROXIES, LoadIn(box, page));

  // A container without a proxy still loads.
  EXPECT_EQ(net::OK, LoadIn(other, page));
}

}  // namespace

}  // namespace koan
