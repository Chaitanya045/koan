// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_user_containers.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace koan::containers {

namespace {

constexpr char kIdKey[] = "id";
constexpr char kNameKey[] = "name";
constexpr char kColorKey[] = "color";
constexpr char kProxyKey[] = "proxy";

// Firefox's container colours in its order (blue, turquoise, green, yellow,
// orange, red, pink, purple), as tab group colours.
constexpr tab_groups::TabGroupColorId kColors[] = {
    tab_groups::TabGroupColorId::kBlue,   tab_groups::TabGroupColorId::kCyan,
    tab_groups::TabGroupColorId::kGreen,  tab_groups::TabGroupColorId::kYellow,
    tab_groups::TabGroupColorId::kOrange, tab_groups::TabGroupColorId::kRed,
    tab_groups::TabGroupColorId::kPink,   tab_groups::TabGroupColorId::kPurple,
};

// The first entry of `list` that is `id`'s.
base::DictValue* FindEntry(base::ListValue& list, const ContainerId& id) {
  if (id.kind != ContainerKind::kUser || !id.persistent()) {
    return nullptr;
  }
  for (base::Value& entry : list) {
    base::DictValue* dict = entry.GetIfDict();
    const std::string* entry_id = dict ? dict->FindString(kIdKey) : nullptr;
    if (entry_id && *entry_id == id.id) {
      return dict;
    }
  }
  return nullptr;
}

std::optional<tab_groups::TabGroupColorId> ColorFromInt(int value) {
  for (tab_groups::TabGroupColorId color : kColors) {
    if (static_cast<int>(color) == value) {
      return color;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<ContainerId> SpaceContainerFromString(std::string_view text) {
  std::optional<ContainerId> container = Parse(text);
  if (!container || container->kind != ContainerKind::kUser ||
      !container->persistent()) {
    return std::nullopt;
  }
  return container;
}

std::vector<UserContainer> ParseUserContainers(const base::ListValue& list) {
  std::vector<UserContainer> containers;
  std::set<std::string> ids;
  for (const base::Value& entry : list) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* id = dict ? dict->FindString(kIdKey) : nullptr;
    const std::string* name = dict ? dict->FindString(kNameKey) : nullptr;
    if (!id || !name || name->empty() || !IsValidContainerId(*id) ||
        !ContainerId{.kind = ContainerKind::kUser, .id = *id}.persistent() ||
        !ids.insert(*id).second) {
      continue;
    }
    containers.push_back(
        {.id = {.kind = ContainerKind::kUser, .id = *id},
         .name = *name,
         .color = ColorFromInt(dict->FindInt(kColorKey).value_or(-1))
                      .value_or(tab_groups::TabGroupColorId::kBlue),
         .proxy = ProxySettingFromValue(dict->Find(kProxyKey))});
  }
  return containers;
}

bool SetUserContainerProxy(base::ListValue& list,
                           const ContainerId& id,
                           const std::optional<ProxySetting>& proxy) {
  if (id.kind != ContainerKind::kUser || !IsValidContainerId(id.id) ||
      !id.persistent()) {
    return false;
  }
  // The first entry ParseUserContainers() reads as `id`; a repeated one is
  // dropped there anyway.
  for (base::Value& entry : list) {
    base::DictValue* dict = entry.GetIfDict();
    const std::string* entry_id = dict ? dict->FindString(kIdKey) : nullptr;
    const std::string* name = dict ? dict->FindString(kNameKey) : nullptr;
    if (!entry_id || *entry_id != id.id || !name || name->empty()) {
      continue;
    }
    if (proxy) {
      dict->Set(kProxyKey, ProxySettingToValue(*proxy));
    } else {
      dict->Remove(kProxyKey);
    }
    return true;
  }
  return false;
}

// Names are what the user types in a prompt: short, printable.
UserContainerNameError CheckUserContainerName(std::u16string_view name) {
  const std::u16string_view trimmed =
      base::TrimWhitespace(name, base::TRIM_ALL);
  if (trimmed.empty()) {
    return UserContainerNameError::kEmpty;
  }
  if (trimmed.size() > kMaxUserContainerNameLength) {
    return UserContainerNameError::kTooLong;
  }
  if (std::ranges::any_of(
          trimmed, [](char16_t c) { return base::IsUnicodeControl(c); })) {
    return UserContainerNameError::kControlCharacter;
  }
  return UserContainerNameError::kNone;
}

std::optional<UserContainer> AddUserContainer(base::ListValue& list,
                                              std::u16string_view name) {
  if (CheckUserContainerName(name) != UserContainerNameError::kNone) {
    return std::nullopt;
  }
  const std::u16string_view trimmed =
      base::TrimWhitespace(name, base::TRIM_ALL);
  const std::vector<UserContainer> existing = ParseUserContainers(list);
  tab_groups::TabGroupColorId color =
      kColors[existing.size() % std::size(kColors)];
  for (tab_groups::TabGroupColorId candidate : kColors) {
    if (std::ranges::none_of(existing, [candidate](const UserContainer& c) {
          return c.color == candidate;
        })) {
      color = candidate;
      break;
    }
  }
  UserContainer container{.id = {.kind = ContainerKind::kUser,
                                 .id = NewContainerId(/*persistent=*/true)},
                          .name = base::UTF16ToUTF8(trimmed),
                          .color = color};
  list.Append(base::DictValue()
                  .Set(kIdKey, container.id.id)
                  .Set(kNameKey, container.name)
                  .Set(kColorKey, static_cast<int>(container.color)));
  return container;
}

base::span<const tab_groups::TabGroupColorId> UserContainerColors() {
  return kColors;
}

bool RenameUserContainer(base::ListValue& list,
                         const ContainerId& id,
                         std::u16string_view name) {
  base::DictValue* entry = FindEntry(list, id);
  if (!entry || CheckUserContainerName(name) != UserContainerNameError::kNone) {
    return false;
  }
  entry->Set(kNameKey,
             base::UTF16ToUTF8(base::TrimWhitespace(name, base::TRIM_ALL)));
  return true;
}

bool SetUserContainerColor(base::ListValue& list,
                           const ContainerId& id,
                           tab_groups::TabGroupColorId color) {
  base::DictValue* entry = FindEntry(list, id);
  if (!entry || !ColorFromInt(static_cast<int>(color))) {
    return false;
  }
  entry->Set(kColorKey, static_cast<int>(color));
  return true;
}

bool RemoveUserContainer(base::ListValue& list, const ContainerId& id) {
  if (!FindEntry(list, id)) {
    return false;
  }
  list.EraseIf([&id](const base::Value& entry) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* entry_id = dict ? dict->FindString(kIdKey) : nullptr;
    return entry_id && *entry_id == id.id;
  });
  return true;
}

std::optional<ContainerId> ListedSpaceContainer(const base::ListValue& list,
                                                std::string_view stored) {
  const std::optional<ContainerId> container = SpaceContainerFromString(stored);
  if (!container) {
    return std::nullopt;
  }
  for (const UserContainer& listed : ParseUserContainers(list)) {
    if (listed.id == *container) {
      return container;
    }
  }
  return std::nullopt;
}

std::optional<std::string> NewTabContainer(const NewTabFacts& facts) {
  // 1, 2: an opener's BrowsingInstance; pages no container holds.
  if (facts.has_opener || !facts.container_url) {
    return std::nullopt;
  }
  // 3: a page's tab keeps the page's partition, unless a route sends it from
  // a tab in its own Space's container to another Space (Zen's
  // shouldUseRouteContainer()). An agent's tabs stay the agent's.
  if (facts.from_page) {
    if (!facts.from_agent && facts.route_container &&
        facts.source_container == facts.source_space_container) {
      return facts.route_container;
    }
    return std::nullopt;
  }
  // 5: a routed tab takes its Space's container (before Zen's add-on rule,
  // as Zen's routing runs first).
  if (facts.route_container) {
    return facts.route_container;
  }
  // 4: an extension's tab with no page behind it stays in the default.
  if (facts.from_extension) {
    return std::nullopt;
  }
  // 6: every other tab no page opened takes the container of its Space.
  return facts.space_container;
}

bool ShowsContainerLine(const std::optional<ContainerId>& tab_container,
                        const std::optional<ContainerId>& space_container) {
  return tab_container && tab_container->kind == ContainerKind::kUser &&
         tab_container != space_container;
}

EssentialsProfiles::EssentialsProfiles() = default;
EssentialsProfiles::EssentialsProfiles(const EssentialsProfiles&) = default;
EssentialsProfiles& EssentialsProfiles::operator=(const EssentialsProfiles&) =
    default;
EssentialsProfiles::~EssentialsProfiles() = default;

std::optional<ContainerId> EssentialsProfiles::GridOf(
    const std::optional<ContainerId>& tab_container) const {
  if (separate && tab_container &&
      tab_container->kind == ContainerKind::kUser &&
      std::ranges::contains(spaces, *tab_container)) {
    return tab_container;
  }
  return std::nullopt;
}

bool EssentialsProfiles::Shows(
    const std::optional<ContainerId>& tab_container) const {
  return !separate || GridOf(tab_container) == shown;
}

}  // namespace koan::containers
