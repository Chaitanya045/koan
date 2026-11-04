// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_vertical_tabs.h"

#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"

namespace koan {

bool ForcesVerticalTabs() {
  return features::IsSidebarEnabled();
}

bool InitialSidebarCollapsed(const PrefService* prefs) {
  // What compact mode hides: the sidebar, unless only the toolbar is hidden
  // (and then not with single toolbar, which has no toolbar to hide).
  return ForcesVerticalTabs() && features::IsCompactModeEnabled() &&
         prefs->GetBoolean(prefs::kCompactModeEnabled) &&
         (prefs->GetBoolean(prefs::kSingleToolbarEnabled) ||
          prefs->GetString(prefs::kCompactModeHide) !=
              prefs::kCompactModeHideToolbar);
}

}  // namespace koan
