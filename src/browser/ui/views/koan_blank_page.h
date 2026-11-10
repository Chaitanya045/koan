// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BLANK_PAGE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BLANK_PAGE_H_

class Profile;

namespace content {
class WebContents;
}

namespace koan {

// Whether `contents` shows a blank page with no address to show: about:blank
// or the new tab page (also one the search engine serves). The URL pill shows
// Zen's placeholder for these, as Firefox's URL bar is empty on its initial
// pages (gInitialPages). Not Zen's empty tab: that is the tab Koan marks
// (IsEmptyTab() in koan_spaces.h), which a user's own about:blank isn't.
bool IsBlankPage(content::WebContents* contents, Profile* profile);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BLANK_PAGE_H_
