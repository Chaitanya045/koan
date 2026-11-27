// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_tab_auto_close.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "base/json/values_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace koan::auto_close {

namespace {

constexpr base::TimeDelta kDefaultCheckInterval = base::Minutes(5);
constexpr base::TimeDelta kMinCheckInterval = base::Seconds(1);
constexpr base::TimeDelta kDefaultFirstCheckDelay = base::Seconds(30);
// The notice's own time on screen (KoanTabAutoCloser's kNoticeTimeout).
constexpr base::TimeDelta kMinNoticeLead = base::Seconds(10);
// A check that runs a moment early by the wall clock still counts.
constexpr base::TimeDelta kNoticeSlack = base::Milliseconds(500);

constexpr char kIdKey[] = "id";
constexpr char kUrlKey[] = "url";
constexpr char kTitleKey[] = "title";
constexpr char kClosedKey[] = "closed";
constexpr char kSpaceKey[] = "space";
constexpr char kDataKey[] = "data";

// Only Koan's own session data comes back with a reopened tab.
constexpr char kKoanDataPrefix[] = "koan.";

// The rules that don't depend on the split view.
Keep WhyKeptAlone(const TabFacts& tab, base::Time now, base::TimeDelta limit) {
  if (tab.essential) {
    return Keep::kEssential;
  }
  if (tab.pinned) {
    return Keep::kPinned;
  }
  if (tab.in_folder) {
    return Keep::kFolder;
  }
  if (tab.active) {
    return Keep::kActive;
  }
  if (tab.empty_tab) {
    return Keep::kEmptyTab;
  }
  if (tab.agent) {
    return Keep::kAgent;
  }
  if (tab.in_use) {
    return Keep::kInUse;
  }
  // A tab never shown counts from when it was made (GetLastActiveTime()); a
  // null time (no data at all) never closes. A time ahead of `now` (the
  // clock was set back) is recent.
  if (tab.last_used.is_null() || now - tab.last_used < limit) {
    return Keep::kRecentlyUsed;
  }
  return Keep::kNone;
}

}  // namespace

std::optional<base::TimeDelta> IdleLimit(int hours,
                                         std::string_view override_seconds) {
  if (hours <= 0 || !std::ranges::contains(kAllowedHours, hours)) {
    return std::nullopt;
  }
  int seconds = 0;
  if (!override_seconds.empty() &&
      base::StringToInt(override_seconds, &seconds) && seconds > 0) {
    return base::Seconds(seconds);
  }
  return base::Hours(hours);
}

base::TimeDelta CheckInterval(base::TimeDelta limit) {
  return std::clamp(limit / 4, kMinCheckInterval, kDefaultCheckInterval);
}

base::TimeDelta FirstCheckDelay(base::TimeDelta limit) {
  return std::min(kDefaultFirstCheckDelay, CheckInterval(limit));
}

base::TimeDelta NoticeLead(base::TimeDelta limit) {
  return std::max(kMinNoticeLead, CheckInterval(limit));
}

bool NoticeLongEnoughAgo(base::Time shown,
                         base::Time now,
                         base::TimeDelta limit) {
  return !shown.is_null() && shown <= now &&
         now - shown >= NoticeLead(limit) - kNoticeSlack;
}

Keep WhyKept(const TabFacts& tab,
             const std::vector<TabFacts>& split_partners,
             base::Time now,
             base::TimeDelta limit) {
  if (Keep keep = WhyKeptAlone(tab, now, limit); keep != Keep::kNone) {
    return keep;
  }
  for (const TabFacts& partner : split_partners) {
    if (WhyKeptAlone(partner, now, limit) != Keep::kNone) {
      return Keep::kSplitPartner;
    }
  }
  return Keep::kNone;
}

base::Time LastUsed(base::Time last_shown, base::Time last_seen) {
  return std::max(last_shown, last_seen);
}

std::string SerializeTime(base::Time time) {
  return base::NumberToString(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
}

base::Time ParseTime(std::string_view text) {
  int64_t microseconds = 0;
  if (!base::StringToInt64(text, &microseconds) || microseconds <= 0) {
    return base::Time();
  }
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

ClosedTab::ClosedTab() = default;
ClosedTab::ClosedTab(const ClosedTab&) = default;
ClosedTab& ClosedTab::operator=(const ClosedTab&) = default;
ClosedTab::ClosedTab(ClosedTab&&) = default;
ClosedTab& ClosedTab::operator=(ClosedTab&&) = default;
ClosedTab::~ClosedTab() = default;

bool ClosedTab::operator==(const ClosedTab&) const = default;

base::DictValue ClosedTab::ToValue() const {
  base::DictValue stored_data;
  for (const auto& [key, value] : data) {
    stored_data.Set(key, value);
  }
  return base::DictValue()
      .Set(kIdKey, id)
      .Set(kUrlKey, url.spec())
      .Set(kTitleKey, title)
      .Set(kClosedKey, base::TimeToValue(closed))
      .Set(kSpaceKey, space)
      .Set(kDataKey, std::move(stored_data));
}

// static
std::optional<ClosedTab> ClosedTab::FromValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString(kIdKey);
  const std::string* url = dict->FindString(kUrlKey);
  const base::Value* closed_value = dict->Find(kClosedKey);
  std::optional<base::Time> closed =
      closed_value ? base::ValueToTime(*closed_value) : std::nullopt;
  if (!id || id->empty() || !url || !closed || closed->is_null()) {
    return std::nullopt;
  }
  ClosedTab tab;
  tab.id = *id;
  tab.url = GURL(*url);
  if (!tab.url.is_valid()) {
    return std::nullopt;
  }
  tab.closed = *closed;
  if (const std::string* title = dict->FindString(kTitleKey)) {
    tab.title = base::UTF8ToUTF16(*title);
  }
  if (const std::string* space = dict->FindString(kSpaceKey)) {
    tab.space = *space;
  }
  if (const base::DictValue* data = dict->FindDict(kDataKey)) {
    for (const auto [key, entry] : *data) {
      if (entry.is_string() && key != kLastSeenSessionKey &&
          base::StartsWith(key, kKoanDataPrefix)) {
        tab.data.emplace(key, entry.GetString());
      }
    }
  }
  return tab;
}

std::vector<ClosedTab> ReadClosedTabs(const base::ListValue& list,
                                      base::Time now) {
  std::vector<ClosedTab> tabs;
  for (const base::Value& value : list) {
    std::optional<ClosedTab> tab = ClosedTab::FromValue(value);
    if (tab && now - tab->closed < kKeepClosedTabsFor) {
      tabs.push_back(std::move(*tab));
    }
  }
  std::ranges::stable_sort(tabs, std::ranges::greater(), &ClosedTab::closed);
  return tabs;
}

base::ListValue AddClosedTab(const base::ListValue& list,
                             const ClosedTab& tab,
                             base::Time now) {
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, now);
  std::erase_if(tabs,
                [&](const ClosedTab& other) { return other.id == tab.id; });
  tabs.insert(tabs.begin(), tab);
  if (tabs.size() > kMaxClosedTabs) {
    tabs.resize(kMaxClosedTabs);
  }
  base::ListValue result;
  for (const ClosedTab& kept : tabs) {
    result.Append(kept.ToValue());
  }
  return result;
}

base::ListValue RemoveClosedTab(const base::ListValue& list,
                                std::string_view id) {
  base::ListValue result;
  for (const base::Value& value : list) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* entry_id = dict ? dict->FindString(kIdKey) : nullptr;
    if (!entry_id || *entry_id != id) {
      result.Append(value.Clone());
    }
  }
  return result;
}

base::ListValue ForgetDeletedHistory(
    const base::ListValue& list,
    bool all,
    std::optional<std::pair<base::Time, base::Time>> range,
    const std::set<GURL>& urls) {
  return ForgetDeletedHistory(list, all, range, urls,
                              [](const ClosedTab&) { return true; });
}

base::ListValue ForgetDeletedHistory(
    const base::ListValue& list,
    bool all,
    std::optional<std::pair<base::Time, base::Time>> range,
    const std::set<GURL>& urls,
    base::FunctionRef<bool(const ClosedTab&)> in_profile) {
  base::ListValue result;
  for (const base::Value& value : list) {
    std::optional<ClosedTab> tab = ClosedTab::FromValue(value);
    if (!tab) {
      continue;
    }
    const bool covered =
        all || urls.contains(tab->url) ||
        (range && tab->closed >= range->first && tab->closed < range->second);
    if (covered && in_profile(*tab)) {
      continue;
    }
    result.Append(value.Clone());
  }
  return result;
}

}  // namespace koan::auto_close
