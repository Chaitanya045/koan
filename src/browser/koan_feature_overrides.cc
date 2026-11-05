// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_feature_overrides.h"

#include "base/command_line.h"
#include "build/build_config.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/ui_features.h"
#include "components/feature_engagement/public/feature_constants.h"
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
  // Upstream's tab group promos have no place with Spaces: "Focus" and
  // "Unfocus" a group (Koan hides the group header and the Unfocus pill they
  // point at), and saving groups (Spaces aren't saved groups).
  overrides.DisableFeature(feature_engagement::kIPHTabGroupsFocusingFeature);
  overrides.DisableFeature(feature_engagement::kIPHTabGroupsUnfocusingFeature);
  overrides.DisableFeature(feature_engagement::kIPHTabGroupsSaveV2IntroFeature);
  overrides.DisableFeature(
      feature_engagement::kIPHTabGroupsSaveV2CloseGroupFeature);
  // Zen closes a tab with no history on Back and returns to its opener;
  // upstream's back-to-opener does exactly that. Switched by
  // prefs::kCloseOnBackWithNoHistory (koan_tab_hooks.h).
  overrides.EnableFeature(tabs::kBackToOpener);

  // The sidebar is Koan's (KoanVerticalTabsPolicy): the commands that reach
  // upstream's vertical tab strip state run the window's actions, which Koan
  // re-points (also the Spaces' tab group commands and the URL bar's new
  // tab), so the legacy command switch (which would not go through them)
  // must not be what runs. On by default upstream; set here so that a
  // study can't turn it off. (A command-line switch still can; the state
  // controller's hook keeps the mode and collapse state safe then.)
  overrides.EnableFeature(features::kUseActionsForBrowserCommands);
  // Upstream's hints about expand-on-hover (shown after any collapse, which
  // compact mode does) and the vertical tabs tutorial and badge: Koan has no
  // such choice to explain.
  overrides.DisableFeature(
      feature_engagement::kIPHVerticalTabsExpandOnHoverFeature);
  overrides.DisableFeature(
      feature_engagement::kIPHVerticalTabstripTutorialFeature);
  overrides.DisableFeature(tabs::kVerticalTabsNewBadge);
  // Startup bars Zen doesn't have: "Koan will reopen your tabs" (Koan makes
  // continue-where-you-left-off the default, which is exactly its trigger) and
  // the PDF-viewer prompt. (The default-browser and pin prompts are gated in
  // infobar_utils.cc; Zen asks once, in its welcome.)
  overrides.DisableFeature(features::kSessionRestoreInfobar);
#if BUILDFLAG(IS_MAC)
  overrides.DisableFeature(features::kPdfInfoBar);
  overrides.DisableFeature(features::kSeparateDefaultAndPinPrompt);
#endif

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
