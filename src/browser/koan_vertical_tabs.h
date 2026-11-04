// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_VERTICAL_TABS_H_
#define KOAN_BROWSER_KOAN_VERTICAL_TABS_H_

class PrefService;

// Koan owns the sidebar: upstream's vertical tab strip stays, but its state
// machine (VerticalTabStripStateControllerImpl) answers to Koan. Called from
// chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc, so kept
// free of tab strip types. See .agent/notes/upstream/vertical-tabs.md.
namespace koan {

// Whether Koan owns the sidebar (its feature is on), the one predicate for it:
// vertical tabs are always on, expand-on-hover is always on, and the strip is
// "collapsed" only while compact mode hides the sidebar, which is never saved
// to the profile or the session; Chromium's own ways to change the mode or to
// collapse the strip are not offered (the menus) or do nothing (the commands).
// Without Koan's sidebar (the feature is off) upstream behaves as in Chrome.
bool OwnsSidebar();

// While one of these lives, a request to collapse or expand the strip is
// Koan's (KoanVerticalTabsPolicy::SetSidebarHidden()); any other, e.g. the
// legacy command path (UseActionsForBrowserCommands off), is refused.
class ScopedSidebarCollapseRequest {
 public:
  ScopedSidebarCollapseRequest();
  ~ScopedSidebarCollapseRequest();
  ScopedSidebarCollapseRequest(const ScopedSidebarCollapseRequest&) = delete;
  ScopedSidebarCollapseRequest& operator=(const ScopedSidebarCollapseRequest&) =
      delete;
};
bool IsSidebarCollapseRequestAllowed();

// Whether a new window's sidebar starts out collapsed (hidden by compact
// mode), from `prefs`, so that a compact window starts hidden without an
// animation and an old saved collapse can't make a rail.
bool InitialSidebarCollapsed(const PrefService* prefs);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_VERTICAL_TABS_H_
