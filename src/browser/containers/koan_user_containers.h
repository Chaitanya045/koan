// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_USER_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_USER_CONTAINERS_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/values.h"
#include "components/tab_groups/tab_group_color.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_container_proxy.h"

// Zen's containers per Space (#318, .agent/notes/upstream/space-containers.md):
// the user's own containers, which Zen's UI calls profiles, and the rules for
// which one a new tab opens in. Pure logic over //base, the container id and
// the tab group colours; the browser side is koan_space_containers.h in
// //koan/browser/ui/views.
namespace koan::containers {

// One of the user's containers: a saved user container (ContainerKind::kUser,
// on disk), its name and its colour (Firefox's container colour, here one of
// the tab group colours, so it has Chromium's colour ids in light and dark),
// and the proxy its tabs go through, if any (#445, koan_container_proxy.h).
struct UserContainer {
  ContainerId id;
  std::string name;
  tab_groups::TabGroupColorId color = tab_groups::TabGroupColorId::kBlue;
  std::optional<ProxySetting> proxy;

  friend bool operator==(const UserContainer&, const UserContainer&) = default;
};

// A Space's container as it is stored (its Space data, koan.workspaces.list,
// window sync): a Serialize()d saved user container, or none for anything
// else (empty, "none", an agent's, a throwaway one, garbage).
std::optional<ContainerId> SpaceContainerFromString(std::string_view text);

// The containers of `list` (prefs::kUserContainers), in order. Entries that
// aren't a saved user container with a name, and repeated ids, are dropped;
// an unknown colour is blue, an invalid proxy none.
std::vector<UserContainer> ParseUserContainers(const base::ListValue& list);

// Sets the proxy of the container `id` in `list` to `proxy` (nullopt: none).
// False if `list` has no such container.
bool SetUserContainerProxy(base::ListValue& list,
                           const ContainerId& id,
                           const std::optional<ProxySetting>& proxy);

// The longest name a container takes, in UTF-16 code units, once trimmed.
inline constexpr size_t kMaxUserContainerNameLength = 64;

// Why a container can't take a name the user typed (trimmed of white space
// at both ends), or kNone if it can.
enum class UserContainerNameError {
  kNone,
  // Empty, or only white space.
  kEmpty,
  // Longer than kMaxUserContainerNameLength.
  kTooLong,
  // A control character (a tab, a line break).
  kControlCharacter,
};
UserContainerNameError CheckUserContainerName(std::u16string_view name);

// Adds a new, empty container named `name` (trimmed) to `list`, with the first
// colour of Firefox's order (blue, turquoise, green, yellow, orange, red,
// pink, purple) that no container has yet, else the next one round. Nullopt
// for a name CheckUserContainerName() refuses.
std::optional<UserContainer> AddUserContainer(base::ListValue& list,
                                              std::u16string_view name);

// Settings > Tab Management > Profiles (#477). The colours a profile can
// have, in Firefox's order.
base::span<const tab_groups::TabGroupColorId> UserContainerColors();

// Renames `id`'s entry of `list` to `name` (trimmed, refused as
// AddUserContainer() refuses it), or sets its colour (one of
// UserContainerColors()). False, and `list` unchanged, for a container the
// list doesn't have, a refused name or colour.
bool RenameUserContainer(base::ListValue& list,
                         const ContainerId& id,
                         std::u16string_view name);
bool SetUserContainerColor(base::ListValue& list,
                           const ContainerId& id,
                           tab_groups::TabGroupColorId color);

// Takes `id`'s entries out of `list`. False if it had none.
bool RemoveUserContainer(base::ListValue& list, const ContainerId& id);

// A Space's container (SpaceContainerFromString()) that `list` still has:
// a deleted profile's Spaces, and whatever still names it (a reopened
// window's tabs, the stored Spaces), read as Default.
std::optional<ContainerId> ListedSpaceContainer(const base::ListValue& list,
                                                std::string_view stored);

// What a new tab opens in, as CreateTargetContents() sees it
// (koan::SiteInstanceForNewTab(), koan_space_containers.h).
struct NewTabFacts {
  // window.open() with an opener: the new tab shares its BrowsingInstance.
  bool has_opener = false;
  // A web page, about:blank or the new tab page: a page a container can
  // hold. Not chrome://settings and the like, extension pages, DevTools.
  bool container_url = true;
  // A page (a link, its context menu, Glance) opened it: the source frame's
  // partition (Serialize()d, empty for the default one), and the container of
  // the source tab's Space. Whether the source is an agent's tab.
  bool from_page = false;
  std::string source_container;
  std::string source_space_container;
  bool from_agent = false;
  // chrome.tabs.create() (an extension's initiator, no page).
  bool from_extension = false;
  // A Space route sends the tab to another Space: that Space's container.
  std::optional<std::string> route_container;
  // The Space the tab opens in: its container (empty for none).
  std::string space_container;
};

// The container a new tab opens in: a Serialize()d user container, empty
// for the default partition, or nullopt to leave it to upstream and the
// container core (an opener's BrowsingInstance, or a source page's
// partition, koan::containers::SiteInstanceForNewTab()). The rules are Zen's
// (getContextIdIfNeeded(), shouldUseRouteContainer()), numbered as in
// .agent/notes/upstream/space-containers.md.
std::optional<std::string> NewTabContainer(const NewTabFacts& facts);

// Zen's container indicator with zen.workspaces.hide-default-container-
// indicator on: whether a tab in `tab_container` shows its container's colour
// line in a Space whose container is `space_container`. Only a user container
// other than the Space's does.
bool ShowsContainerLine(const std::optional<ContainerId>& tab_container,
                        const std::optional<ContainerId>& space_container);

// Zen's container-specific Essentials (zen.workspaces.separate-essentials,
// #479) in one window: one grid of Essentials per profile, and the window
// shows the active Space's.
struct EssentialsProfiles {
  EssentialsProfiles();
  EssentialsProfiles(const EssentialsProfiles&);
  EssentialsProfiles& operator=(const EssentialsProfiles&);
  ~EssentialsProfiles();

  // Off (the setting, or a window without Spaces): one grid of every
  // Essential.
  bool separate = false;
  // The active Space's container (nullopt: none, the default profile).
  std::optional<ContainerId> shown;
  // The user containers the window's Spaces have.
  std::vector<ContainerId> spaces;

  // The grid of an Essential, or of a tab that would become one, in
  // `tab_container`: that container's if one of the Spaces has it, else the
  // default profile's (nullopt). Zen hides an Essential whose container no
  // Space has; here it stays reachable in the Spaces without a profile.
  // Off, every Essential is in the one grid (nullopt).
  std::optional<ContainerId> GridOf(
      const std::optional<ContainerId>& tab_container) const;
  // Whether the grid shows the Essential now.
  bool Shows(const std::optional<ContainerId>& tab_container) const;

  friend bool operator==(const EssentialsProfiles&,
                         const EssentialsProfiles&) = default;
};

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_USER_CONTAINERS_H_
