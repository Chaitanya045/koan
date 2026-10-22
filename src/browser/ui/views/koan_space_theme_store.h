// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_THEME_STORE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_THEME_STORE_H_

#include <map>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "koan/browser/ui/views/koan_space_theme.h"

class Profile;

namespace koan {

// Where each Space's theme lives: the profile pref koan.spaces.themes, keyed
// by the Space's saved tab group GUID, which survives restarts (tab group ids
// don't); "default" is the default Space ("All tabs"). A Space without a
// saved group (yet) keeps its theme for the session. The one place Space
// themes are read and written.
class KoanSpaceThemeStore {
 public:
  using Space = std::optional<tab_groups::TabGroupId>;

  explicit KoanSpaceThemeStore(Profile* profile);
  KoanSpaceThemeStore(const KoanSpaceThemeStore&) = delete;
  KoanSpaceThemeStore& operator=(const KoanSpaceThemeStore&) = delete;
  ~KoanSpaceThemeStore();

  // The theme as stored (a dict with its family; see theme::GetTokens()),
  // empty for the default theme.
  base::DictValue GetStoredTheme(Space space) const;
  // Returns whether the theme is only kept for the session (no pref change
  // tells the windows then).
  bool SetSpaceTheme(Space space, const theme::SpaceTheme& theme);

 private:
  std::string SpaceKey(Space space) const;

  const raw_ptr<Profile> profile_;
  std::map<std::string, theme::SpaceTheme> session_themes_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_THEME_STORE_H_
