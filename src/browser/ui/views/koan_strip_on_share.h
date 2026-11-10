// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_STRIP_ON_SHARE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_STRIP_ON_SHARE_H_

#include "url/gurl.h"

namespace koan {

// Zen copies and shares a page's URL without its tracking parameters (Zen's
// gURLBar.zenStrippedURI, Firefox's QueryStringStripper.stripForCopyOrShare()
// with its strip-on-share list, issue #78): `url` without the query
// parameters on Firefox's list (utm_*, fbclid, gclid ...), and on a site
// with a list of its own (Spotify's si, Amazon's ref ...) without those too.
// The other parameters keep their order and their text. Only http(s) URLs
// are changed; a URL left with no parameters loses its "?". The list is in
// koan_strip_on_share.cc.
GURL StripTrackingParameters(const GURL& url);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_STRIP_ON_SHARE_H_
