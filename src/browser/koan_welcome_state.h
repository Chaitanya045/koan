// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_WELCOME_STATE_H_
#define KOAN_BROWSER_KOAN_WELCOME_STATE_H_

class BrowserWindowInterface;

namespace koan {

// Which window Koan's welcome screen (KoanWelcome) is about to show in or
// shows in: at most one, for the browser session. Kept apart from the views
// code so that upstream hooks can ask it cheaply.
void SetWelcomeWindow(const BrowserWindowInterface* browser);

// Hook for SearchEngineChoiceDialogService::ComputeDialogConditions() (a
// hook patch): whether the welcome is pending or showing in `browser`. Its
// search engine page is Zen's choice screen, so Chromium's own dialog waits
// (and once Next there has recorded the choice, never shows).
bool IsWelcomeShowing(const BrowserWindowInterface& browser);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_WELCOME_STATE_H_
