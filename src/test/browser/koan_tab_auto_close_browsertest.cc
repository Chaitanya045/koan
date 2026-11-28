// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// "Close tabs I haven't used for" (#482, koan_tab_auto_closer.h) against a
// real window: an old tab closes into the Library's list and reopens from
// it, Essentials, pinned tabs and a tab playing sound stay, nothing closes
// before the one-time notice or with the setting at Never, and a tab that
// aged while the browser was closed goes after the session is restored, a
// tab never shown included (#577). The
// --koan-auto-close-tabs-after switch makes "old" one second.

#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_switches.h"
#include "koan/browser/koan_tab_auto_close.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_tab_auto_closer.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "url/gurl.h"

namespace koan {

namespace {

GURL Page(const std::string& title) {
  return GURL("data:text/html,<title>" + title + "</title>");
}

class KoanTabAutoCloseBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kAutoCloseTabsAfter, "1");
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }

 protected:
  TabStripModel* model() { return browser()->GetTabStripModel(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  KoanTabAutoCloser* closer() {
    return KoanBrowserViewController::From(
               BrowserView::GetBrowserViewForBrowser(browser()))
        ->tab_auto_closer();
  }

  // The setting at 12 hours (one second with the switch).
  void TurnOn(bool notice_shown = true) {
    prefs()->SetTime(
        prefs::kAutoCloseNoticeTime,
        notice_shown ? base::Time::Now() - base::Hours(1) : base::Time());
    prefs()->SetInteger(prefs::kAutoCloseTabsAfterHours, 12);
  }

  // A loaded tab at the end of the active Space, never shown.
  tabs::TabInterface* AddBackgroundTab(const std::string& title) {
    chrome::AddTabAt(browser(), Page(title), -1, /*foreground=*/false);
    tabs::TabInterface* tab = model()->GetTabAtIndex(model()->count() - 1);
    EXPECT_TRUE(content::WaitForLoadStop(tab->GetContents()));
    // Koan adds new tabs to the active Space in a posted task.
    EXPECT_TRUE(
        base::test::RunUntil([&] { return tab->GetGroup().has_value(); }));
    return tab;
  }

  bool HasTab(const GURL& url) {
    for (int i = 0; i < model()->count(); ++i) {
      if (CurrentTabUrl(model()->GetWebContentsAt(i)) == url) {
        return true;
      }
    }
    return false;
  }

  // Lets the tabs go unused for longer than the switch's second. The setting
  // is turned on afterwards, so the window's timer runs no check meanwhile.
  void WaitPastTheLimit() {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(1500));
    loop.Run();
  }

  std::vector<auto_close::ClosedTab> Closed() {
    return GetAutoClosedTabs(browser()->GetProfile());
  }
};

// With the setting's own time (12 hours) instead of the test switch's
// second: a tab's age after a restart has to come from before it.
class KoanTabAutoCloseHoursBrowserTest : public KoanTabAutoCloseBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
  }

 protected:
  std::string OpenedUnseen(const std::string& title) {
    for (int i = 0; i < model()->count(); ++i) {
      content::WebContents* contents = model()->GetWebContentsAt(i);
      if (CurrentTabUrl(contents) == Page(title)) {
        return GetTabSessionData(contents, auto_close::kOpenedUnseenSessionKey);
      }
    }
    return std::string();
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest, NeverIsTheDefault) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  EXPECT_EQ(0, prefs()->GetInteger(prefs::kAutoCloseTabsAfterHours));
  AddBackgroundTab("Old");
  WaitPastTheLimit();
  closer()->CheckNowForTesting();
  EXPECT_TRUE(HasTab(Page("Old")));
  EXPECT_TRUE(Closed().empty());
}

IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       AnOldTabClosesIntoTheLibraryAndReopens) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  tabs::TabInterface* old = AddBackgroundTab("Old");
  const tab_groups::TabGroupId space = *old->GetGroup();
  WaitPastTheLimit();
  TurnOn();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Old")); }));

  std::vector<auto_close::ClosedTab> closed = Closed();
  ASSERT_EQ(1u, closed.size());
  EXPECT_EQ(Page("Old"), closed[0].url);
  EXPECT_EQ(u"Old", closed[0].title);
  EXPECT_EQ(space.ToString(), closed[0].space);
  // Not in Chromium's recently closed tabs: Cmd+Shift+T stays the user's.
  if (sessions::TabRestoreService* restore =
          TabRestoreServiceFactory::GetForProfile(browser()->GetProfile())) {
    for (const auto& entry : restore->entries()) {
      EXPECT_NE(sessions::tab_restore::Type::TAB, entry->type);
    }
  }

  // The Library's Reopen: back in its Space, and off the list.
  ASSERT_TRUE(
      ReopenAutoClosedTab(browser(), closed[0].id, /*background=*/false));
  EXPECT_EQ(Page("Old"), CurrentTabUrl(model()->GetActiveWebContents()));
  EXPECT_EQ(space, model()->GetActiveTab()->GetGroup());
  EXPECT_TRUE(Closed().empty());
}

IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       EssentialsPinnedTabsAndATabPlayingSoundStay) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  pinned->AddToEssentials(AddBackgroundTab("Essential"));
  pinned->PinInSpace(AddBackgroundTab("Pinned"));
  content::WebContents* playing = AddBackgroundTab("Playing")->GetContents();
  base::ScopedClosureRunner audible = playing->MarkAudible();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return playing->IsCurrentlyAudible(); }));
  AddBackgroundTab("Old");
  WaitPastTheLimit();
  TurnOn();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Old")); }));
  EXPECT_TRUE(HasTab(Page("Essential")));
  EXPECT_TRUE(HasTab(Page("Pinned")));
  EXPECT_TRUE(HasTab(Page("Playing")));
  // The window's own tab, on screen, stays too.
  EXPECT_TRUE(model()->GetActiveTab());
}

// The newest background tab, never shown, isn't taken for the tab the Space
// on screen shows (that is the active tab): it closes like the others.
IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       TheNewestBackgroundTabClosesToo) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  tabs::TabInterface* active = model()->GetActiveTab();
  AddBackgroundTab("Older");
  AddBackgroundTab("Newest");
  WaitPastTheLimit();
  TurnOn();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !HasTab(Page("Older")) && !HasTab(Page("Newest")); }));
  EXPECT_EQ(active, model()->GetActiveTab());
}

IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       NothingClosesBeforeTheNotice) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  AddBackgroundTab("Old");
  WaitPastTheLimit();
  // Checked before the window's own timer runs.
  TurnOn(/*notice_shown=*/false);
  closer()->CheckNowForTesting();
  // The first check only tells the user.
  const base::Time shown = prefs()->GetTime(prefs::kAutoCloseNoticeTime);
  EXPECT_FALSE(shown.is_null());
  EXPECT_TRUE(HasTab(Page("Old")));
  // A check right after it (the window's timer, or another window's check)
  // closes nothing either: the notice is still up.
  closer()->CheckNowForTesting();
  EXPECT_TRUE(HasTab(Page("Old")));
  EXPECT_EQ(shown, prefs()->GetTime(prefs::kAutoCloseNoticeTime));
  // Once it has been up a while (the switch's 1 s checks: the notice's 10 s).
  prefs()->SetTime(prefs::kAutoCloseNoticeTime,
                   shown - auto_close::NoticeLead(base::Seconds(1)));
  closer()->CheckNowForTesting();
  EXPECT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Old")); }));
}

// Restarted: a tab not used since before the browser closed goes once the
// session is back.
IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       PRE_ATabThatAgedWhileClosedGoesAfterTheRestore) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  AddBackgroundTab("Aged");
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       ATabThatAgedWhileClosedGoesAfterTheRestore) {
  ASSERT_TRUE(base::test::RunUntil([&] { return HasTab(Page("Aged")); }));
  TurnOn();
  ASSERT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Aged")); }));
  std::vector<auto_close::ClosedTab> closed = Closed();
  ASSERT_EQ(1u, closed.size());
  EXPECT_EQ(Page("Aged"), closed[0].url);
}

// Restarted (#577): a tab opened in the background 13 hours ago and never
// shown goes after the restore with the time at 12 hours, though Chromium
// gives it the restore time as its last-active time. One opened just now
// stays, and loses its "opened unseen" time once it is shown.
IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseHoursBrowserTest,
                       PRE_ANeverShownTabKeepsItsAgeAcrossARestart) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  tabs::TabInterface* unseen = AddBackgroundTab("Unseen");
  AddBackgroundTab("Recent");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !OpenedUnseen("Unseen").empty() && !OpenedUnseen("Recent").empty();
  }));
  EXPECT_LE(auto_close::ParseTime(OpenedUnseen("Recent")), base::Time::Now());
  EXPECT_TRUE(GetTabSessionData(model()->GetActiveWebContents(),
                                auto_close::kOpenedUnseenSessionKey)
                  .empty());
  // As if it had been opened 13 hours ago.
  SetTabSessionData(
      unseen->GetContents(), auto_close::kOpenedUnseenSessionKey,
      auto_close::SerializeTime(base::Time::Now() - base::Hours(13)));
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseHoursBrowserTest,
                       ANeverShownTabKeepsItsAgeAcrossARestart) {
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return HasTab(Page("Unseen")) && HasTab(Page("Recent")); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !SessionRestore::IsRestoring(browser()->GetProfile()); }));
  // Its time came back with the session; the restore didn't replace it.
  const base::Time opened = auto_close::ParseTime(OpenedUnseen("Unseen"));
  EXPECT_LT(opened, base::Time::Now() - base::Hours(12));
  TurnOn();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Unseen")); }));
  EXPECT_TRUE(HasTab(Page("Recent")));
  std::vector<auto_close::ClosedTab> closed = Closed();
  ASSERT_EQ(1u, closed.size());
  EXPECT_EQ(Page("Unseen"), closed[0].url);
  EXPECT_FALSE(closed[0].data.contains(auto_close::kOpenedUnseenSessionKey));
  // Shown, it counts from when it is left.
  for (int i = 0; i < model()->count(); ++i) {
    if (CurrentTabUrl(model()->GetWebContentsAt(i)) == Page("Recent")) {
      model()->ActivateTabAt(i);
    }
  }
  EXPECT_TRUE(OpenedUnseen("Recent").empty());
}

// The Library's Clear history for one profile (#474), "Everything": only
// that profile's closed tabs are forgotten. Chromium's notice of the
// deletion doesn't say which profile it was; another profile's closed tab
// stays listed and reopens in its profile.
IN_PROC_BROWSER_TEST_F(KoanTabAutoCloseBrowserTest,
                       ClearingOneProfilesHistoryKeepsTheOthersClosedTabs) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  Profile* profile = browser()->GetProfile();
  prefs()->SetBoolean(prefs::kSpacesSeparateHistory, true);
  std::string work;
  {
    ScopedListPrefUpdate update(prefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), u"Work");
    ASSERT_TRUE(container);
    work = containers::Serialize(container->id);
  }
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  ui_test_utils::WaitForHistoryToLoad(history);

  // A closed tab in the first Space (the default profile).
  AddBackgroundTab("Home");
  WaitPastTheLimit();
  TurnOn();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Home")); }));
  // And one in a Space with the Work profile.
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work_space = *spaces->GetActiveSpace();
  AddBackgroundTab("Work");
  WaitPastTheLimit();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return !HasTab(Page("Work")); }));
  std::vector<auto_close::ClosedTab> closed = Closed();
  ASSERT_EQ(2u, closed.size());
  EXPECT_EQ(Page("Work"), closed[0].url);
  EXPECT_EQ(work, closed[0].data[containers::kContainerSessionKey]);
  EXPECT_EQ(Page("Home"), closed[1].url);

  // A visit of the default profile's, so the deletion has a notice.
  history->AddPage(GURL("https://home.example/"), base::Time::Now(),
                   history::SOURCE_BROWSED);
  // The default profile's history, everything, as the Library clears it.
  {
    HistoryDeletionWaiter deleted(history);
    DeleteProfileHistory(profile, history, /*history_profile=*/std::string(),
                         {}, base::Time());
    // Its notice names the visits gone, not all history (#582).
    EXPECT_FALSE(deleted.Wait().all_history);
  }
  closed = Closed();
  ASSERT_EQ(1u, closed.size());
  EXPECT_EQ(Page("Work"), closed[0].url);

  // It reopens in its Space, in the Work profile.
  ASSERT_TRUE(
      ReopenAutoClosedTab(browser(), closed[0].id, /*background=*/false));
  content::WebContents* reopened = model()->GetActiveWebContents();
  EXPECT_EQ(Page("Work"), CurrentTabUrl(reopened));
  EXPECT_EQ(work_space, model()->GetActiveTab()->GetGroup());
  std::optional<containers::ContainerId> container =
      containers::ContainerOf(reopened);
  ASSERT_TRUE(container);
  EXPECT_EQ(work, containers::Serialize(*container));

  // A deletion that names no profile (Clear browsing data, an extension's
  // chrome.history) still forgets every profile's closed tabs in its range.
  AddBackgroundTab("Work again");
  WaitPastTheLimit();
  closer()->CheckNowForTesting();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !HasTab(Page("Work again")); }));
  ASSERT_EQ(1u, Closed().size());
  history->AddPage(GURL("https://work.example/"), base::Time::Now(),
                   history::SOURCE_BROWSED);
  base::CancelableTaskTracker tracker;
  base::test::TestFuture<void> cleared;
  history->ExpireHistoryBetween(
      {}, std::nullopt, base::Time::Now() - base::Hours(1), base::Time(),
      /*user_initiated=*/true, cleared.GetCallback(), &tracker);
  ASSERT_TRUE(cleared.Wait());
  EXPECT_TRUE(base::test::RunUntil([&] { return Closed().empty(); }));
}

}  // namespace koan
