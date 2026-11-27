// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_UPDATES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_UPDATES_H_

class BrowserView;

// Updates from Koan's tagged GitHub releases (issues #84, #549;
// koan_update_logic.h). Browser-wide, started by the first normal window:
//
// - Zen's "Update Complete!" (ZenUpdates.mjs): after a launch of a build
//   other than the previous launch's (Koan's or Chromium's version), the
//   sidebar shows Zen's notification once, with what's new (the release
//   page of this version).
// - The update check: a minute after the first window, then at most every
//   12 hours (koan_prefs.h kUpdatesCheckEnabled, on by default), one request
//   for the latest release's signed manifest (koan_update_source.h). It
//   carries no cookies, no credentials and nothing about the user; Incognito
//   windows never start it. When the manifest's entry for this platform
//   verifies and its version is newer, the sidebar says so once for that
//   version, with what's new and the release to download (on Linux also the
//   Flatpak install command). Nothing is downloaded or installed here; on
//   the Mac, #550 will.
namespace koan::updates {

// From KoanBrowserViewController, once `browser_view` is in its widget.
void OnBrowserWindowShown(BrowserView* browser_view);

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_UPDATES_H_
