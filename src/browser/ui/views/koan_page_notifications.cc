// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_page_notifications.h"

#include <optional>
#include <string>

#include "base/containers/lru_cache.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/elide_url.h"
#include "content/public/browser/notification_event_dispatcher.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/ui/views/koan_space_profile_menu.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/base_window.h"
#include "ui/message_center/public/cpp/notification.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace koan {

namespace {

// Where a page's notification came from.
struct NotificationSource {
  base::WeakPtr<content::WebContents> contents;
  // The window and Space its tab was in when it showed, for a tab that has
  // closed since.
  base::WeakPtr<BrowserWindowInterface> window;
  std::optional<tab_groups::TabGroupId> space;
};

// The latest page notifications' sources, by notification id. An entry stays
// after its notification closed until newer ones push it out: macOS keeps
// notifications in its Notification Center, and Koan isn't told of every
// close.
constexpr size_t kMaxNotificationSources = 100;

// What fits in a macOS banner's subtitle (Chromium's
// CreateMacNotificationContext() measures 28 wide characters), which shows a
// set context message untrimmed; past it, the site is cut to its domain so the
// Space's name stays.
constexpr size_t kMaxContextLength = 28;

base::LRUCache<std::string, NotificationSource>& Sources() {
  static base::NoDestructor<base::LRUCache<std::string, NotificationSource>>
      sources(kMaxNotificationSources);
  return *sources;
}

// Whether `contents` is in a Space's profile: a user container, not an agent's
// (whose pages never get the permission) nor the default partition.
bool IsInSpaceProfile(content::WebContents* contents) {
  const std::optional<containers::ContainerId> container =
      containers::ContainerOf(contents);
  return container && container->kind == containers::ContainerKind::kUser;
}

// The window comes forward, out of the Dock or the taskbar if minimised.
void BringToFront(BrowserWindowInterface* window) {
  ui::BaseWindow* base_window = window->GetWindow();
  if (!base_window) {
    return;
  }
  if (base_window->IsMinimized()) {
    base_window->Restore();
  }
  base_window->Activate();
}

// The tab, its Space and its window come forward, as when the user clicks
// the tab in the sidebar.
void ShowTab(content::WebContents* contents) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (!window) {
    return;
  }
  TabStripModel* strip = window->GetTabStripModel();
  if (KoanWorkspaceController* spaces =
          KoanWorkspaceController::FromTabStrip(strip);
      spaces && tab->GetGroup()) {
    spaces->SwitchTo(*tab->GetGroup());
  }
  const int index = strip->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    strip->ActivateTabAt(index);
  }
  BringToFront(window);
}

// The Space the closed tab was in comes forward, in its window.
void ShowSpace(BrowserWindowInterface* window,
               const std::optional<tab_groups::TabGroupId>& space) {
  if (KoanWorkspaceController* spaces =
          KoanWorkspaceController::FromTabStrip(window->GetTabStripModel());
      spaces && space) {
    spaces->SwitchTo(*space);
  }
  BringToFront(window);
}

}  // namespace

void OnShowingPageNotification(Profile* profile,
                               message_center::Notification& notification) {
  content::RenderFrameHost* frame =
      content::NotificationEventDispatcher::GetInstance()
          ->GetNonPersistentNotificationFrame(notification.id());
  content::WebContents* contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  if (!contents) {
    // A shared worker's notification: no page.
    return;
  }
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  KoanWorkspaceController* spaces =
      window ? KoanWorkspaceController::FromTabStrip(window->GetTabStripModel())
             : nullptr;
  Sources().Put(notification.id(),
                NotificationSource{
                    .contents = contents->GetWeakPtr(),
                    .window = window ? window->GetWeakPtr() : nullptr,
                    .space = tab ? tab->GetGroup() : std::nullopt,
                });

  if (!IsInSpaceProfile(contents)) {
    return;
  }
  // The tab's Space, or for a tab in none (an Essential, a window without
  // Spaces) its profile's name.
  std::u16string name;
  if (spaces && tab->GetGroup()) {
    if (const std::optional<KoanWorkspaceController::SpaceInfo> space =
            spaces->GetSpace(*tab->GetGroup())) {
      name = space->name;
    }
  }
  if (name.empty()) {
    name = SpaceProfileName(
        profile, containers::Serialize(*containers::ContainerOf(contents)));
  }
  std::u16string site = url_formatter::FormatOriginForSecurityDisplay(
      url::Origin::Create(notification.origin_url()),
      url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS);
  if (PageNotificationContext(site, name).size() > kMaxContextLength) {
    if (const std::string domain =
            net::registry_controlled_domains::GetDomainAndRegistry(
                notification.origin_url(),
                net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
        !domain.empty()) {
      site = base::UTF8ToUTF16(domain);
    }
  }
  notification.set_context_message(PageNotificationContext(site, name));
}

void OnPageNotificationClicked(Profile* profile,
                               const std::string& notification_id) {
  base::LRUCache<std::string, NotificationSource>& sources = Sources();
  const auto source = sources.Get(notification_id);
  if (source == sources.end()) {
    return;
  }
  if (content::WebContents* contents = source->second.contents.get()) {
    if (Profile::FromBrowserContext(contents->GetBrowserContext()) == profile) {
      ShowTab(contents);
    }
    return;
  }
  if (BrowserWindowInterface* window = source->second.window.get();
      window && window->GetProfile() == profile) {
    ShowSpace(window, source->second.space);
  }
}

std::u16string PageNotificationContext(const std::u16string& site,
                                       const std::u16string& space) {
  return space.empty() ? site : base::StrCat({site, u" · ", space});
}

bool ShowsProfileNotificationNote(content::WebContents* contents) {
  if (!contents || !IsInSpaceProfile(contents)) {
    return false;
  }
  const GURL& url = contents->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  HostContentSettingsMap* settings =
      HostContentSettingsMapFactory::GetForProfile(
          Profile::FromBrowserContext(contents->GetBrowserContext()));
  return settings && settings->GetContentSetting(
                         url, url, ContentSettingsType::NOTIFICATIONS) !=
                         CONTENT_SETTING_BLOCK;
}

}  // namespace koan
