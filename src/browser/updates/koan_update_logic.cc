// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_logic.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace koan::updates {

namespace {

constexpr size_t kShortShaLength = 12;

// The digits of `text` from `pos`, `length` of them, as a number.
std::optional<int> ReadNumber(std::string_view text,
                              size_t pos,
                              size_t length) {
  int value = 0;
  if (pos + length > text.size() ||
      !base::StringToInt(text.substr(pos, length), &value)) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::optional<base::Time> ParseGitHubTime(std::string_view text) {
  // YYYY-MM-DDTHH:MM:SSZ
  if (text.size() != 20 || text[4] != '-' || text[7] != '-' ||
      text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
      text[19] != 'Z') {
    return std::nullopt;
  }
  const std::optional<int> year = ReadNumber(text, 0, 4);
  const std::optional<int> month = ReadNumber(text, 5, 2);
  const std::optional<int> day = ReadNumber(text, 8, 2);
  const std::optional<int> hour = ReadNumber(text, 11, 2);
  const std::optional<int> minute = ReadNumber(text, 14, 2);
  const std::optional<int> second = ReadNumber(text, 17, 2);
  if (!year || !month || !day || !hour || !minute || !second) {
    return std::nullopt;
  }
  base::Time::Exploded exploded = {};
  exploded.year = *year;
  exploded.month = *month;
  exploded.day_of_month = *day;
  exploded.hour = *hour;
  exploded.minute = *minute;
  exploded.second = *second;
  base::Time time;
  if (!exploded.HasValidValues() ||
      !base::Time::FromUTCExploded(exploded, &time)) {
    return std::nullopt;
  }
  return time;
}

std::optional<Commit> ParseLatestCommit(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || !value->is_list() || value->GetList().empty() ||
      !value->GetList().front().is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& commit = value->GetList().front().GetDict();
  const std::string* sha = commit.FindString("sha");
  const std::string* date =
      commit.FindStringByDottedPath("commit.committer.date");
  if (!sha || sha->size() != 40 ||
      !std::ranges::all_of(*sha, base::IsHexDigit<char>) || !date) {
    return std::nullopt;
  }
  const std::optional<base::Time> time = ParseGitHubTime(*date);
  if (!time) {
    return std::nullopt;
  }
  return Commit{*sha, *time};
}

bool IsUpdateAvailable(std::string_view built_sha,
                       base::Time built_time,
                       const Commit& latest) {
  return !built_sha.empty() && latest.sha != built_sha &&
         latest.time > built_time;
}

base::TimeDelta NextCheckDelay(base::Time last_check, base::Time now) {
  if (last_check.is_null() || last_check > now) {
    return last_check.is_null() ? kFirstCheckDelay : kCheckInterval;
  }
  return std::max(kFirstCheckDelay, last_check + kCheckInterval - now);
}

std::string BuildStamp(std::string_view version, std::string_view commit) {
  return base::StrCat({version, "/", commit.substr(0, kShortShaLength)});
}

std::string_view CommitOfStamp(std::string_view stamp) {
  const size_t slash = stamp.find('/');
  return slash == std::string_view::npos ? std::string_view()
                                         : stamp.substr(slash + 1);
}

GURL LatestCommitUrl(std::string_view repo) {
  return GURL(base::StrCat({"https://api.github.com/repos/", repo,
                            "/commits?sha=", kBranch, "&per_page=1"}));
}

GURL WhatsNewUrl(std::string_view repo,
                 std::string_view from,
                 std::string_view to) {
  if (from.empty() || to.empty() || from == to) {
    return GURL(
        base::StrCat({"https://github.com/", repo, "/commits/", kBranch}));
  }
  return GURL(base::StrCat(
      {"https://github.com/", repo, "/compare/", from, "...", to}));
}

GURL HowToUpdateUrl(std::string_view repo) {
  return GURL(base::StrCat({"https://github.com/", repo, "/blob/", kBranch,
                            "/docs/BUILDING-MAC.md#updating"}));
}

GURL IssuesUrl(std::string_view repo) {
  return GURL(base::StrCat({"https://github.com/", repo, "/issues"}));
}

}  // namespace koan::updates
