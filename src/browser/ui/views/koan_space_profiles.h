// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback_forward.h"
#include "ui/color/color_id.h"

class Profile;

// Settings > Tab Management > Profiles (#477,
// .agent/notes/upstream/space-containers.md "Managing profiles"): the user's
// profiles (Zen's containers per Space, koan_space_profile_menu.h), what uses
// them, and renaming, recolouring, clearing and deleting them. `profile` is
// the regular profile the settings page is in; for any other these list
// nothing and change nothing. A profile is named by its Serialize()d id
// (koan/browser/containers/koan_container_partition.h).
namespace koan {

struct SpaceProfileInfo {
  SpaceProfileInfo();
  SpaceProfileInfo(const SpaceProfileInfo&);
  SpaceProfileInfo& operator=(const SpaceProfileInfo&);
  ~SpaceProfileInfo();

  std::string id;
  std::string name;
  // A tab_groups::TabGroupColorId, one of GetSpaceProfileColors().
  int color = 0;
  // The Spaces that use it, by name, each once, in sidebar order.
  std::vector<std::u16string> spaces;
  // Its open tabs, in every window, Essentials too (not the Spaces' hidden
  // empty tabs).
  int open_tabs = 0;
};

// The profiles, in the order they were made.
std::vector<SpaceProfileInfo> GetSpaceProfiles(Profile* profile);

// The colours a profile can have, in Firefox's order, with Firefox's names
// and the colour of their dot (as in the Set Profile menu).
struct SpaceProfileColor {
  int color;
  const char* name;
  ui::ColorId dot;
};
std::vector<SpaceProfileColor> GetSpaceProfileColors();

// How much each profile keeps on disk, in bytes, by id; runs `done`
// asynchronously.
void GetSpaceProfileSizes(
    Profile* profile,
    base::OnceCallback<void(base::flat_map<std::string, int64_t>)> done);

// Rename and change colour: every window follows (the colour line, the Set
// Profile menu, the Create-a-Space form). False for a refused name or colour,
// or a profile the list doesn't have.
bool RenameSpaceProfile(Profile* profile,
                        const std::string& id,
                        const std::u16string& name);
bool SetSpaceProfileColor(Profile* profile, const std::string& id, int color);

// Clear this profile's data: signs out of everything in its Spaces. Its tabs
// stay open. Runs `done` when it is cleared; false (and never runs `done`)
// for a profile the list doesn't have.
bool ClearSpaceProfileData(Profile* profile,
                           const std::string& id,
                           base::OnceClosure done);

// Delete profile...: in every window, the Spaces that use it go back to
// Default (Zen's rule when a container is deleted) and its tabs close
// (Firefox's), then it leaves the list and its data is deleted, folder and
// all (koan::containers::DeleteUserContainerData()). Nothing of another
// profile or of the default partition is touched. Runs `done` when the data
// is gone; false (and nothing done) for a profile the list doesn't have.
bool DeleteSpaceProfile(Profile* profile,
                        const std::string& id,
                        base::OnceClosure done);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILES_H_
