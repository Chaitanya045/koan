// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_POLICY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_POLICY_H_

#include <optional>

class BrowserView;
class BrowserWindowInterface;

namespace koan {

// Koan's command policy: every browser command Koan redirects, and every
// macOS menu bar item it hides, in one place (koan_command_policy.cc; audit 2
// §0.1-0.2, .agent/notes/upstream/tab-groups-omnibox-fullscreen.md).
//
// With kUseActionsForBrowserCommands (forced on in koan_feature_overrides.cc)
// every route to a browser command, the menu bar, shortcuts,
// chrome::ExecuteCommand() and toolbar buttons, runs the window's own
// ActionItem (BrowserActions). Re-pointing that item redirects them all, in
// this window only. Each row of the table names the action, the command, the
// Koan code that may take it and, where Koan lets it through, upstream's
// callback (restated from browser_actions.cc).

// Installs the table's rows in `browser_view`'s window. Called once per
// window, by KoanBrowserViewController::CreateFor() once the window's
// controller is set.
void InstallCommandPolicy(BrowserView* browser_view);

// BrowserCommandController::HandleCommandWithDisposition(), first: the same
// rows for the legacy command switch (without kUseActionsForBrowserCommands).
// Returns whether Koan took browser command `command_id` (an IDC_* id) in
// `browser`; false lets upstream run it.
bool HandlesBrowserCommand(BrowserWindowInterface* browser, int command_id);

// BrowserNativeWidgetMac::ValidateUserInterfaceItem(), after upstream: the
// macOS menu bar items Koan hides per window. For an item Koan decides,
// whether it is hidden (and disabled) in `browser_view`'s window, or shown;
// nullopt for every other item (upstream's state stands). The menu bar is
// shared by the windows, so each item Koan owns gets an answer both ways.
// (A hidden item's key equivalent doesn't fire in AppKit: hide only what
// must not run from the keyboard either.)
std::optional<bool> HidesMenuCommand(BrowserView* browser_view, int command_id);

// BrowserNativeWidgetMac::ValidateUserInterfaceItem(), after upstream: the
// check mark of a macOS menu bar item whose command Koan redirects to its own
// setting (View > Always Show Bookmarks Bar: Koan's bookmarks bar setting),
// in `browser_view`'s window; nullopt for every other item.
std::optional<bool> MenuCommandCheckState(BrowserView* browser_view,
                                          int command_id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_POLICY_H_
