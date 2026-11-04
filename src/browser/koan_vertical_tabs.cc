// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_vertical_tabs.h"

#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"

namespace koan {

namespace {
int g_collapse_requests = 0;
}  // namespace

bool OwnsSidebar() {
  return features::IsSidebarEnabled();
}

ScopedSidebarCollapseRequest::ScopedSidebarCollapseRequest() {
  ++g_collapse_requests;
}

ScopedSidebarCollapseRequest::~ScopedSidebarCollapseRequest() {
  --g_collapse_requests;
}

bool IsSidebarCollapseRequestAllowed() {
  return g_collapse_requests > 0;
}

bool InitialSidebarCollapsed(const PrefService* prefs) {
  // What compact mode hides: the sidebar, unless only the toolbar is hidden
  // (and then not with single toolbar, which has no toolbar to hide).
  return OwnsSidebar() && features::IsCompactModeEnabled() &&
         prefs->GetBoolean(prefs::kCompactModeEnabled) &&
         (prefs->GetBoolean(prefs::kSingleToolbarEnabled) ||
          prefs->GetString(prefs::kCompactModeHide) !=
              prefs::kCompactModeHideToolbar);
}

}  // namespace koan
