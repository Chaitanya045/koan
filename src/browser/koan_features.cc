// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_features.h"

#include <string>

#include "base/command_line.h"
#include "base/no_destructor.h"
#include "koan/browser/koan_switches.h"

namespace koan::features {

BASE_FEATURE(kKoanSidebar, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanCompactMode, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWorkspaces, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanGlance, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanRestoreTabsOnDemand, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanCommandPalette, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanUrlBar, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanPinnedTabs, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanMediaControls, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanDownloadAnimation, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWindowDrag, base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kKoanWelcome, base::FEATURE_ENABLED_BY_DEFAULT);

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

bool IsGlanceActivationClick(bool ctrl, bool shift, bool alt, bool meta) {
  if (ctrl + shift + alt + meta != 1 || !IsGlanceEnabled()) {
    return false;
  }
  // The command line doesn't change, so parse it once.
  static const base::NoDestructor<std::string> parsed(
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kGlanceActivationMethod));
  const std::string& method = *parsed;
  if (method == switches::kGlanceOff) {
    return false;
  }
  if (method == "ctrl") {
    return ctrl;
  }
  if (method == "shift") {
    return shift;
  }
  if (method == "meta") {
    return meta;
  }
  // Zen's default.
  return alt;
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

}  // namespace koan::features
