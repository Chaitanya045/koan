// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_SITE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_SITE_H_

#include "url/gurl.h"

// A pinned tab or Essential keeps its site, as in Zen (issue #74, Z8 of
// #161): what the URL bar opens from it on another site goes to a new tab
// (zen.tabs.open-pinned-in-new-tab), and a link from it that opens a new tab
// on another site opens in Glance (zen.glance.open-essential-external-links).
// The site is the host, as Zen compares it (www.example.com is not
// example.com; the scheme and port don't count). The hooks are in
// koan_url_bar.h and koan_glance.h.
namespace koan {

// Zen's ZenUrlbarController #urlStringsDomainMatch: whether `destination`,
// opened from the URL bar of a pinned tab showing `page`, loads in that tab.
// Only an http(s) URL on the page's host does; a search, or any URL from a
// page that isn't http(s) (a new tab page), opens a new tab.
inline bool StaysOnPinnedSite(const GURL& page, const GURL& destination) {
  return page.SchemeIsHTTPOrHTTPS() && destination.SchemeIsHTTPOrHTTPS() &&
         page.host() == destination.host();
}

// Zen's ZenGlanceManager tabDomainsDiffer, for Koan's Glance (http(s) pages
// only): whether a link from a pinned tab showing `page` that would open a
// new tab on `target` leaves the tab's site, so it opens in Glance. Any
// http(s) link from a page that isn't http(s) (Zen: about: pages) does.
inline bool LeavesPinnedSite(const GURL& page, const GURL& target) {
  if (!target.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  return !page.SchemeIsHTTPOrHTTPS() || page.host() != target.host();
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_SITE_H_
