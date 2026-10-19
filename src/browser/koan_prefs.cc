// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_prefs.h"

#include "components/pref_registry/pref_registry_syncable.h"

namespace koan::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kCompactModeEnabled, false);
  registry->RegisterBooleanPref(kSingleToolbarEnabled, true);
  registry->RegisterBooleanPref(kWorkspaceTint, true);
  registry->RegisterIntegerPref(kWorkspaceTintApplied, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedColor, 0);
  registry->RegisterIntegerPref(kWorkspaceTintSavedVariant, 0);
  registry->RegisterDictionaryPref(kShortcuts);
}

}  // namespace koan::prefs
