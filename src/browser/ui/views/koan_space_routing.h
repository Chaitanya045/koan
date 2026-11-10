// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ROUTING_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ROUTING_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"

class BrowserView;

namespace content {
class WebContents;
}

namespace koan {

class KoanWorkspaceController;

// Zen's Space routing for one window (ZenSpaceRoutingManager.sys.mjs): a new
// tab whose URL matches a route (koan_space_routes.h, prefs::kSpaceRoutes)
// goes to that route's Space instead of the active one, in the background,
// with Zen's toast "New tab opened in <Space>". Links from other apps
// (ScopedExternalLinks, koan_spaces.h) with no matching route go to
// prefs::kSpaceRoutingExternalDefault. Never for Essentials or pinned tabs,
// tabs restored by a session, duplicated or moved between windows, or tabs
// placed in a Space other than the active one on purpose (agent containers).
// A tab that opens empty and then navigates (window.open(), target=_blank)
// is routed on its first navigation, as Zen's ZenSpaceRoutingNavigation.
// Zen's container option is not copied: Chromium has no containers here.
class KoanSpaceRouting {
 public:
  KoanSpaceRouting(KoanWorkspaceController* controller,
                   BrowserView* browser_view);
  KoanSpaceRouting(const KoanSpaceRouting&) = delete;
  KoanSpaceRouting& operator=(const KoanSpaceRouting&) = delete;
  ~KoanSpaceRouting();

  // From the window's tab strip observer: `tab` was just inserted, with
  // `previous` the tab that was active before (if any).
  void OnTabInserted(tabs::TabInterface* tab, tabs::TabInterface* previous);

  // Zen's "Add Route for Domain(s)" (tab context menu): a route for the
  // tabs' hosts to the most recent Space, then the settings to pick one.
  void AddRouteForTabs(const std::vector<tabs::TabInterface*>& tabs);
  // Zen's "Space Routing Settings" (Space menu, the command palette's "Open
  // Space Routing"): the routes and the default for external links.
  void ShowSettings();

  void OnBrowserViewDestroying();

 private:
  class FirstNavigationWatcher;

  void RouteTab(tabs::TabHandle handle,
                const GURL& url,
                bool from_external,
                tabs::TabHandle previous);
  void OnFirstNavigation(content::WebContents* contents, const GURL& url);
  void StopWatching(content::WebContents* contents);
  std::optional<tab_groups::TabGroupId> SpaceNamed(
      const std::u16string& name) const;

  const raw_ptr<KoanWorkspaceController> controller_;
  const raw_ptr<BrowserView> browser_view_;
  bool destroying_ = false;
  std::map<content::WebContents*, std::unique_ptr<FirstNavigationWatcher>>
      watchers_;
  base::WeakPtrFactory<KoanSpaceRouting> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ROUTING_H_
