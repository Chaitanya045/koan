// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_HOOKS_H_

// Hooks for Zen's pinned tabs and Essentials, called from upstream tab code
// (see patches/). Their tab menu items and session data go through
// koan_tab_menu.h and koan_session_data.h, and the pinned check other code
// uses is in koan_pinned.h. Declarations only, so upstream targets can
// depend on them cheaply; the definitions live in //koan/browser/ui/views:impl,
// linked through //chrome/browser:browser.

class TabView;

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
struct TabData;
}  // namespace tabs

namespace koan {

// --- Tab views ---

// Adds the reset button ("Back to pinned url") to a new tab view. Called at
// the end of TabView's constructor.
void OnTabViewCreated(TabView* tab_view);

// The tab view gave its close button new colours, which redraws upstream's
// close icon: a pinned tab's close button shows Zen's unload button instead.
// Called in TabView::UpdateColors().
void OnTabViewColorsChanged(const TabView* tab_view);

// The close button or a middle click on `tab`: a pinned tab follows Zen's
// close behaviour instead of closing, and a Space's only tab on a blank page
// becomes its empty tab (KeepAsEmptyTabInsteadOfClosing() in koan_spaces.h).
// Returns true if Koan handled it.
bool HandleTabCloseRequest(const tabs::TabInterface* tab);

// Chrome's "Close Tab" command (Ctrl+W, Ctrl+F4, Cmd+W and the menu bar on
// macOS) on the active `tab`: a pinned tab or Essential follows Zen's close
// behaviour instead, the empty tab closes the window, and a Space's only tab
// on a blank page becomes its empty tab. Returns true if Koan handled it.
// Called from chrome::CloseTab().
bool HandleCloseTabCommand(tabs::TabInterface* tab);

// Called from chrome::DuplicateTabAt() once `duplicate`, the clone of
// `source`, is in the window: Zen's duplicate of a pinned tab or Essential is
// a normal tab of the active Space. Returns whether `duplicate` is still
// pinned (for the session).
bool OnTabDuplicated(content::WebContents* source,
                     content::WebContents* duplicate);

// Applies a tab's custom label and icon (Change Label… / Change Icon…), and
// hides the throbber where Zen does (ShowsTabThrobber()). Called at the end of
// TabData::FromTabInterface().
void AdjustTabData(const tabs::TabInterface* tab, tabs::TabData& data);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_HOOKS_H_
