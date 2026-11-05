// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_CRASH_RESTORE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_CRASH_RESTORE_H_

class BrowserWindowInterface;

namespace koan {

// Zen restores the previous session after a crash without asking
// (SessionStartup / SessionStore patches); Chromium shows a "Restore pages?"
// bubble and restores only when it is clicked. Called from
// chrome/browser/ui/startup/infobar_utils.cc for the first window of a
// launch, where upstream shows the bubble when `post_crash_launch`.
//
// Returns whether Koan dealt with the launch (restored, or decided to leave
// the session alone), so upstream shows no bubble. False means: show it as
// upstream does. That is the case for:
// - an Incognito or Guest window (no session service to restore from; the
//   bubble does nothing for them either);
// - a second crash in a row. Like Firefox (max_resumed_crashes=1), the first
//   crash is restored silently, and a crash during or after that restore
//   shows the bubble, so a session that crashes the browser can't loop.
//
// Nothing is restored twice: a later launch of the same profile in this
// process (a Dock reopen after every window was closed) or one while a
// restore runs does nothing. The restore itself is posted, so it runs after
// the launch has added its own tabs and not inside the tab strip callback
// that called this.
//
// Also resets the crash count when the last session ended cleanly.
bool RestoreAfterCrash(BrowserWindowInterface* browser, bool post_crash_launch);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_CRASH_RESTORE_H_
