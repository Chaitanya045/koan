// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_feature_overrides.h"

#include "base/command_line.h"
#include "build/build_config.h"
#include "chrome/browser/ui/omnibox/omnibox_next_features.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/toasts/toast_features.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/new_tab_page/composebox/variations/composebox_fieldtrial.h"
#include "chrome/common/chrome_features.h"
#include "components/compose/core/browser/compose_features.h"
#include "components/contextual_tasks/public/features.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/lens/lens_features.h"
#include "components/omnibox/browser/aim_eligibility_service_features.h"
#include "components/omnibox/common/omnibox_feature_configs.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/performance_manager/public/features.h"
#include "components/search/ntp_features.h"
#include "components/universal_optout/features.h"
#include "components/variations/feature_overrides.h"
#include "koan/browser/ui/vector_icons/koan_icons.h"
#include "third_party/blink/public/common/features_generated.h"
#include "ui/base/ui_base_features.h"

#if BUILDFLAG(IS_MAC)
#include "components/os_crypt/common/os_crypt_switches.h"
#endif

namespace koan {

void RegisterFeatureOverrides(variations::FeatureOverrides& overrides) {
  // The earliest browser-process hook: Zen's icons for every Chromium icon
  // that has one (koan_icons.h).
  InstallVectorIconOverride();

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

  // Every route to a browser command (the menu bar, shortcuts,
  // chrome::ExecuteCommand, toolbar buttons) runs the window's actions, which
  // Koan's command policy re-points (koan_command_policy.cc: the sidebar's
  // commands, the Spaces' tab group commands, the URL bar's new tab, the
  // Glance's page commands, Esc). On by default upstream; set here so that a
  // study can't bring back the legacy command switch. (A command-line switch
  // still can; the policy's legacy hook and the state controller's hook keep
  // things safe then.)
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
  // Chromium offers to turn its glass frame on or off with a promo; Koan's
  // is Zen's, on, with a setting.
  overrides.DisableFeature(feature_engagement::kIPHGlassFrameOptInFeature);
  overrides.DisableFeature(feature_engagement::kIPHGlassFrameOptOutFeature);
  // Zen's translucent window: Chromium's glass frame, which koan_window_glass.h
  // makes work for every window. On by default (the glass_frame.enabled
  // setting), off while macOS's Reduce Transparency is on.
  overrides.EnableFeature(::features::kGlassFrame);
#endif
  // The omnibox lockdown (.agent/notes/upstream/tab-groups-omnibox-
  // fullscreen.md, section 2). Koan's builds have no field trials, so every
  // upstream omnibox feature runs at its default unless it is set here.
  //
  // O1: the suggestions popup is Chromium's classic Views one, which Koan's
  // row hooks style (koan_omnibox_rows.h). M158 defaults to a WebUI popup
  // that they never reach, with Google's AI Mode chip, entity thumbnails and
  // an "add context" button. omnibox_popup state: see O15 in
  // KoanUrlBarController.
  overrides.DisableFeature(omnibox::internal::kWebUIOmniboxPopup);
  overrides.DisableFeature(omnibox::internal::kWebUIOmniboxAimPopup);
  overrides.DisableFeature(omnibox::internal::kWebUIOmniboxFullPopup);
  overrides.DisableFeature(omnibox::internal::kWebUIOmniboxSimplification);

  // O5: Zen has no Google AI. kAimEnabled is the switch the omnibox's AI Mode
  // eligibility checks (the page action, the chip, the starter pack, the NTP's
  // composebox); Lens, Gemini (Glic), contextual tasks and the NTP's AI
  // features go with it, and with model execution off Chromium's on-device
  // model service never starts (its GPU probe logs "Unable to get gpu
  // adapter").
  overrides.DisableFeature(omnibox::kAimEnabled);
  // Belt and braces for what kAimEnabled already gates: the @aimode starter
  // pack and the third-party AI Mode entry point (the AI Mode row of the "@"
  // list, a chip or button in the bar or its suggestions).
  overrides.DisableFeature(omnibox::kAiModeStartPack);
  // "Ask Google about this page": the contextual-search provider's Lens entry
  // point row on a focused, empty box. It was already out of reach with
  // kLensOverlay off (no Lens controller, IsLensEnabled() false); off here
  // too, so it does not depend on that.
  overrides.DisableFeature(omnibox_feature_configs::ContextualSearch::
                               kOmniboxContextualSearchOnFocusSuggestions);
  overrides.DisableFeature(omnibox::kAim3pEntrypoint);
  // The Gemini starter pack (@gemini): Settings > Search engines lists it
  // (SearchEnginesHandler::GetCategorizedTemplateUrls() hides it only without
  // this feature or with browser.gemini_settings = 1), and the omnibox's "@"
  // list offers it (AutocompleteProviderClient::IsGeminiStarterPackEnabled()).
  // The feature does nothing else.
  overrides.DisableFeature(omnibox::kStarterPackExpansion);
  overrides.DisableFeature(lens::features::kLensOverlay);
  overrides.DisableFeature(lens::features::kLensOverlayOmniboxEntryPoint);
  overrides.DisableFeature(::features::kGlic);
  overrides.DisableFeature(::features::kGlicRollout);
  overrides.DisableFeature(contextual_tasks::kContextualTasks);
  overrides.DisableFeature(ntp_features::kNtpNextFeatures);
  overrides.DisableFeature(ntp_composebox::kNtpComposebox);
  // No side panel that opens by itself on a new tab page.
  overrides.DisableFeature(ntp_features::kNtpCustomizeChromeAutoOpen);
  overrides.DisableFeature(
      optimization_guide::features::kOptimizationGuideModelExecution);
  // Z36: "Help me write" (Compose) in the context menu of text fields, off
  // directly, not only through the model execution above.
  overrides.DisableFeature(compose::features::kEnableCompose);

  // Z24: Global Privacy Control, on by default as in Zen
  // (privacy.globalprivacycontrol.enabled): the navigator.globalPrivacyControl
  // API, and the setting (universal_optout.enabled, on in koan_prefs.cc) that
  // sends Sec-GPC and has a row in Settings > Privacy.
  overrides.EnableFeature(blink::features::kGlobalPrivacyControlApi);
  overrides.EnableFeature(universal_optout::features::kUniversalOptOutSettings);

  // Z21: no "Link copied" / "Image copied" toast after the context menu's
  // Copy Link Address, Copy Image, Copy Video Frame or Copy Link to
  // Highlight, or upstream's Copy URL command: Firefox and Zen confirm none
  // of these. (Koan's own Copy URL, Cmd+Shift+C, shows Zen's "Copied current
  // URL!" toast; koan_url_bar_controller.cc.)
  overrides.DisableFeature(toast_features::kLinkCopiedToast);
  overrides.DisableFeature(toast_features::kImageCopiedToast);
  overrides.DisableFeature(toast_features::kVideoFrameCopiedToast);
  overrides.DisableFeature(toast_features::kLinkToHighlightCopiedToast);

  // O13: Zen has no in-product help, and none for the features Koan
  // repurposes. (Code that asks for a promo before the browser has finished
  // starting still gets one line in the log, "Attempting to show IPH ...;
  // IPH will not be shown": BrowserUserEducationInterfaceImpl logs that
  // before it looks at whether the feature is on. So these switch off what
  // is shown, not that line.) The features: vertical tabs (the sidebar), split
  // view, side panels, and Google's AI entry points. (The tab groups' ones,
  // for focus mode, are with Spaces above.)
  namespace fe = feature_engagement;
  for (const base::Feature* iph : {
           &fe::kIPHVerticalTabsExpandOnHoverFeature,
           &fe::kIPHVerticalTabstripTutorialFeature,
           &fe::kIPHSplitViewHorizontalIndirectAccessFeature,
           &fe::kIPHSidePanelGenericPinnableFeature,
           &fe::kIPHSidePanelLensOverlayPinnableFeature,
           &fe::kIPHSidePanelLensOverlayPinnableFollowupFeature,
           &fe::kIPHSidePanelContextualTasksPinnableFeature,
           &fe::kIPHGlicPromoFeature,
           &fe::kIPHLensOverlayFeature,
           &fe::kIPHOmniboxAskGLensIconFeature,
           &fe::kIPHOmniboxEverywhereLensPromoFeature,
           &fe::kIPHExtensionsZeroStatePromoFeature,
       }) {
    overrides.DisableFeature(*iph);
  }
}

}  // namespace koan
