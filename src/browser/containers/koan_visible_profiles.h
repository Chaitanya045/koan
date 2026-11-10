// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_VISIBLE_PROFILES_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_VISIBLE_PROFILES_H_

#include <stddef.h>

#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"

namespace koan::containers {

// ProfilePicker::GetStartupModeReason(): the profiles that count for showing
// the picker at launch. Agents' hidden profiles (strict mode,
// koan_agent_containers.h) are omitted and never do, so one left by a crash,
// or a task running, never brings up the picker.
inline size_t CountVisibleProfiles(const ProfileAttributesStorage& storage) {
  size_t count = 0;
  for (const ProfileAttributesEntry* entry :
       storage.GetAllProfilesAttributes()) {
    if (!entry->IsOmitted()) {
      ++count;
    }
  }
  return count;
}

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_VISIBLE_PROFILES_H_
