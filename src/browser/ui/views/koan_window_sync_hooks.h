// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_HOOKS_H_

#include "base/functional/callback_helpers.h"

class BrowserWindowInterface;
class Profile;

namespace koan {

// Zen's window sync (koan_window_sync.h), called from upstream.

// UnloadController::OnWindowClosing(), before the window's session entry,
// its Recently Closed entry and its tabs' close: the window's synced pages
// (Essentials and pinned tabs whose page it has) move to the most recently
// active other synced window, as Zen does on SSWindowClosing. Nothing while
// quitting, nor for a window that isn't synced.
void OnWindowClosing(BrowserWindowInterface* browser);

// GetBrowserForDisposition() in browser_navigator.cc, around the creation of
// a window for a kNewWindow navigation (a link's "Open Link in New Window",
// Shift+click): with Zen's zen.window-sync.open-link-in-new-unsynced-window
// (prefs::kWindowSyncOpenLinksInBlankWindow) and synced windows open, the
// window is a New Blank Window. Keep the result until the window exists.
[[nodiscard]] base::ScopedClosureRunner WillOpenWindowForNavigation(
    Profile* profile);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_HOOKS_H_
