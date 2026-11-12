// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_CONTAINERS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_CONTAINERS_H_

#include "base/memory/scoped_refptr.h"

class GURL;
struct NavigateParams;

namespace content {
class SiteInstance;
}  // namespace content

namespace koan {

// CreateTargetContents() in browser_navigator.cc, for a new tab with no
// opener: the SiteInstance it starts in, or null for upstream's (the default
// partition). Zen's containers per Space (#318,
// .agent/notes/upstream/space-containers.md): a tab no page opened (Cmd+T,
// the URL bar, bookmarks, links from other apps, New Split) opens in its
// Space's container; a tab a page opened keeps the page's partition
// (koan::containers::SiteInstanceForNewTab()); a routed tab takes its route's
// Space's container. Windows without Spaces and pinned tabs: the page's
// partition only, as before.
scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    const NavigateParams& params,
    const GURL& url);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_CONTAINERS_H_
