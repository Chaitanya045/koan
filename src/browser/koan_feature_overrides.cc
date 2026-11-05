// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_feature_overrides.h"

#include "base/command_line.h"
#include "build/build_config.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/ui_features.h"
#include "components/performance_manager/public/features.h"
#include "components/variations/feature_overrides.h"

#if BUILDFLAG(IS_MAC)
#include "components/os_crypt/common/os_crypt_switches.h"
#endif

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
  // Its once-a-minute memory measurement only feeds that discard. (The CPU
  // measurement stays: energy saver's freezing of busy tabs needs it.)
  overrides.DisableFeature(
      performance_manager::features::kMemoryMeasurementInFreezingPolicy);

#if BUILDFLAG(IS_MAC)
  // "Relaunch the browser to load your profile data and keep it encrypted":
  // shown when the encryption key can't be had. With --use-mock-keychain
  // (scripts/run_mac.sh, tests) the key never can: the async key provider
  // then asks a fake keychain, which refuses an item without its access group
  // (errSecMissingEntitlement). That is no problem of the profile, so no bar.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          os_crypt::switches::kUseMockKeychain)) {
    overrides.DisableFeature(features::kOSCryptAsyncAvailabilityInfoBar);
  }
#endif
}

}  // namespace koan
