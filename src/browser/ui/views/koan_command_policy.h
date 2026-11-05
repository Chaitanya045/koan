// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_POLICY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_POLICY_H_

class BrowserView;
class BrowserWindowInterface;

namespace koan {

// Koan's browser-command redirects, in one table (koan_command_policy.cc;
// audit 2 §0.1, .agent/notes/upstream/tab-groups-omnibox-fullscreen.md).
//
// With kUseActionsForBrowserCommands (on by default in M158) every route to
// a browser command, the Mac menu bar, shortcuts, chrome::ExecuteCommand()
// and toolbar buttons, runs the window's own ActionItem (BrowserActions).
// Re-pointing that item redirects them all, in this window only. Each row
// names the command, the Koan code that may take it, and restates upstream's
// callback (browser_actions.cc) for when Koan lets it through.
//
// Koan forces the feature on (koan_feature_overrides.cc); without it the
// legacy switch in BrowserCommandController::HandleCommandWithDisposition()
// runs, which asks HandlesBrowserCommand() first (the backstop).
//
// Called once per normal window, when its KoanBrowserViewController exists.
void InstallCommandPolicy(BrowserView* browser_view);

// Whether Koan took browser command `command_id` (an IDC_* id) in `browser`,
// e.g. ⌘W while a Glance card is open closes the card. The rows of the table
// in one function, for the legacy command switch.
bool HandlesBrowserCommand(BrowserWindowInterface* browser, int command_id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_POLICY_H_
