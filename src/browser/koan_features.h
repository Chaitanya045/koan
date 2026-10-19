// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
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

// Glance: Shift+click a link to preview it in a floating card.
BASE_DECLARE_FEATURE(kKoanGlance);

bool IsSidebarEnabled();
bool IsCompactModeEnabled();
bool IsWorkspacesEnabled();
bool IsGlanceEnabled();

}  // namespace koan::features

#endif  // KOAN_BROWSER_KOAN_FEATURES_H_
