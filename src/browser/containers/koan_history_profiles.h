// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_HISTORY_PROFILES_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_HISTORY_PROFILES_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace content {
class WebContents;
}  // namespace content

// History per Space profile (#474, .agent/notes/upstream/space-history.md).
// Every visit records the profile (the saved user container,
// koan_user_containers.h) of the tab it happened in, in the visits table's
// `app_id` column: upstream's per-visit app tag, which only Android sets
// (Custom Tabs), which QueryHistory(), ExpireHistoryBetween() and the delete
// paths already filter by, and whose schema and migrations are upstream's.
// A visit with no tag, or one from before this shipped, is the default
// profile's.
//
// A profile is named as a Space's container is (GetSpaceContainer()): a
// Serialize()d saved user container, or empty for the default profile.
namespace koan::containers {

// The app_id of a visit in `profile`: "koan:" and the profile, or empty for
// the default profile and for anything that isn't a saved user container.
// QueryOptions::app_id with the empty string matches the default profile's
// visits (upstream stores no tag as '').
std::string HistoryAppIdForProfile(std::string_view profile);

// The profile a visit's app_id names: empty for none, for an app's own tag
// (Android's) and for one that doesn't parse.
std::string HistoryProfileFromAppId(const std::optional<std::string>& app_id);

// HistoryTabHelper::CreateHistoryAddPageArgs(): the app_id of a visit in
// `contents`. An app_id upstream set is kept (Android only); otherwise the
// tab's saved user container's, or none. Agent tabs record none: their
// history is kept out, or the user's (koan_agent_access.h).
std::optional<std::string> VisitAppId(const content::WebContents* contents,
                                      std::optional<std::string> app_id);

// The profiles a page (a URL, or the URLs that strip to one omnibox match)
// has visits in.
struct PageProfiles {
  // A visit without a tag: the default profile's, or one from before #474.
  bool in_default = false;
  // Non-empty profiles.
  std::set<std::string> profiles;
};

// Whether the URL bar shows a history suggestion of a page with `page`'s
// visits in a Space of `profile`. A page with no tagged visit (`page` null)
// is the default profile's only. A page visited in two profiles shows in
// both.
bool PageShowsInProfile(const PageProfiles* page, std::string_view profile);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_HISTORY_PROFILES_H_
