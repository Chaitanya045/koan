// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_FLAG_ENTRIES_H_
#define KOAN_BROWSER_KOAN_FLAG_ENTRIES_H_

#include "components/webui/flags/feature_entry_macros.h"
#include "koan/browser/koan_features.h"

// Expands to the Koan entries of about_flags.cc's kFeatureEntries. Uses that
// file's kOsDesktop. Names and descriptions live here so upstream
// flag_descriptions.{h,cc} stay untouched.
#define KOAN_FEATURE_ENTRIES                                                 \
  {"koan-sidebar", "Koan sidebar",                                           \
   "Vertical tabs by default, with Zen-style sidebar styling.", kOsDesktop, \
   FEATURE_VALUE_TYPE(koan::features::kKoanSidebar)},                        \
      {"koan-compact-mode", "Koan compact mode",                             \
       "Auto-hiding sidebar and toolbar, toggled with Ctrl+Alt+C.",          \
       kOsDesktop, FEATURE_VALUE_TYPE(koan::features::kKoanCompactMode)},

#endif  // KOAN_BROWSER_KOAN_FLAG_ENTRIES_H_
