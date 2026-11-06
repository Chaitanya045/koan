// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_prefs.h"

#include "base/values.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"

namespace koan::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kCompactModeEnabled, false);
  registry->RegisterStringPref(kCompactModeHide, kCompactModeHideSidebar);
  registry->RegisterBooleanPref(kCompactModeToolbarFlashPopup, false);
  registry->RegisterBooleanPref(kCompactModeDragCollapseHintSeen, false);
  registry->RegisterBooleanPref(kTabsOnRight, false);
  registry->RegisterBooleanPref(kSingleToolbarEnabled, true);
  registry->RegisterStringPref(kUrlBarBehavior, kUrlBarBehaviorFloatingOnType);
  registry->RegisterBooleanPref(kUrlBarReplaceNewTab, true);
  registry->RegisterBooleanPref(kUrlBarShowCopyUrl, true);
  registry->RegisterBooleanPref(kWorkspaceTint, true);
  registry->RegisterIntegerPref(kWorkspaceTintApplied, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedColor, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedVariant, 0);
  registry->RegisterListPref(kWorkspaceList);
  registry->RegisterBooleanPref(kGlanceEnabled, true);
  registry->RegisterStringPref(kGlanceActivationMethod, "alt");
  registry->RegisterDictionaryPref(kShortcuts);
  registry->RegisterBooleanPref(kSelectRecentlyUsedOnClose, true);
  registry->RegisterBooleanPref(kCloseOnBackWithNoHistory, true);
  registry->RegisterBooleanPref(kCtrlTabWithinKind, false);
  registry->RegisterBooleanPref(kCtrlTabSkipPending, false);
  registry->RegisterBooleanPref(kCommandPaletteSuggestions, true);
  registry->RegisterDictionaryPref(kCommandPaletteUsage);
  registry->RegisterStringPref(kPinnedTabCloseShortcutBehavior,
                               "reset-unload-switch");
  registry->RegisterBooleanPref(kPinnedTabRestoreToPinnedUrl, false);
  registry->RegisterStringPref(kLibraryLastSection, "history");
  registry->RegisterBooleanPref(kLibraryHistoryClosedTabs, false);
  registry->RegisterBooleanPref(kMediaControlsEnabled, true);
  registry->RegisterBooleanPref(kDownloadAnimation, true);
  registry->RegisterBooleanPref(kDragWindowFromContent, true);
  registry->RegisterBooleanPref(kBookmarksBarShow, false);
  registry->RegisterStringPref(kNewTabPage, kNewTabPageKoan);
  registry->RegisterBooleanPref(kWelcomeScreenSeen, false);
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
}

// Registers Koan's local-state prefs, and sets Koan's defaults for upstream
// ones.
void RegisterLocalStatePrefDefaults(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kLookStyle, "zen");
  registry->RegisterStringPref(kLookMode, "dark");
  registry->RegisterStringPref(kLookBase, "black");
  registry->RegisterStringPref(kLookAccent, "none");
  registry->RegisterStringPref(kLookCustomCss, "");
  namespace pm = performance_manager::user_tuning::prefs;
  // Memory Saver: discard tabs that have been hidden for 4 hours (the
  // balanced setting) until they are opened again. Chromium ships it off.
  registry->SetDefaultPrefValue(
      pm::kMemorySaverModeState,
      base::Value(static_cast<int>(pm::MemorySaverModeState::kEnabled)));
}

}  // namespace koan::prefs
