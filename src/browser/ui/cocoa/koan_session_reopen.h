// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COCOA_KOAN_SESSION_REOPEN_H_
#define KOAN_BROWSER_UI_COCOA_KOAN_SESSION_REOPEN_H_

class Profile;

namespace koan {

// Called by -[AppController applicationShouldHandleReopen:hasVisibleWindows:]
// where Chromium would open an empty window, i.e. a click on the Dock icon
// with no browser window open: brings back the window that closed last, with
// its Spaces and tabs, as Zen does (Zen drops Firefox's macOS-only "pinned
// tabs only" branch in SessionStore.sys.mjs). Only where the On startup
// setting restores the last session, through Chromium's own session restore.
// Returns false if it restores nothing; the caller then opens its window.
// Returns true while a restore of this profile runs (its window is coming).
bool ReopenLastSession(Profile* profile);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COCOA_KOAN_SESSION_REOPEN_H_
