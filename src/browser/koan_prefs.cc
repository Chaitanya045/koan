// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/koan_prefs.h"

#include "components/pref_registry/pref_registry_syncable.h"

namespace koan::prefs {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kCompactModeEnabled, false);
}

}  // namespace koan::prefs
