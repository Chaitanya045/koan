// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_NEW_TAB_PAGE_H_
#define KOAN_BROWSER_KOAN_NEW_TAB_PAGE_H_

#include "url/gurl.h"

class PrefService;

namespace koan {

// The page chrome://newtab shows instead of Chromium's, for a regular
// profile with `prefs` (prefs::kNewTabPage): Koan's own, chrome://koan-newtab.
// Empty when the user chose Chromium's, or Koan's new tab page is off in
// chrome://flags. Called from
// chrome/browser/search/search.cc, after the checks for a new tab page set
// by policy or by an extension, which come first and win.
GURL GetNewTabPageURL(const PrefService& prefs);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_NEW_TAB_PAGE_H_
