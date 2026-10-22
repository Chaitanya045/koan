// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_theme_store.h"

#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "koan/browser/koan_prefs.h"

namespace koan {

namespace {

constexpr char kDefaultSpaceKey[] = "default";
constexpr char kSessionSpacePrefix[] = "session:";

}  // namespace

KoanSpaceThemeStore::KoanSpaceThemeStore(Profile* profile)
    : profile_(profile) {}

KoanSpaceThemeStore::~KoanSpaceThemeStore() = default;

std::string KoanSpaceThemeStore::SpaceKey(Space space) const {
  if (!space) {
    return kDefaultSpaceKey;
  }
  if (auto* service =
          tab_groups::TabGroupSyncServiceFactory::GetForProfile(profile_)) {
    if (std::optional<tab_groups::SavedTabGroup> group =
            service->GetGroup(*space)) {
      return group->saved_guid().AsLowercaseString();
    }
  }
  return base::StrCat({kSessionSpacePrefix, space->ToString()});
}

base::DictValue KoanSpaceThemeStore::GetStoredTheme(Space space) const {
  const std::string key = SpaceKey(space);
  if (key.starts_with(kSessionSpacePrefix)) {
    const auto it = session_themes_.find(key);
    return it == session_themes_.end() ? base::DictValue()
                                       : it->second.ToDict();
  }
  const base::DictValue* dict =
      profile_->GetPrefs()->GetDict(prefs::kSpaceThemes).FindDict(key);
  return dict ? dict->Clone() : base::DictValue();
}

bool KoanSpaceThemeStore::SetSpaceTheme(Space space,
                                        const theme::SpaceTheme& theme) {
  const std::string key = SpaceKey(space);
  if (key.starts_with(kSessionSpacePrefix)) {
    session_themes_[key] = theme;
    return true;
  }
  ScopedDictPrefUpdate(profile_->GetPrefs(), prefs::kSpaceThemes)
      ->Set(key, theme.ToDict());
  return false;
}

}  // namespace koan
