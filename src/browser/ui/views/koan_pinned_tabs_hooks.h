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

namespace tabs {
class TabInterface;
struct TabData;
}  // namespace tabs

namespace koan {

// --- Tab views ---

// Adds the reset button ("Back to pinned url") to a new tab view. Called at
// the end of TabView's constructor.
void OnTabViewCreated(TabView* tab_view);

// The close button or a middle click on `tab`: a pinned tab follows Zen's
// close behaviour instead of closing. Returns true if Koan handled it.
bool HandleTabCloseRequest(const tabs::TabInterface* tab);

// Chrome's "Close Tab" command (Ctrl+W, Ctrl+F4, Cmd+W and the menu bar on
// macOS) on the active `tab`: a pinned tab or Essential follows Zen's close
// behaviour instead. Returns true if Koan handled it. Called from
// chrome::CloseTab().
bool HandleCloseTabCommand(tabs::TabInterface* tab);

// Applies a tab's custom label and icon (Change Label… / Change Icon…).
// Called at the end of TabData::FromTabInterface().
void AdjustTabData(const tabs::TabInterface* tab, tabs::TabData& data);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_HOOKS_H_
