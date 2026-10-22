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
  registry->RegisterIntegerPref(kCompactModeSidebarCollapsedBefore,
                                kSidebarCollapsedBeforeUnknown);
  registry->RegisterBooleanPref(kTabsOnRight, false);
  registry->RegisterBooleanPref(kSingleToolbarEnabled, true);
  registry->RegisterStringPref(kUrlBarBehavior, kUrlBarBehaviorFloatingOnType);
  registry->RegisterBooleanPref(kUrlBarReplaceNewTab, true);
  registry->RegisterBooleanPref(kUrlBarShowCopyUrl, true);
  registry->RegisterBooleanPref(kWorkspaceTint, true);
  registry->RegisterIntegerPref(kWorkspaceTintApplied, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedColor, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedVariant, 0);
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
}

void RegisterLocalStatePrefDefaults(PrefRegistrySimple* registry) {
  namespace pm = performance_manager::user_tuning::prefs;
  // Memory Saver: discard tabs that have been hidden for 4 hours (the
  // balanced setting) until they are opened again. Chromium ships it off.
  registry->SetDefaultPrefValue(
      pm::kMemorySaverModeState,
      base::Value(static_cast<int>(pm::MemorySaverModeState::kEnabled)));
}

}  // namespace koan::prefs
