// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_UNLOAD_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_UNLOAD_H_

namespace content {
class WebContents;
}

namespace koan {

// Zen's explicit unloading (Unload Space, a tab's Unload), shared by its
// callers.

// Zen's "pending" tabs: not loaded (discarded, or restored and not shown
// since). Unloading skips them.
bool IsTabUnloaded(content::WebContents* contents);

// Whether `contents` is in use in a way Memory Saver protects
// (performance_manager's DiscardEligibilityPolicy): on screen, playing
// sound, in picture-in-picture, capturing or mirrored, connected to a
// device, open in DevTools, holding form input, kept by an extension or the
// "always keep these sites active" list. Being recently visible or audible,
// or the URL's scheme, don't count for an unload the user asked for.
bool IsTabInUse(content::WebContents* contents);

// Discards `contents` (it reloads when shown). Not for the active tab.
void UnloadTab(content::WebContents* contents);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_UNLOAD_H_
