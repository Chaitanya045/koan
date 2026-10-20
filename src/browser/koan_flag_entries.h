// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_FLAG_ENTRIES_H_
#define KOAN_BROWSER_KOAN_FLAG_ENTRIES_H_

#include "components/performance_manager/public/features.h"
#include "components/webui/flags/feature_entry_macros.h"
#include "koan/browser/koan_features.h"

// Expands to the Koan entries of about_flags.cc's kFeatureEntries. Uses that
// file's kOsDesktop. Names and descriptions live here so upstream
// flag_descriptions.{h,cc} stay untouched.
#define KOAN_FEATURE_ENTRIES                                                 \
  {"koan-sidebar", "Koan sidebar",                                           \
   "Vertical tabs by default, with Zen-style sidebar styling.", kOsDesktop,  \
   FEATURE_VALUE_TYPE(koan::features::kKoanSidebar)},                        \
      {"koan-compact-mode", "Koan compact mode",                             \
       "Auto-hiding sidebar and toolbar, toggled with Ctrl+Alt+C.",          \
       kOsDesktop, FEATURE_VALUE_TYPE(koan::features::kKoanCompactMode)},    \
      {"koan-workspaces", "Koan workspaces",                                 \
       "Tab groups shown one at a time, switched from the sidebar or with "  \
       "Ctrl+Alt+Left/Right.",                                               \
       kOsDesktop, FEATURE_VALUE_TYPE(koan::features::kKoanWorkspaces)},     \
      {"koan-glance", "Koan Glance",                                         \
       "Alt+click a link (trigger method in Koan's settings) to preview it " \
       "in a floating card; expand it to a tab or open it in split view.",   \
       kOsDesktop, FEATURE_VALUE_TYPE(koan::features::kKoanGlance)},         \
      {"koan-restore-tabs-on-demand", "Koan restore tabs on demand",         \
       "Restored background tabs load when you first open them instead "     \
       "of all at startup, like Zen.",                                       \
       kOsDesktop,                                                           \
       FEATURE_VALUE_TYPE(koan::features::kKoanRestoreTabsOnDemand)},        \
      {"koan-freeze-background-tabs", "Koan freeze background tabs",         \
       "Background tabs other than the 5 most recently used stop running "   \
       "(except for 5 s each minute) unless they are pinned, played audio, " \
       "use WebRTC, devices or notifications. On by default in Koan.",       \
       kOsDesktop,                                                           \
       FEATURE_VALUE_TYPE(                                                   \
           performance_manager::features::kInfiniteTabsFreezing)},

#endif  // KOAN_BROWSER_KOAN_FLAG_ENTRIES_H_
