// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_history_profiles.h"

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "koan/browser/containers/koan_container_partition.h"

namespace koan::containers {

namespace {

// Android's app ids are package names ("com.example.app"), which never start
// so.
constexpr std::string_view kAppIdPrefix = "koan:";

// Whether `profile` is a saved user container, the only kind with a history
// of its own.
bool IsHistoryProfile(std::string_view profile) {
  const std::optional<ContainerId> container = Parse(profile);
  return container && container->kind == ContainerKind::kUser &&
         container->persistent();
}

}  // namespace

std::string HistoryAppIdForProfile(std::string_view profile) {
  return IsHistoryProfile(profile) ? base::StrCat({kAppIdPrefix, profile})
                                   : std::string();
}

std::string HistoryProfileFromAppId(const std::optional<std::string>& app_id) {
  if (!app_id || !base::StartsWith(*app_id, kAppIdPrefix)) {
    return std::string();
  }
  const std::string_view profile =
      std::string_view(*app_id).substr(kAppIdPrefix.size());
  return IsHistoryProfile(profile) ? std::string(profile) : std::string();
}

std::optional<std::string> VisitAppId(const content::WebContents* contents,
                                      std::optional<std::string> app_id) {
  if (app_id) {
    return app_id;
  }
  const std::optional<ContainerId> container = ContainerOf(contents);
  std::string tag =
      container ? HistoryAppIdForProfile(Serialize(*container)) : std::string();
  if (tag.empty()) {
    return std::nullopt;
  }
  return tag;
}

bool PageShowsInProfile(const PageProfiles* page, std::string_view profile) {
  if (!page) {
    return profile.empty();
  }
  return profile.empty() ? page->in_default
                         : page->profiles.contains(std::string(profile));
}

}  // namespace koan::containers
