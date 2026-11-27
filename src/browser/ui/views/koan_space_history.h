// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HISTORY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HISTORY_H_

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace koan {

// TabMatcherDesktop::GetOpenWebContents(): whether the URL bar of `profile`
// leaves out `contents`, an open tab, from Switch to Tab, its open-tab rows
// and the tabs search: with Keep history apart for each profile on, a tab of
// another Space profile than the one the URL bar shows the history of
// (koan_space_history_index.h, #474).
bool IsOutsideOmniboxProfile(Profile* profile, content::WebContents* contents);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_HISTORY_H_
