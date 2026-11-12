// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_features.h"

#include "koan/common/koan_glance_trigger.h"

namespace koan::features {

BASE_FEATURE(kKoanSidebar, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanCompactMode, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanSidebarSingleReflow, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWorkspaces, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanGlance, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanRestoreTabsOnDemand, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanCommandPalette, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanUrlBar, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanPinnedTabs, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanLibrary, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanMediaControls, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanDownloadAnimation, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWindowDrag, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWelcome, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanFolders, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanNewTab, base::FEATURE_ENABLED_BY_DEFAULT);

bool IsSidebarEnabled() {
  return base::FeatureList::IsEnabled(kKoanSidebar);
}

bool IsCompactModeEnabled() {
  return base::FeatureList::IsEnabled(kKoanCompactMode);
}

bool IsSidebarSingleReflowEnabled() {
  return base::FeatureList::IsEnabled(kKoanSidebarSingleReflow);
}

bool IsWorkspacesEnabled() {
  return base::FeatureList::IsEnabled(kKoanWorkspaces);
}

bool IsGlanceEnabled() {
  return base::FeatureList::IsEnabled(kKoanGlance);
}

bool IsRestoreTabsOnDemandEnabled() {
  return base::FeatureList::IsEnabled(kKoanRestoreTabsOnDemand);
}

bool IsMediaControlsEnabled() {
  return base::FeatureList::IsEnabled(kKoanMediaControls);
}

bool IsDownloadAnimationEnabled() {
  return base::FeatureList::IsEnabled(kKoanDownloadAnimation);
}

bool IsWindowDragEnabled() {
  return base::FeatureList::IsEnabled(kKoanWindowDrag);
}

bool IsWelcomeEnabled() {
  return base::FeatureList::IsEnabled(kKoanWelcome);
}

bool IsNewTabEnabled() {
  return base::FeatureList::IsEnabled(kKoanNewTab);
}

bool IsGlanceActivationClick(bool ctrl, bool shift, bool alt, bool meta) {
  return IsGlanceEnabled() &&
         IsGlanceTriggerClick(GetGlanceTrigger(), ctrl, shift, alt, meta);
}

bool IsCommandPaletteEnabled() {
  return base::FeatureList::IsEnabled(kKoanCommandPalette);
}

bool IsUrlBarEnabled() {
  return base::FeatureList::IsEnabled(kKoanUrlBar);
}

bool IsPinnedTabsEnabled() {
  return base::FeatureList::IsEnabled(kKoanPinnedTabs);
}

bool IsLibraryEnabled() {
  return base::FeatureList::IsEnabled(kKoanLibrary);
}

bool IsFoldersEnabled() {
  return base::FeatureList::IsEnabled(kKoanFolders);
}

}  // namespace koan::features
