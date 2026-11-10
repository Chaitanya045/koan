// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_UPDATES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_UPDATES_H_

class BrowserView;

// Updates for a Koan built from source (issue #84; koan_update_logic.h).
// Browser-wide, started by the first normal window:
//
// - Zen's "Update Complete!" (ZenUpdates.mjs): after a launch of a build
//   other than the previous launch's (its Chromium version or Koan commit,
//   stamped at build time), the sidebar shows Zen's notification once, with
//   what's new (the commits between the two builds on GitHub).
// - The update check: at most once a day (koan_prefs.h kUpdatesCheckEnabled,
//   on by default), one request to GitHub's API for the newest commit of the
//   clone's develop branch. It carries no cookies, no credentials and
//   nothing about the user; Incognito windows never start it. When that
//   commit is newer than the build's, the sidebar says so once, with what's
//   new and how to update (`git pull && scripts/build.sh`). Nothing is ever
//   downloaded or installed.
namespace koan::updates {

// From KoanBrowserViewController, once `browser_view` is in its widget.
void OnBrowserWindowShown(BrowserView* browser_view);

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_UPDATES_H_
