// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_HOOKS_H_

class BrowserWindowInterface;

namespace koan {

// Zen's window sync (koan_window_sync.h), called from upstream.

// UnloadController::OnWindowClosing(), before the window's session entry,
// its Recently Closed entry and its tabs' close: the window's synced pages
// (Essentials and pinned tabs whose page it has) move to the most recently
// active other synced window, as Zen does on SSWindowClosing. Nothing while
// quitting, nor for a window that isn't synced.
void OnWindowClosing(BrowserWindowInterface* browser);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_HOOKS_H_
