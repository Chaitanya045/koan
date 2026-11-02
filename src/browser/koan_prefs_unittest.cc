// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_prefs.h"

#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

// The defaults follow Zen's (zen-default-prefs.js); the pref names are what
// profiles on disk and chrome://koan-settings use, so they must not change.
TEST(KoanPrefsTest, ProfilePrefDefaults) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  prefs::RegisterProfilePrefs(prefs.registry());

  EXPECT_FALSE(prefs.GetBoolean(prefs::kCompactModeEnabled));
  EXPECT_EQ(prefs::kCompactModeHideSidebar,
            prefs.GetString(prefs::kCompactModeHide));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kCompactModeToolbarFlashPopup));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kCompactModeDragCollapseHintSeen));
  EXPECT_EQ(prefs::kSidebarCollapsedBeforeUnknown,
            prefs.GetInteger(prefs::kCompactModeSidebarCollapsedBefore));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kTabsOnRight));
  EXPECT_TRUE(prefs.GetBoolean(prefs::kSingleToolbarEnabled));

  EXPECT_EQ(prefs::kUrlBarBehaviorFloatingOnType,
            prefs.GetString(prefs::kUrlBarBehavior));
  EXPECT_TRUE(prefs.GetBoolean(prefs::kUrlBarReplaceNewTab));
  EXPECT_TRUE(prefs.GetBoolean(prefs::kUrlBarShowCopyUrl));

  EXPECT_TRUE(prefs.GetBoolean(prefs::kWorkspaceTint));
  EXPECT_EQ(0, prefs.GetInteger(prefs::kWorkspaceTintApplied));
  EXPECT_EQ(0, prefs.GetInteger(prefs::kWorkspaceTintSavedColor));
  EXPECT_EQ(0, prefs.GetInteger(prefs::kWorkspaceTintSavedVariant));

  EXPECT_TRUE(prefs.GetBoolean(prefs::kGlanceEnabled));
  EXPECT_EQ("alt", prefs.GetString(prefs::kGlanceActivationMethod));

  EXPECT_TRUE(prefs.GetDict(prefs::kShortcuts).empty());

  EXPECT_TRUE(prefs.GetBoolean(prefs::kSelectRecentlyUsedOnClose));
  EXPECT_TRUE(prefs.GetBoolean(prefs::kCloseOnBackWithNoHistory));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kCtrlTabWithinKind));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kCtrlTabSkipPending));

  EXPECT_TRUE(prefs.GetBoolean(prefs::kCommandPaletteSuggestions));
  EXPECT_TRUE(prefs.GetDict(prefs::kCommandPaletteUsage).empty());

  EXPECT_EQ("reset-unload-switch",
            prefs.GetString(prefs::kPinnedTabCloseShortcutBehavior));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kPinnedTabRestoreToPinnedUrl));
}

TEST(KoanPrefsTest, ProfilePrefsTakeUserValues) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  prefs::RegisterProfilePrefs(prefs.registry());

  prefs.SetBoolean(prefs::kCompactModeEnabled, true);
  prefs.SetString(prefs::kUrlBarBehavior, prefs::kUrlBarBehaviorFloat);
  EXPECT_TRUE(prefs.GetBoolean(prefs::kCompactModeEnabled));
  EXPECT_EQ(prefs::kUrlBarBehaviorFloat,
            prefs.GetString(prefs::kUrlBarBehavior));
}

TEST(KoanPrefsTest, ProfilePrefsAreInKoanNamespace) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  prefs::RegisterProfilePrefs(prefs.registry());

  // A Koan pref never shadows an upstream one: all names start with "koan.",
  // so the tree of values has "koan" as its only root.
  const base::DictValue values =
      prefs.GetPreferenceValues(PrefService::INCLUDE_DEFAULTS);
  EXPECT_EQ(1u, values.size());
  EXPECT_TRUE(values.contains("koan"));
}

TEST(KoanPrefsTest, MemorySaverIsOnByDefault) {
  TestingPrefServiceSimple local_state;
  performance_manager::user_tuning::prefs::RegisterLocalStatePrefs(
      local_state.registry());
  EXPECT_EQ(
      static_cast<int>(performance_manager::user_tuning::prefs::
                           MemorySaverModeState::kDisabled),
      local_state.GetInteger(
          performance_manager::user_tuning::prefs::kMemorySaverModeState));

  prefs::RegisterLocalStatePrefDefaults(local_state.registry());
  EXPECT_EQ(
      static_cast<int>(performance_manager::user_tuning::prefs::
                           MemorySaverModeState::kEnabled),
      local_state.GetInteger(
          performance_manager::user_tuning::prefs::kMemorySaverModeState));

  // A value the user sets still wins.
  local_state.SetInteger(
      performance_manager::user_tuning::prefs::kMemorySaverModeState,
      static_cast<int>(performance_manager::user_tuning::prefs::
                           MemorySaverModeState::kDisabled));
  EXPECT_EQ(
      static_cast<int>(performance_manager::user_tuning::prefs::
                           MemorySaverModeState::kDisabled),
      local_state.GetInteger(
          performance_manager::user_tuning::prefs::kMemorySaverModeState));
}

}  // namespace koan
