// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_EMPTY_TAB_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_EMPTY_TAB_H_

class Profile;

namespace content {
class WebContents;
}

namespace koan {

// Whether `contents` is what Zen calls the empty tab: a blank page, with no
// address (about:blank, the new tab page, also one the search engine serves).
// Its pill is blank and compact mode can't hide the sidebar while it is
// selected.
bool IsEmptyTab(content::WebContents* contents, Profile* profile);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_EMPTY_TAB_H_
