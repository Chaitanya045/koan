// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_profiles.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_agent_containers.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_containers.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_page_notifications.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_space_profile_menu.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_tab_auto_closer.h"
#include "koan/browser/ui/views/koan_workspaces.h"

namespace koan {

namespace {

// Only the user's own regular profile has Spaces' profiles (none in
// Incognito, a guest window or an agent's hidden profile).
bool HasSpaceProfiles(Profile* profile) {
  return profile && !profile->IsOffTheRecord() && profile->IsRegularProfile();
}

// `id` if it is one of the profile's listed containers.
std::optional<containers::ContainerId> ListedProfile(Profile* profile,
                                                     const std::string& id) {
  if (!HasSpaceProfiles(profile)) {
    return std::nullopt;
  }
  return containers::ListedSpaceContainer(
      profile->GetPrefs()->GetList(prefs::kUserContainers), id);
}

// Firefox's names for its container colours (UserContainerColors()' order).
const char* ColorName(tab_groups::TabGroupColorId color) {
  switch (color) {
    case tab_groups::TabGroupColorId::kBlue:
      return "Blue";
    case tab_groups::TabGroupColorId::kCyan:
      return "Turquoise";
    case tab_groups::TabGroupColorId::kGreen:
      return "Green";
    case tab_groups::TabGroupColorId::kYellow:
      return "Yellow";
    case tab_groups::TabGroupColorId::kOrange:
      return "Orange";
    case tab_groups::TabGroupColorId::kRed:
      return "Red";
    case tab_groups::TabGroupColorId::kPink:
      return "Pink";
    case tab_groups::TabGroupColorId::kPurple:
      return "Purple";
    default:
      return "Colour";
  }
}

// Runs `visit` on every window of `profile` that has Spaces.
template <typename Visit>
void ForEachSpacesWindow(Profile* profile, Visit visit) {
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (KoanWorkspaceController* spaces =
                KoanWorkspaceController::FromTabStrip(
                    browser->GetTabStripModel())) {
          visit(*spaces);
        }
        return true;
      });
}

base::flat_map<std::string, int64_t> ComputeSizes(
    std::vector<std::pair<std::string, base::FilePath>> paths) {
  std::vector<std::pair<std::string, int64_t>> sizes;
  for (const auto& [id, path] : paths) {
    sizes.emplace_back(id, path.empty() ? 0 : base::ComputeDirectorySize(path));
  }
  return base::flat_map<std::string, int64_t>(std::move(sizes));
}

}  // namespace

SpaceProfileInfo::SpaceProfileInfo() = default;
SpaceProfileInfo::SpaceProfileInfo(const SpaceProfileInfo&) = default;
SpaceProfileInfo& SpaceProfileInfo::operator=(const SpaceProfileInfo&) =
    default;
SpaceProfileInfo::~SpaceProfileInfo() = default;

std::vector<SpaceProfileInfo> GetSpaceProfiles(Profile* profile) {
  std::vector<SpaceProfileInfo> profiles;
  if (!HasSpaceProfiles(profile)) {
    return profiles;
  }
  for (const containers::UserContainer& container :
       GetUserContainers(profile)) {
    SpaceProfileInfo info;
    info.id = containers::Serialize(container.id);
    info.name = container.name;
    info.color = static_cast<int>(container.color);
    profiles.push_back(std::move(info));
  }
  const auto find = [&profiles](const std::string& id) -> SpaceProfileInfo* {
    for (SpaceProfileInfo& info : profiles) {
      if (info.id == id) {
        return &info;
      }
    }
    return nullptr;
  };
  // Every window shows the same Spaces (window sync): each name once.
  ForEachSpacesWindow(profile, [&](KoanWorkspaceController& spaces) {
    for (const KoanWorkspaceController::SpaceInfo& space : spaces.GetSpaces()) {
      SpaceProfileInfo* info = find(space.container);
      if (info && !std::ranges::contains(info->spaces, space.name)) {
        info->spaces.push_back(space.name);
      }
    }
  });
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* tab_strip = browser->GetTabStripModel();
        for (int i = 0; tab_strip && i < tab_strip->count(); ++i) {
          // (A Space's empty tab isn't a tab the user sees.)
          if (IsEmptyTab(tab_strip->GetTabAtIndex(i))) {
            continue;
          }
          const std::optional<containers::ContainerId> container =
              containers::ContainerOf(tab_strip->GetWebContentsAt(i));
          if (SpaceProfileInfo* info =
                  container ? find(containers::Serialize(*container))
                            : nullptr) {
            tabs::TabInterface* tab = tab_strip->GetTabAtIndex(i);
            ++info->open_tabs;
            info->essentials += tab->IsPinned();
            info->pinned_tabs += IsPinnedInSpace(tab);
          }
        }
        return true;
      });
  return profiles;
}

std::vector<SpaceProfileColor> GetSpaceProfileColors() {
  std::vector<SpaceProfileColor> colors;
  for (tab_groups::TabGroupColorId color : containers::UserContainerColors()) {
    colors.push_back({.color = static_cast<int>(color),
                      .name = ColorName(color),
                      .dot = GetTabGroupContextMenuColorId(color)});
  }
  return colors;
}

void GetSpaceProfileSizes(
    Profile* profile,
    base::OnceCallback<void(base::flat_map<std::string, int64_t>)> done) {
  std::vector<std::pair<std::string, base::FilePath>> paths;
  if (HasSpaceProfiles(profile)) {
    for (const containers::UserContainer& container :
         GetUserContainers(profile)) {
      // (Loading it is what Chromium's partition garbage collection does
      // with every saved container too, LoadSavedContainers().)
      content::StoragePartition* partition = profile->GetStoragePartition(
          containers::PartitionConfigFor(profile, container.id),
          /*can_create=*/true);
      paths.emplace_back(containers::Serialize(container.id),
                         partition ? partition->GetPath() : base::FilePath());
    }
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ComputeSizes, std::move(paths)), std::move(done));
}

bool RenameSpaceProfile(Profile* profile,
                        const std::string& id,
                        const std::u16string& name) {
  const std::optional<containers::ContainerId> container =
      ListedProfile(profile, id);
  if (!container) {
    return false;
  }
  ScopedListPrefUpdate update(profile->GetPrefs(), prefs::kUserContainers);
  return containers::RenameUserContainer(update.Get(), *container, name);
}

bool SetSpaceProfileColor(Profile* profile, const std::string& id, int color) {
  const std::optional<containers::ContainerId> container =
      ListedProfile(profile, id);
  if (!container || color < 0 ||
      color >= static_cast<int>(tab_groups::TabGroupColorId::kNumEntries)) {
    return false;
  }
  ScopedListPrefUpdate update(profile->GetPrefs(), prefs::kUserContainers);
  return containers::SetUserContainerColor(
      update.Get(), *container,
      static_cast<tab_groups::TabGroupColorId>(color));
}

bool ClearSpaceProfileData(Profile* profile,
                           const std::string& id,
                           base::OnceClosure done) {
  const std::optional<containers::ContainerId> container =
      ListedProfile(profile, id);
  if (!container) {
    return false;
  }
  containers::ClearUserContainerData(profile, *container, std::move(done));
  return true;
}

bool DeleteSpaceProfile(Profile* profile,
                        const std::string& id,
                        base::OnceClosure done) {
  const std::optional<containers::ContainerId> container =
      ListedProfile(profile, id);
  if (!container) {
    return false;
  }
  const std::string serialized = containers::Serialize(*container);
  // 1. Its Spaces go back to Default in every window, while they still read
  // it (a Space reads a profile that isn't listed as Default, so whatever
  // still names it afterwards, a reopened window or the stored Spaces, does
  // too). Their empty tabs move into Default now (SetSpaceContainer(), #561).
  ForEachSpacesWindow(profile, [&](KoanWorkspaceController& spaces) {
    for (const KoanWorkspaceController::SpaceInfo& space : spaces.GetSpaces()) {
      if (space.container == serialized) {
        spaces.SetSpaceContainer(space.id, std::string());
      }
    }
  });
  // 2. Out of the list: nothing opens in it any more, and none of its tabs
  // restores into it (SetUserContainerCheck()).
  {
    ScopedListPrefUpdate update(profile->GetPrefs(), prefs::kUserContainers);
    containers::RemoveUserContainer(update.Get(), *container);
  }
  // 3. Its tabs close, without a Recently Closed entry. A Space left with no
  // tab gets its empty tab back, now in the default partition. A page that
  // asks before it is left can keep its tab: its data goes anyway.
  std::vector<tabs::TabHandle> tabs;
  for (content::WebContents* contents :
       containers::GetContainerTabs(profile, *container)) {
    tabs.push_back(tabs::TabInterface::GetFromContents(contents)->GetHandle());
  }
  for (const tabs::TabHandle& handle : tabs) {
    tabs::TabInterface* tab = handle.Get();
    BrowserWindowInterface* browser =
        tab ? tab->GetBrowserWindowInterface() : nullptr;
    if (!browser) {
      continue;
    }
    TabStripModel* tab_strip = browser->GetTabStripModel();
    const int index = tab_strip->GetIndexOfTab(tab);
    if (index != TabStripModel::kNoTab) {
      tab_strip->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    }
  }
  // 4. The notifications it shows (a click on one would be dropped anyway,
  // LoadNotificationPartition()).
  CloseSpaceProfileNotifications(profile, *container);
  // 5. Its visits and closed tabs (#474), its data and its folder. What
  // marks them for deletion after a crash goes to disk with the list, before
  // anything is deleted.
  ForgetSpaceProfileHistory(profile, serialized);
  containers::DeleteUserContainerData(profile, *container, std::move(done));
  return true;
}

}  // namespace koan
