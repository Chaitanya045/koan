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

// The pref names are what profiles on disk and Koan's settings use.
TEST(KoanPrefsTest, ProfilePrefsAreInKoanNamespace) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  prefs::RegisterProfilePrefs(prefs.registry());

  // Koan's prefs are all named "koan.<...>", so the tree of values has
  // "koan" as its only root.
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
}

}  // namespace koan
