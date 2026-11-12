// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_containers.h"

#include <optional>
#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/constants.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/ui/views/koan_space_routing.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace koan {

namespace {

// A page a container can hold: a web page, about:, data:, blob:, file: and
// the new tab page (and so a Space's empty tab). Not chrome://settings and
// the like (nothing per site), extension pages (their own partition),
// DevTools, isolated apps.
bool IsContainerUrl(const GURL& url) {
  return url.is_empty() || url.SchemeIsHTTPOrHTTPS() ||
         url.SchemeIs(url::kAboutScheme) || url.SchemeIs(url::kDataScheme) ||
         url.SchemeIs(url::kBlobScheme) || url.SchemeIsFile() ||
         url == GURL(chrome::kChromeUINewTabURL);
}

// The container of the Space `contents` is a tab of, in its own window;
// empty for none.
std::string ContainerOfTabSpace(content::WebContents* contents) {
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  KoanWorkspaceController* spaces =
      browser
          ? KoanWorkspaceController::FromTabStrip(browser->GetTabStripModel())
          : nullptr;
  return spaces && tab->GetGroup() ? spaces->GetSpaceContainer(*tab->GetGroup())
                                   : std::string();
}

}  // namespace

scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    const NavigateParams& params,
    const GURL& url) {
  Profile* profile = params.browser->GetProfile();
  content::SiteInstance* source = params.source_site_instance.get();
  KoanWorkspaceController* spaces =
      KoanWorkspaceController::FromTabStrip(params.browser->GetTabStripModel());
  // No Spaces (Incognito, a New Blank Window), or an Essential: a page's tab
  // keeps the page's container, as before.
  if (!spaces || profile->IsOffTheRecord() ||
      (params.tabstrip_add_types & AddTabTypes::ADD_PINNED)) {
    return containers::SiteInstanceForNewTab(profile, source, url);
  }

  containers::NewTabFacts facts{.has_opener = params.opener != nullptr,
                                .container_url = IsContainerUrl(url)};
  // The Space the tab opens in: the one asked for, else a page's tab goes
  // next to its page, any other in the active Space (PlaceUnindexedTab()).
  std::optional<tab_groups::TabGroupId> space = spaces->GetActiveSpace();
  if (source) {
    facts.from_page = true;
    if (const std::optional<containers::ContainerId> container =
            containers::ContainerFromPartitionConfig(
                source->GetSecurityPrincipal().GetStoragePartitionConfig())) {
      facts.source_container = containers::Serialize(*container);
    }
    facts.from_agent =
        containers::IsAgentTab(params.source_contents) ||
        containers::IsAgentPartition(
            source->GetSecurityPrincipal().GetStoragePartitionConfig());
    facts.source_space_container = ContainerOfTabSpace(params.source_contents);
    tabs::TabInterface* source_tab =
        params.source_contents
            ? tabs::TabInterface::MaybeGetFromContents(params.source_contents)
            : nullptr;
    if (source_tab &&
        source_tab->GetBrowserWindowInterface() == params.browser) {
      space = source_tab->GetGroup();
    }
  } else {
    facts.from_extension =
        params.initiator_origin &&
        params.initiator_origin->scheme() == extensions::kExtensionScheme;
  }
  if (params.group) {
    space = params.group;
  }
  // Zen's Space routing (KoanSpaceRouting::OnTabInserted() moves the tab once
  // it is in): only a tab for the active Space is routed.
  if (space && space == spaces->GetActiveSpace()) {
    if (const std::optional<tab_groups::TabGroupId> routed =
            spaces->space_routing()->RouteFor(url,
                                              ScopedExternalLinks::IsActive());
        routed && routed != space) {
      facts.route_container = spaces->GetSpaceContainer(*routed);
    }
  }
  if (space) {
    facts.space_container = spaces->GetSpaceContainer(*space);
  }

  const std::optional<std::string> container =
      containers::NewTabContainer(facts);
  if (!container) {
    return containers::SiteInstanceForNewTab(profile, source, url);
  }
  const std::optional<containers::ContainerId> id =
      containers::SpaceContainerFromString(*container);
  if (!id) {
    // The default partition (upstream's SiteInstance).
    return nullptr;
  }
  return containers::CreateSiteInstance(
      profile, *id, url.SchemeIsHTTPOrHTTPS() ? url : GURL());
}

}  // namespace koan
