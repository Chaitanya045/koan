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

namespace content {
class WebContents;
}

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
  // empty tabs; a synced Essential or pinned tab once, not each window's
  // copy).
  int open_tabs = 0;
  // Of those, its Essentials (#510: the profile's own grid) and its pinned
  // tabs, which the question names: they close too.
  int essentials = 0;
  int pinned_tabs = 0;
  // Its proxy (#445, koan_container_proxy.h): the saved server, empty for
  // none (its tabs connect as every other tab does, "Direct").
  std::string proxy;
  // Whether its tabs can't connect at all right now: --koan-container-proxy's
  // "disconnected" for this run, or a proxy whose server is only known at
  // runtime and isn't up.
  bool proxy_disconnected = false;
  // The extensions that are off in it (#473), those the Extensions... dialog
  // lists, by name. (An id of an extension that is no longer installed isn't
  // one.)
  struct OffExtension {
    std::string id;
    std::u16string name;
  };
  std::vector<OffExtension> extensions_off;
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

// The Space menu's Extensions... (ShowSpaceExtensions()) for a Space that
// uses the profile `id`, over the window of `from` (the settings page), or
// over another window with Spaces if that one has none. False (and nothing
// shown) when no Space uses it: the list is edited for a Space.
bool ShowSpaceProfileExtensions(Profile* profile,
                                content::WebContents* from,
                                const std::string& id);

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
// Default (Zen's rule when a container is deleted; their empty tabs move into
// Default at once), it leaves the list, its tabs close (Firefox's), its
// Essentials and pinned tabs too, and the notifications it shows; its visits
// and closed tabs are forgotten (ForgetSpaceProfileHistory()) and its data
// is deleted, folder and all (koan::containers::DeleteUserContainerData()).
// Its extension list (#473) was part of its entry in the list. Nothing of
// another profile or of the default partition is touched. Runs `done` when the
// data is gone; false (and nothing done) for a profile the list doesn't have.
bool DeleteSpaceProfile(Profile* profile,
                        const std::string& id,
                        base::OnceClosure done);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILES_H_
