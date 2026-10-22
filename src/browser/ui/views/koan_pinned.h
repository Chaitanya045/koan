// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_H_

// Koan's pinned check, for every feature and for upstream code that leaves
// pinned tabs alone. Declarations only, so upstream targets can depend on it
// cheaply; defined in koan_pinned_tabs.cc (//koan/browser/ui/views:impl).

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace koan {

// Whether the user pinned `tab` / the tab showing `contents`: an Essential
// (Chromium's pinned tab) or, with Koan's pinned tabs on, a tab pinned to its
// Space, which Chromium doesn't know as pinned. Code that leaves pinned tabs
// alone asks this instead of TabInterface::IsPinned(): upstream's Close Other
// Tabs, Close Tabs Below and closing a tab on Back, and Koan's Clear, and so
// should other features (moving tabs between Spaces or into folders).
bool IsPinnedForUser(const tabs::TabInterface* tab);
bool IsPinnedForUser(content::WebContents* contents);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_H_
