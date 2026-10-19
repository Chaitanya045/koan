// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/koan_features.h"

namespace koan::features {

BASE_FEATURE(kKoanSidebar, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanCompactMode, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWorkspaces, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanGlance, base::FEATURE_ENABLED_BY_DEFAULT);

bool IsSidebarEnabled() {
  return base::FeatureList::IsEnabled(kKoanSidebar);
}

bool IsCompactModeEnabled() {
  return base::FeatureList::IsEnabled(kKoanCompactMode);
}

bool IsWorkspacesEnabled() {
  return base::FeatureList::IsEnabled(kKoanWorkspaces);
}

bool IsGlanceEnabled() {
  return base::FeatureList::IsEnabled(kKoanGlance);
}

}  // namespace koan::features
