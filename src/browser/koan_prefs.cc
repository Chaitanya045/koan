// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_prefs.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/universal_optout/prefs.h"
#include "koan/browser/agent/koan_agent_limits.h"
#include "koan/browser/koan_features.h"

namespace koan::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kCompactModeEnabled, false);
  registry->RegisterStringPref(kCompactModeHide, kCompactModeHideSidebar);
  registry->RegisterBooleanPref(kCompactModeToolbarFlashPopup, false);
  registry->RegisterBooleanPref(kCompactModeDragCollapseHintSeen, false);
  registry->RegisterBooleanPref(kTabsOnRight, false);
  registry->RegisterBooleanPref(kShowNewTabVertical, true);
  registry->RegisterBooleanPref(kSingleToolbarEnabled, true);
  registry->RegisterStringPref(kUrlBarBehavior, kUrlBarBehaviorFloatingOnType);
  registry->RegisterBooleanPref(kUrlBarReplaceNewTab, true);
  registry->RegisterBooleanPref(kUrlBarShowCopyUrl, true);
  registry->RegisterBooleanPref(kWorkspaceTint, true);
  registry->RegisterIntegerPref(kDarkThemeStyle, 0);
  registry->RegisterBooleanPref(kShowCustomColors, false);
  registry->RegisterBooleanPref(kWorkspacesWrapAround, true);
  registry->RegisterBooleanPref(kWorkspacesNaturalScroll, false);
  registry->RegisterStringPref(kWorkspacesScrollModifierKey, "ctrl");
  registry->RegisterIntegerPref(kWorkspaceTintApplied, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedColor, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedVariant, 0);
  registry->RegisterListPref(kWorkspaceList);
  registry->RegisterBooleanPref(kWindowSyncEnabled, true);
  registry->RegisterBooleanPref(kWindowSyncOnlyPinned, true);
  registry->RegisterDictionaryPref(kWindowSyncState);
  registry->RegisterBooleanPref(kWindowSyncOpenLinksInBlankWindow, true);
  registry->RegisterBooleanPref(kGlanceEnabled, true);
  registry->RegisterStringPref(kGlanceActivationMethod, "alt");
  registry->RegisterBooleanPref(kGlanceOpenPinnedExternalLinks, true);
  registry->RegisterBooleanPref(kGlanceContextMenuSearch, true);
  registry->RegisterDictionaryPref(kShortcuts);
  registry->RegisterBooleanPref(kSelectRecentlyUsedOnClose, true);
  registry->RegisterBooleanPref(kCloseOnBackWithNoHistory, true);
  registry->RegisterBooleanPref(kOpenPinnedInNewTab, true);
  registry->RegisterBooleanPref(kCtrlTabWithinKind, false);
  registry->RegisterBooleanPref(kCtrlTabSkipPending, false);
  registry->RegisterBooleanPref(kCommandPaletteSuggestions, true);
  registry->RegisterDictionaryPref(kCommandPaletteUsage);
  registry->RegisterStringPref(kPinnedTabCloseShortcutBehavior,
                               "reset-unload-switch");
  registry->RegisterBooleanPref(kPinnedTabRestoreToPinnedUrl, false);
  registry->RegisterListPref(kFolderList);
  registry->RegisterStringPref(kLibraryLastSection, "history");
  registry->RegisterBooleanPref(kLibraryHistoryClosedTabs, false);
  registry->RegisterBooleanPref(kMediaControlsEnabled, true);
  registry->RegisterBooleanPref(kDownloadAnimation, true);
  registry->RegisterBooleanPref(kDragWindowFromContent, true);
  registry->RegisterListPref(kSpaceRoutes);
  registry->RegisterStringPref(kSpaceRoutingExternalDefault, "");
  registry->RegisterBooleanPref(kRenameTabs, true);
  registry->RegisterBooleanPref(kLoadingIndicator, true);
  registry->RegisterBooleanPref(kHideTabThrobber, true);
  registry->RegisterBooleanPref(kBookmarksBarShow, false);
  registry->RegisterStringPref(kNewTabPage, kNewTabPageKoan);
  registry->RegisterBooleanPref(kWelcomeScreenSeen, false);
  registry->RegisterIntegerPref(kResumedCrashes, 0);
  registry->RegisterTimePref(kLastResumedTime, base::Time());
  registry->RegisterDictionaryPref(kAgentSavedContainers);
  registry->RegisterBooleanPref(kAgentControlStrict, false);
}

bool AgentReadyByDefault() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(::switches::kIncognito) ||
      command_line.HasSwitch(::switches::kGuest)) {
    return false;
  }
  base::FilePath user_data_dir;
  base::FilePath default_dir;
  return base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir) &&
         chrome::GetDefaultUserDataDirectory(&default_dir) &&
         user_data_dir == default_dir;
}

// Sets Koan's defaults for upstream profile prefs (Zen's search suggestion
// and AI settings). Must run after the owning components registered them, at
// the end of chrome::RegisterProfilePrefs() (crash note 8).
void OverrideUpstreamProfilePrefDefaults(
    user_prefs::PrefRegistrySyncable* registry) {
  // chrome/common/pref_names.h kSearchSuggestEnabled,
  // components/omnibox/browser/omnibox_pref_names.h kShowAiModeOmniboxButton
  // and kAIModeSettings (1: AI Mode disallowed). Written out: these headers'
  // targets are not dependencies of this one.
  registry->SetDefaultPrefValue("search.suggest_enabled", base::Value(false));
  registry->SetDefaultPrefValue("omnibox.show_ai_mode_omnibox_button",
                                base::Value(false));
  registry->SetDefaultPrefValue("omnibox.ai_mode_settings", base::Value(1));
  // Z24: Global Privacy Control on (Zen's
  // privacy.globalprivacycontrol.enabled): Sec-GPC and
  // navigator.globalPrivacyControl, with the settings row that
  // kUniversalOptOutSettings shows (koan_feature_overrides.cc).
  registry->SetDefaultPrefValue(
      universal_optout::prefs::kUniversalOptOutEnabled, base::Value(true));
  // A new window's width when it isn't restored (BrowserWindowFeatures). The
  // pref is registered by tabs::RegisterProfilePrefs().
  if (features::IsSidebarEnabled()) {
    registry->SetDefaultPrefValue(::prefs::kVerticalTabsUncollapsedWidth,
                                  base::Value(kSidebarDefaultWidth));
  }
}

void OverrideServiceProfilePrefDefaults(
    user_prefs::PrefRegistrySyncable* registry) {
  // Z35: no "Sign in to Koan?" bubble after a Google sign-in on the web
  // (registered by DiceWebSigninInterceptorFactory). Zen has no browser
  // account promos.
  registry->SetDefaultPrefValue(::prefs::kSigninInterceptionEnabled,
                                base::Value(false));
  // Z15: no browser sign-in (registered by AccountConsistencyModeManager-
  // Factory; kSigninAllowed follows it at the next start). Koan has no Google
  // sync, and Zen no such account rows: the app menu's "Sign in to Koan" and
  // sync rows, the avatar menu's and the bookmark bubble's sign-in promos go.
  // Signing in to Google on the web still works. Settings' sign-in switch
  // turns it back on.
  registry->SetDefaultPrefValue(::prefs::kSigninAllowedOnNextStartup,
                                base::Value(false));
}

// Registers Koan's local-state prefs, and sets Koan's defaults for upstream
// ones.
void RegisterLocalStatePrefDefaults(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kLookStyle, "zen");
  registry->RegisterStringPref(kLookMode, "dark");
  registry->RegisterStringPref(kLookBase, "black");
  registry->RegisterStringPref(kLookAccent, "none");
  registry->RegisterStringPref(kLookCustomCss, "");
  registry->RegisterBooleanPref(kLookAcrylicUrlBar, false);
  registry->RegisterBooleanPref(kHapticFeedback, true);
  registry->RegisterBooleanPref(kUpdatesCheckEnabled, true);
  registry->RegisterTimePref(kUpdatesLastCheck, base::Time());
  registry->RegisterStringPref(kUpdatesLatestCommit, "");
  registry->RegisterTimePref(kUpdatesLatestCommitTime, base::Time());
  registry->RegisterStringPref(kUpdatesNotifiedCommit, "");
  registry->RegisterBooleanPref(kUpdatesShowUpdateNotification, true);
  registry->RegisterStringPref(kUpdatesLastBuild, "");
  registry->RegisterBooleanPref(kAgentControlEnabled, AgentReadyByDefault());
  registry->RegisterIntegerPref(kAgentControlMaxLeasedTabs,
                                agent::kLeasedTabsLimit.default_value);
  registry->RegisterBooleanPref(kAgentControlLowPowerHalves, true);
  registry->RegisterIntegerPref(kAgentControlMaxPromptsPerMinute,
                                agent::kPromptsLimit.default_value);
  registry->RegisterIntegerPref(kAgentControlMaxAuthFailures,
                                agent::kFailuresLimit.default_value);
  namespace pm = performance_manager::user_tuning::prefs;
  // Memory Saver: discard tabs that have been hidden for 4 hours (the
  // balanced setting) until they are opened again. Chromium ships it off.
  registry->SetDefaultPrefValue(
      pm::kMemorySaverModeState,
      base::Value(static_cast<int>(pm::MemorySaverModeState::kEnabled)));
}

}  // namespace koan::prefs
