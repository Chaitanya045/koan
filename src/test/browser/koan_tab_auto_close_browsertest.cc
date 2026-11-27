// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// "Close tabs I haven't used for" (#482, koan_tab_auto_closer.h) against a
// real window: an old tab closes into the Library's list and reopens from
// it, Essentials, pinned tabs and a tab playing sound stay, nothing closes
// before the one-time notice or with the setting at Never, and a tab that
// aged while the browser was closed goes after the session is restored. The
// --koan-auto-close-tabs-after switch makes "old" one second.

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_switches.h"
#include "koan/browser/koan_tab_auto_close.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_tab_auto_closer.h"
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

}  // namespace koan
