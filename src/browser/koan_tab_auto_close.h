// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_TAB_AUTO_CLOSE_H_
#define KOAN_BROWSER_KOAN_TAB_AUTO_CLOSE_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"

// The pure part of "Close tabs I haven't used for" (Arc's auto-archive,
// #482; .agent/notes/upstream/tab-auto-close.md): which tabs close, when the
// window checks, and the list the closed tabs are kept in for the Library.
// KoanTabAutoCloser (koan/browser/ui/views/koan_tab_auto_closer.h) gathers
// the facts and closes the tabs.
namespace koan::auto_close {

// The setting's values, in hours (prefs::kAutoCloseTabsAfterHours). 0 is
// Never, the default.
inline constexpr int kAllowedHours[] = {0, 12, 24, 7 * 24, 30 * 24};

// How long a tab may go unused before it closes: the setting's `hours`, or,
// for testing, `override_seconds` (the --koan-auto-close-tabs-after switch's
// value: a whole number of seconds above 0). Never (0 hours) stays never
// whatever the switch says, and so does a value that isn't in
// kAllowedHours. nullopt: nothing closes.
std::optional<base::TimeDelta> IdleLimit(int hours,
                                         std::string_view override_seconds);

// How often a window checks its tabs for `limit`: every 5 minutes, or a
// quarter of a shorter (test) limit, but at least every second.
base::TimeDelta CheckInterval(base::TimeDelta limit);

// The first check after a window opens: once the session it restores is in,
// at most 30 seconds, sooner with a shorter test limit.
base::TimeDelta FirstCheckDelay(base::TimeDelta limit);

// What the closer knows about a tab.
struct TabFacts {
  // Chromium's pinned tab: one of Zen's Essentials.
  bool essential = false;
  // One of Zen's pinned tabs, which stay in their Space.
  bool pinned = false;
  bool in_folder = false;
  // The tab its window shows, or the one a Space shows when switched to (its
  // last active tab), or selected with either.
  bool active = false;
  // A Space's empty tab (no row; koan_spaces.h IsEmptyTab()).
  bool empty_tab = false;
  // An agent's own tab, or one an agent holds a lease on.
  bool agent = false;
  // In use by Memory Saver's tests (sound, a call, screen sharing, DevTools,
  // form input, picture-in-picture, ...), downloading, or with a
  // beforeunload handler that could ask the user to stay.
  bool in_use = false;
  // When it was last the visible tab of its window (LastUsed()).
  base::Time last_used;
};

// Why a tab stays open; kNone: it closes.
enum class Keep {
  kNone,
  kEssential,
  kPinned,
  kFolder,
  kActive,
  kEmptyTab,
  kAgent,
  kInUse,
  // Used within the limit (also: last used in the future, the clock moved).
  kRecentlyUsed,
  // In a split view with a tab that stays.
  kSplitPartner,
};

// The rule, the heart of the feature: why `tab` stays open at `now` with
// tabs closing after `limit` unused, or kNone if it closes. `split_partners`
// are the other tabs of its split view (empty if it isn't in one): it stays
// while any of them does, so a split view closes whole or not at all.
Keep WhyKept(const TabFacts& tab,
             const std::vector<TabFacts>& split_partners,
             base::Time now,
             base::TimeDelta limit);

// When a tab was last the visible tab of its window: the later of when it
// was last shown (WebContents::GetLastActiveTime(), kept with the session)
// and when the closer last saw it on screen (`last_seen`, the tab's
// kLastSeenSessionKey). A tab on screen for hours counts from when it was
// left, not from when it was shown.
base::Time LastUsed(base::Time last_shown, base::Time last_seen);

// The per-tab session data key (koan_session_data.h) of when the closer last
// saw the tab on screen, and its value's form.
inline constexpr char kLastSeenSessionKey[] = "koan.last_seen";
std::string SerializeTime(base::Time time);
// A null time for anything that isn't SerializeTime()'s.
base::Time ParseTime(std::string_view text);

// --- The closed tabs (prefs::kAutoClosedTabs) ---

// Kept 120 days, as long as the Library's History looks back, and at most
// this many: Chromium's recently closed tabs keep 25, which a morning's
// automatic closes would push the user's own closes out of.
inline constexpr size_t kMaxClosedTabs = 500;
inline constexpr base::TimeDelta kKeepClosedTabsFor = base::Days(120);

// A tab closed automatically, as the Library lists it and reopens it.
struct ClosedTab {
  ClosedTab();
  ClosedTab(const ClosedTab&);
  ClosedTab& operator=(const ClosedTab&);
  ClosedTab(ClosedTab&&);
  ClosedTab& operator=(ClosedTab&&);
  ~ClosedTab();

  bool operator==(const ClosedTab&) const;

  base::DictValue ToValue() const;
  // nullopt for an entry that isn't a dict, has no id or time, or whose URL
  // isn't valid. Only Koan's keys ("koan.") are kept from `data`.
  static std::optional<ClosedTab> FromValue(const base::Value& value);

  // Unique in the list.
  std::string id;
  GURL url;
  std::u16string title;
  base::Time closed;
  // Its Space's tab group token (tab_groups::TabGroupId::ToString()), or
  // empty: where it reopens if its window still has that Space.
  std::string space;
  // Its Koan session data (koan_session_data.h) without kLastSeenSessionKey:
  // its container (#318, so it reopens in the same profile), Space theme and
  // the like, given back as a reopened closed tab's.
  std::map<std::string, std::string> data;
};

// The valid entries of `list` (newest first) that are still kept at `now`.
std::vector<ClosedTab> ReadClosedTabs(const base::ListValue& list,
                                      base::Time now);

// `list` with `tab` added in front, without entries older than
// kKeepClosedTabsFor at `now` and beyond kMaxClosedTabs (the oldest go).
base::ListValue AddClosedTab(const base::ListValue& list,
                             const ClosedTab& tab,
                             base::Time now);

// `list` without the entry `id`.
base::ListValue RemoveClosedTab(const base::ListValue& list,
                                std::string_view id);

// After history was deleted: `list` without the tabs it covers, all of them
// if `all`, else those closed within `range` [begin, end) if it is given (a
// time range deleted for every URL), and those whose URL is in `urls`.
base::ListValue ForgetDeletedHistory(
    const base::ListValue& list,
    bool all,
    std::optional<std::pair<base::Time, base::Time>> range,
    const std::set<GURL>& urls);

}  // namespace koan::auto_close

#endif  // KOAN_BROWSER_KOAN_TAB_AUTO_CLOSE_H_
