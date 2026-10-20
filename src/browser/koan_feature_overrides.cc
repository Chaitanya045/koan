// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_feature_overrides.h"

#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/ui_features.h"
#include "components/performance_manager/public/features.h"
#include "components/variations/feature_overrides.h"

namespace koan {

void RegisterFeatureOverrides(variations::FeatureOverrides& overrides) {
  // Workspaces are tab groups shown one at a time: upstream's group focus
  // mode hides every other group and scopes tab cycling to the focused one.
  overrides.EnableFeature(features::kTabGroupsFocusing);
  // Zen closes a tab with no history on Back and returns to its opener;
  // upstream's back-to-opener does exactly that. Switched by
  // prefs::kCloseOnBackWithNoHistory (koan_tab_hooks.h).
  overrides.EnableFeature(tabs::kBackToOpener);

  // Freeze background tabs beyond the 5 most recently used; they run for
  // 5 s each minute to catch up. Pinned tabs, tabs that played audio since
  // they were last shown, and tabs that hold locks, use WebRTC, devices or
  // notifications, or are loading are never frozen (the first two are Koan
  // hooks in freezing_policy.cc). Switch:
  // chrome://flags/#koan-freeze-background-tabs.
  overrides.EnableFeature(performance_manager::features::kInfiniteTabsFreezing);
  // Upstream discards a frozen tab whose memory grows by 100 MB, whatever the
  // Memory Saver setting; with every background tab freezable that would
  // make Memory Saver no longer the switch for discarding.
  overrides.DisableFeature(performance_manager::features::
                               kDiscardFrozenBrowsingInstancesWithGrowingPMF);
}

}  // namespace koan
