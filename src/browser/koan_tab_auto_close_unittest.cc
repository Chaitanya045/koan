// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_tab_auto_close.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::auto_close {

namespace {

constexpr base::TimeDelta kLimit = base::Hours(12);

base::Time Now() {
  base::Time now;
  EXPECT_TRUE(base::Time::FromUTCString("1 Dec 2026 12:00:00", &now));
  return now;
}

// A normal tab last used `idle` ago.
TabFacts Idle(base::TimeDelta idle) {
  return TabFacts{.last_used = Now() - idle};
}

Keep Judge(const TabFacts& tab) {
  return WhyKept(tab, {}, Now(), kLimit);
}

ClosedTab Closed(const std::string& id, base::Time closed) {
  ClosedTab tab;
  tab.id = id;
  tab.url = GURL("https://example.com/" + id);
  tab.title = u"Example";
  tab.closed = closed;
  return tab;
}

}  // namespace

TEST(KoanTabAutoCloseTest, NeverIsTheDefaultAndStaysNever) {
  EXPECT_EQ(std::nullopt, IdleLimit(0, ""));
  // The test switch doesn't turn the feature on.
  EXPECT_EQ(std::nullopt, IdleLimit(0, "60"));
}

TEST(KoanTabAutoCloseTest, TheSettingsTimes) {
  EXPECT_EQ(base::Hours(12), IdleLimit(12, ""));
  EXPECT_EQ(base::Hours(24), IdleLimit(24, ""));
  EXPECT_EQ(base::Days(7), IdleLimit(7 * 24, ""));
  EXPECT_EQ(base::Days(30), IdleLimit(30 * 24, ""));
}

TEST(KoanTabAutoCloseTest, OtherHoursAreNever) {
  // A pref edited by hand, or a negative one.
  EXPECT_EQ(std::nullopt, IdleLimit(1, ""));
  EXPECT_EQ(std::nullopt, IdleLimit(-12, ""));
}

TEST(KoanTabAutoCloseTest, TheTestSwitchOverridesTheTime) {
  EXPECT_EQ(base::Seconds(60), IdleLimit(12, "60"));
  EXPECT_EQ(base::Seconds(5), IdleLimit(30 * 24, "5"));
  // Not a whole number of seconds above 0: the setting's time.
  EXPECT_EQ(base::Hours(12), IdleLimit(12, "0"));
  EXPECT_EQ(base::Hours(12), IdleLimit(12, "-5"));
  EXPECT_EQ(base::Hours(12), IdleLimit(12, "1.5"));
  EXPECT_EQ(base::Hours(12), IdleLimit(12, "soon"));
}

TEST(KoanTabAutoCloseTest, CheckIntervals) {
  EXPECT_EQ(base::Minutes(5), CheckInterval(base::Hours(12)));
  EXPECT_EQ(base::Seconds(15), CheckInterval(base::Seconds(60)));
  EXPECT_EQ(base::Seconds(1), CheckInterval(base::Seconds(2)));
  EXPECT_EQ(base::Seconds(30), FirstCheckDelay(base::Hours(12)));
  EXPECT_EQ(base::Seconds(15), FirstCheckDelay(base::Seconds(60)));
}

TEST(KoanTabAutoCloseTest, AnOldNormalTabCloses) {
  EXPECT_EQ(Keep::kNone, Judge(Idle(base::Hours(12))));
  EXPECT_EQ(Keep::kNone, Judge(Idle(base::Days(40))));
}

TEST(KoanTabAutoCloseTest, ARecentTabStays) {
  EXPECT_EQ(Keep::kRecentlyUsed, Judge(Idle(base::Hours(11))));
  EXPECT_EQ(Keep::kRecentlyUsed, Judge(Idle(base::TimeDelta())));
}

TEST(KoanTabAutoCloseTest, ATabUsedInTheFutureStays) {
  // The clock was set back.
  EXPECT_EQ(Keep::kRecentlyUsed, Judge(Idle(-base::Days(3))));
}

TEST(KoanTabAutoCloseTest, ATabWithNoTimeStays) {
  EXPECT_EQ(Keep::kRecentlyUsed, Judge(TabFacts()));
}

TEST(KoanTabAutoCloseTest, EachLimit) {
  for (int hours : kAllowedHours) {
    if (hours == 0) {
      continue;
    }
    const base::TimeDelta limit = base::Hours(hours);
    EXPECT_EQ(Keep::kRecentlyUsed,
              WhyKept(Idle(limit - base::Minutes(1)), {}, Now(), limit))
        << hours;
    EXPECT_EQ(Keep::kNone, WhyKept(Idle(limit), {}, Now(), limit)) << hours;
  }
}

TEST(KoanTabAutoCloseTest, EssentialsStay) {
  TabFacts tab = Idle(base::Days(40));
  tab.essential = true;
  EXPECT_EQ(Keep::kEssential, Judge(tab));
}

TEST(KoanTabAutoCloseTest, PinnedTabsStay) {
  TabFacts tab = Idle(base::Days(40));
  tab.pinned = true;
  EXPECT_EQ(Keep::kPinned, Judge(tab));
}

TEST(KoanTabAutoCloseTest, TabsInAFolderStay) {
  TabFacts tab = Idle(base::Days(40));
  tab.in_folder = true;
  EXPECT_EQ(Keep::kFolder, Judge(tab));
}

TEST(KoanTabAutoCloseTest, TheActiveTabOfASpaceStays) {
  TabFacts tab = Idle(base::Days(40));
  tab.active = true;
  EXPECT_EQ(Keep::kActive, Judge(tab));
}

TEST(KoanTabAutoCloseTest, ASpacesEmptyTabStays) {
  TabFacts tab = Idle(base::Days(40));
  tab.empty_tab = true;
  EXPECT_EQ(Keep::kEmptyTab, Judge(tab));
}

TEST(KoanTabAutoCloseTest, AgentTabsStay) {
  TabFacts tab = Idle(base::Days(40));
  tab.agent = true;
  EXPECT_EQ(Keep::kAgent, Judge(tab));
}

TEST(KoanTabAutoCloseTest, TabsInUseStay) {
  // Sound, a call, screen sharing, DevTools, a half-filled form,
  // downloading, picture-in-picture: the closer folds them into in_use.
  TabFacts tab = Idle(base::Days(40));
  tab.in_use = true;
  EXPECT_EQ(Keep::kInUse, Judge(tab));
}

TEST(KoanTabAutoCloseTest, ASplitViewWithATabThatStaysStaysWhole) {
  const TabFacts old = Idle(base::Days(2));
  TabFacts playing = Idle(base::Days(2));
  playing.in_use = true;
  EXPECT_EQ(Keep::kSplitPartner, WhyKept(old, {playing}, Now(), kLimit));
  EXPECT_EQ(Keep::kSplitPartner,
            WhyKept(old, {Idle(base::Hours(1))}, Now(), kLimit));
  TabFacts shown = Idle(base::Days(2));
  shown.active = true;
  EXPECT_EQ(Keep::kSplitPartner, WhyKept(old, {old, shown}, Now(), kLimit));
}

TEST(KoanTabAutoCloseTest, AnOldSplitViewClosesWhole) {
  const TabFacts old = Idle(base::Days(2));
  EXPECT_EQ(Keep::kNone, WhyKept(old, {old}, Now(), kLimit));
}

TEST(KoanTabAutoCloseTest, LastUsedIsWhenTheTabWasLeft) {
  const base::Time shown = Now() - base::Days(2);
  const base::Time left = Now() - base::Hours(1);
  // Shown two days ago and on screen until an hour ago: used an hour ago.
  EXPECT_EQ(left, LastUsed(shown, left));
  EXPECT_EQ(left, LastUsed(left, shown));
  EXPECT_EQ(shown, LastUsed(shown, base::Time()));
}

TEST(KoanTabAutoCloseTest, TimeCountsAcrossARestart) {
  // A tab last seen 8 days ago, restored now with its session data: it
  // closes at the first check with a 7 day limit.
  const base::Time seen = Now() - base::Days(8);
  const std::string stored = SerializeTime(seen);
  TabFacts tab{.last_used = LastUsed(seen - base::Hours(3), ParseTime(stored))};
  EXPECT_EQ(seen, tab.last_used);
  EXPECT_EQ(Keep::kNone, WhyKept(tab, {}, Now(), base::Days(7)));
}

TEST(KoanTabAutoCloseTest, TimesRoundTrip) {
  EXPECT_EQ(Now(), ParseTime(SerializeTime(Now())));
  EXPECT_TRUE(ParseTime("").is_null());
  EXPECT_TRUE(ParseTime("yesterday").is_null());
  EXPECT_TRUE(ParseTime("-4").is_null());
}

TEST(KoanTabAutoCloseTest, ClosedTabsRoundTrip) {
  ClosedTab tab = Closed("a", Now());
  tab.space = "0123456789ABCDEF0123456789ABCDEF";
  tab.data = {{"koan.container", "user:s1"}};
  EXPECT_EQ(tab, ClosedTab::FromValue(base::Value(tab.ToValue())));
}

TEST(KoanTabAutoCloseTest, ClosedTabsKeepOnlyKoanData) {
  base::DictValue value = Closed("a", Now()).ToValue();
  value.Set("data", base::DictValue()
                        .Set("koan.container", "user:s1")
                        .Set(kLastSeenSessionKey, "1")
                        .Set("other", "x")
                        .Set("koan.number", 3));
  std::optional<ClosedTab> tab =
      ClosedTab::FromValue(base::Value(std::move(value)));
  ASSERT_TRUE(tab);
  EXPECT_EQ((std::map<std::string, std::string>{{"koan.container", "user:s1"}}),
            tab->data);
}

TEST(KoanTabAutoCloseTest, BrokenClosedTabsAreSkipped) {
  base::ListValue list;
  list.Append("not a dict");
  list.Append(Closed("", Now()).ToValue());
  base::DictValue bad_url = Closed("b", Now()).ToValue();
  bad_url.Set("url", "not a url");
  list.Append(std::move(bad_url));
  base::DictValue no_time = Closed("c", Now()).ToValue();
  no_time.Remove("closed");
  list.Append(std::move(no_time));
  list.Append(Closed("d", Now()).ToValue());
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, Now());
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ("d", tabs[0].id);
}

TEST(KoanTabAutoCloseTest, ClosedTabsAreNewestFirst) {
  base::ListValue list;
  list = AddClosedTab(list, Closed("old", Now() - base::Days(1)), Now());
  list = AddClosedTab(list, Closed("new", Now()), Now());
  list.Append(Closed("middle", Now() - base::Hours(1)).ToValue());
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, Now());
  ASSERT_EQ(3u, tabs.size());
  EXPECT_EQ("new", tabs[0].id);
  EXPECT_EQ("middle", tabs[1].id);
  EXPECT_EQ("old", tabs[2].id);
}

TEST(KoanTabAutoCloseTest, ClosedTabsAreNotLostLikeRecentlyClosedOnes) {
  // Far more than the 25 recently closed tabs Chromium keeps.
  base::ListValue list;
  for (size_t i = 0; i < kMaxClosedTabs + 10; ++i) {
    list = AddClosedTab(
        list, Closed(base::NumberToString(i), Now() - base::Seconds(1000 - i)),
        Now());
  }
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, Now());
  ASSERT_EQ(kMaxClosedTabs, tabs.size());
  // The oldest went.
  EXPECT_EQ(base::NumberToString(kMaxClosedTabs + 9), tabs.front().id);
  EXPECT_EQ("10", tabs.back().id);
}

TEST(KoanTabAutoCloseTest, ClosedTabsExpireWithTheLibrarysHistory) {
  base::ListValue list;
  list = AddClosedTab(list, Closed("old", Now() - kKeepClosedTabsFor), Now());
  list = AddClosedTab(list, Closed("kept", Now() - base::Days(100)), Now());
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, Now());
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ("kept", tabs[0].id);
  EXPECT_EQ(1u, list.size());
}

TEST(KoanTabAutoCloseTest, AddingAnIdAgainReplacesIt) {
  base::ListValue list;
  list = AddClosedTab(list, Closed("a", Now() - base::Hours(2)), Now());
  list = AddClosedTab(list, Closed("a", Now()), Now());
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, Now());
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ(Now(), tabs[0].closed);
}

TEST(KoanTabAutoCloseTest, RemovingAClosedTab) {
  base::ListValue list;
  list = AddClosedTab(list, Closed("a", Now()), Now());
  list = AddClosedTab(list, Closed("b", Now()), Now());
  list = RemoveClosedTab(list, "a");
  std::vector<ClosedTab> tabs = ReadClosedTabs(list, Now());
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ("b", tabs[0].id);
  EXPECT_EQ(1u, RemoveClosedTab(list, "missing").size());
}

TEST(KoanTabAutoCloseTest, DeletingHistoryForgetsClosedTabs) {
  base::ListValue list;
  list = AddClosedTab(list, Closed("old", Now() - base::Days(3)), Now());
  list = AddClosedTab(list, Closed("new", Now() - base::Hours(1)), Now());
  list = AddClosedTab(list, Closed("other", Now() - base::Days(2)), Now());
  EXPECT_TRUE(
      ForgetDeletedHistory(list, /*all=*/true, std::nullopt, {}).empty());
  // The last day.
  base::ListValue kept = ForgetDeletedHistory(
      list, /*all=*/false, std::make_pair(Now() - base::Days(1), Now()), {});
  EXPECT_EQ(2u, kept.size());
  // One page.
  kept = ForgetDeletedHistory(list, /*all=*/false, std::nullopt,
                              {GURL("https://example.com/old")});
  std::vector<ClosedTab> tabs = ReadClosedTabs(kept, Now());
  ASSERT_EQ(2u, tabs.size());
  EXPECT_EQ("new", tabs[0].id);
  EXPECT_EQ("other", tabs[1].id);
}

}  // namespace koan::auto_close
