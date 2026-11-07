// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_empty_tab.h"

#include "chrome/browser/search/search.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace koan {

bool IsEmptyTab(content::WebContents* contents, Profile* profile) {
  if (!contents) {
    return false;
  }
  const GURL& url = contents->GetVisibleURL();
  // A new tab page also counts when the search engine serves it (e.g.
  // google.com's).
  return url.is_empty() || url.IsAboutBlank() ||
         url == GURL(chrome::kChromeUINewTabURL) || search::IsNTPURL(url) ||
         search::IsNTPOrRelatedURL(url, profile);
}

}  // namespace koan
