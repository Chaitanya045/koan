// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_USER_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_USER_CONTAINERS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "components/tab_groups/tab_group_color.h"
#include "koan/browser/containers/koan_container_partition.h"

// Zen's containers per Space (#318, .agent/notes/upstream/space-containers.md):
// the user's own containers, which Zen's UI calls profiles, and the rules for
// which one a new tab opens in. Pure logic over //base, the container id and
// the tab group colours; the browser side is koan_space_containers.h in
// //koan/browser/ui/views.
namespace koan::containers {

// One of the user's containers: a saved user container (ContainerKind::kUser,
// on disk), its name and its colour (Firefox's container colour, here one of
// the tab group colours, so it has Chromium's colour ids in light and dark).
struct UserContainer {
  ContainerId id;
  std::string name;
  tab_groups::TabGroupColorId color = tab_groups::TabGroupColorId::kBlue;

  friend bool operator==(const UserContainer&, const UserContainer&) = default;
};

// A Space's container as it is stored (its Space data, koan.workspaces.list,
// window sync): a Serialize()d saved user container, or none for anything
// else (empty, "none", an agent's, a throwaway one, garbage).
std::optional<ContainerId> SpaceContainerFromString(std::string_view text);

// The containers of `list` (prefs::kUserContainers), in order. Entries that
// aren't a saved user container with a name, and repeated ids, are dropped;
// an unknown colour is blue.
std::vector<UserContainer> ParseUserContainers(const base::ListValue& list);

// Adds a new, empty container named `name` (trimmed) to `list`, with the first
// colour of Firefox's order (blue, turquoise, green, yellow, orange, red,
// pink, purple) that no container has yet, else the next one round. Nullopt
// for a name that is empty, longer than 64 characters or has a control
// character.
std::optional<UserContainer> AddUserContainer(base::ListValue& list,
                                              std::u16string_view name);

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

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_USER_CONTAINERS_H_
