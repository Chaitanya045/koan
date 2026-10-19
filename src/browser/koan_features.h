// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_FEATURES_H_
#define KOAN_BROWSER_KOAN_FEATURES_H_

#include "base/feature_list.h"

namespace koan::features {

// Vertical tabs on by default, with Zen-style sidebar styling.
BASE_DECLARE_FEATURE(kKoanSidebar);

// Compact mode (auto-hiding sidebar and toolbar) is available.
BASE_DECLARE_FEATURE(kKoanCompactMode);

// Workspaces (tab groups shown one at a time) with a switcher in the sidebar.
BASE_DECLARE_FEATURE(kKoanWorkspaces);

// Glance: Alt+click a link to preview it in a floating card, like Zen.
BASE_DECLARE_FEATURE(kKoanGlance);

// Restored background tabs (session restore, reopened windows) load when
// first shown instead of all at startup, like Zen. Read in the browser
// process only.
BASE_DECLARE_FEATURE(kKoanRestoreTabsOnDemand);

bool IsSidebarEnabled();
bool IsCompactModeEnabled();
bool IsWorkspacesEnabled();
bool IsGlanceEnabled();
bool IsRestoreTabsOnDemandEnabled();

}  // namespace koan::features

#endif  // KOAN_BROWSER_KOAN_FEATURES_H_
