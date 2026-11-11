// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ROUTES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ROUTES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

namespace koan {

// Zen's Space routing rules (ZenSpaceRoutingManager.sys.mjs), as plain logic
// for the unit tests: which Space a new tab's URL opens in. Stored in
// prefs::kSpaceRoutes (a list of ToValue() dicts) and
// prefs::kSpaceRoutingExternalDefault.

// How a route's reference is compared with the URL (Zen's matchType).
enum class SpaceRouteMatch {
  kContains,  // "contains": the URL contains it, ignoring case.
  kEqualTo,   // "equal-to": the same URL without scheme, www. and final /.
  kRegex,     // "regex": a regular expression found in the URL, any case.
};

struct SpaceRoute {
  std::string reference;
  SpaceRouteMatch match = SpaceRouteMatch::kContains;
  // The Space's name; empty for Zen's "most-recent-space" (the active one).
  // Koan's Spaces belong to a window, so a route names them.
  std::u16string space;

  bool operator==(const SpaceRoute&) const = default;

  base::DictValue ToValue() const;
  static std::optional<SpaceRoute> FromValue(const base::Value& value);
};

std::vector<SpaceRoute> ParseSpaceRoutes(const base::ListValue& list);
// Drops routes with an empty reference, as Zen's saveRoutes().
base::ListValue SpaceRoutesToValue(const std::vector<SpaceRoute>& routes);

// Whether `route` matches `url` (Zen's isRouteMatching()). An empty
// reference or an invalid regular expression matches nothing.
bool IsSpaceRouteMatching(std::string_view url, const SpaceRoute& route);
// Whether `pattern` is a regular expression Koan can use (RE2's syntax: no
// lookaround or backreferences).
bool IsValidSpaceRouteRegex(std::string_view pattern);

// The Space `url` opens in (Zen's routeUri()): the first matching route's,
// else for a link from another app `external_default`; null for the active
// Space (no route, or "most recent Space").
std::optional<std::u16string> RouteUrlToSpace(
    std::string_view url,
    const std::vector<SpaceRoute>& routes,
    bool from_external,
    const std::u16string& external_default);

// A renamed Space keeps its routes: those naming `old_name` name `new_name`.
// Returns whether any changed.
bool RenameSpaceInRoutes(std::vector<SpaceRoute>& routes,
                         const std::u16string& old_name,
                         const std::u16string& new_name);

// Zen's "Add Route for Domain(s)" (addRouteForSelected()): one host contains,
// several a regular expression "(a\.com|b\.org)". Null without hosts.
std::optional<SpaceRoute> RouteForHosts(const std::vector<std::string>& hosts);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ROUTES_H_
