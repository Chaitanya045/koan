// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_routes.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/re2/src/re2/re2.h"

namespace koan {

namespace {

constexpr char kReferenceKey[] = "reference";
constexpr char kMatchKey[] = "match_type";
constexpr char kSpaceKey[] = "space";
// Zen's names for the match types.
constexpr char kContains[] = "contains";
constexpr char kEqualTo[] = "equal-to";
constexpr char kRegex[] = "regex";

std::optional<SpaceRouteMatch> MatchFromString(std::string_view name) {
  if (name == kContains) {
    return SpaceRouteMatch::kContains;
  }
  if (name == kEqualTo) {
    return SpaceRouteMatch::kEqualTo;
  }
  if (name == kRegex) {
    return SpaceRouteMatch::kRegex;
  }
  return std::nullopt;
}

const char* MatchToString(SpaceRouteMatch match) {
  switch (match) {
    case SpaceRouteMatch::kContains:
      return kContains;
    case SpaceRouteMatch::kEqualTo:
      return kEqualTo;
    case SpaceRouteMatch::kRegex:
      return kRegex;
  }
}

// Zen's #normalizeURL(): no http(s)://, no www., no final slash.
std::string NormalizeUrl(std::string_view url) {
  std::string clean(base::TrimWhitespaceASCII(url, base::TRIM_ALL));
  clean = base::ToLowerASCII(clean);
  for (std::string_view scheme : {"https://", "http://"}) {
    if (clean.starts_with(scheme)) {
      clean.erase(0, scheme.size());
      break;
    }
  }
  if (clean.starts_with("www.")) {
    clean.erase(0, 4);
  }
  if (clean.ends_with('/')) {
    clean.pop_back();
  }
  return clean;
}

RE2::Options RegexOptions() {
  RE2::Options options;
  // Zen's "i" flag (gh-15835); no log spam for a user's typo.
  options.set_case_sensitive(false);
  options.set_log_errors(false);
  return options;
}

}  // namespace

base::DictValue SpaceRoute::ToValue() const {
  return base::DictValue()
      .Set(kReferenceKey, reference)
      .Set(kMatchKey, MatchToString(match))
      .Set(kSpaceKey, space);
}

// static
std::optional<SpaceRoute> SpaceRoute::FromValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* reference = dict->FindString(kReferenceKey);
  const std::string* match = dict->FindString(kMatchKey);
  const std::string* space = dict->FindString(kSpaceKey);
  if (!reference || !match || !MatchFromString(*match)) {
    return std::nullopt;
  }
  SpaceRoute route;
  route.reference = *reference;
  route.match = *MatchFromString(*match);
  route.space = space ? base::UTF8ToUTF16(*space) : std::u16string();
  return route;
}

std::vector<SpaceRoute> ParseSpaceRoutes(const base::ListValue& list) {
  std::vector<SpaceRoute> routes;
  for (const base::Value& value : list) {
    if (std::optional<SpaceRoute> route = SpaceRoute::FromValue(value)) {
      routes.push_back(std::move(*route));
    }
  }
  return routes;
}

base::ListValue SpaceRoutesToValue(const std::vector<SpaceRoute>& routes) {
  base::ListValue list;
  for (const SpaceRoute& route : routes) {
    if (!base::TrimWhitespaceASCII(route.reference, base::TRIM_ALL).empty()) {
      list.Append(route.ToValue());
    }
  }
  return list;
}

bool IsValidSpaceRouteRegex(std::string_view pattern) {
  return RE2(pattern, RegexOptions()).ok();
}

bool IsSpaceRouteMatching(std::string_view url, const SpaceRoute& route) {
  if (base::TrimWhitespaceASCII(route.reference, base::TRIM_ALL).empty()) {
    return false;
  }
  switch (route.match) {
    case SpaceRouteMatch::kContains:
      return base::ToLowerASCII(url).find(
                 base::ToLowerASCII(route.reference)) != std::string::npos;
    case SpaceRouteMatch::kEqualTo:
      return NormalizeUrl(url) == NormalizeUrl(route.reference);
    case SpaceRouteMatch::kRegex: {
      const RE2 regex(route.reference, RegexOptions());
      return regex.ok() && RE2::PartialMatch(url, regex);
    }
  }
}

std::optional<std::u16string> RouteUrlToSpace(
    std::string_view url,
    const std::vector<SpaceRoute>& routes,
    bool from_external,
    const std::u16string& external_default) {
  for (const SpaceRoute& route : routes) {
    if (IsSpaceRouteMatching(url, route)) {
      if (route.space.empty()) {
        return std::nullopt;
      }
      return route.space;
    }
  }
  if (from_external && !external_default.empty()) {
    return external_default;
  }
  return std::nullopt;
}

bool RenameSpaceInRoutes(std::vector<SpaceRoute>& routes,
                         const std::u16string& old_name,
                         const std::u16string& new_name) {
  bool changed = false;
  for (SpaceRoute& route : routes) {
    if (!old_name.empty() && route.space == old_name) {
      route.space = new_name;
      changed = true;
    }
  }
  return changed;
}

std::optional<SpaceRoute> RouteForHosts(const std::vector<std::string>& hosts) {
  std::vector<std::string> unique;
  for (const std::string& host : hosts) {
    if (!host.empty() &&
        std::find(unique.begin(), unique.end(), host) == unique.end()) {
      unique.push_back(host);
    }
  }
  if (unique.empty()) {
    return std::nullopt;
  }
  SpaceRoute route;
  if (unique.size() == 1) {
    route.reference = unique.front();
    return route;
  }
  route.match = SpaceRouteMatch::kRegex;
  std::vector<std::string> escaped;
  for (const std::string& host : unique) {
    escaped.push_back(RE2::QuoteMeta(host));
  }
  route.reference = "(" + base::JoinString(escaped, "|") + ")";
  return route;
}

}  // namespace koan
