// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// What Koan keeps in the session: Spaces, pinned tabs and the per-tab and
// per-window data of koan_session_data.h. Each test is a PRE_ and a main
// test: the first runs in a browser that is then closed, the second in the
// next one on the same profile, which restores the session ("continue where
// I left off"). Pages are chrome:// ones, which load without a server.

#include <algorithm>
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr char kTabDataKey[] = "koan.test_tab";
constexpr char kWindowDataKey[] = "koan.test_window";
constexpr char16_t kRocket[] = u"\U0001F680";

GURL PinnedUrl() {
  return GURL("chrome://version/");
}

GURL OtherUrl() {
  return GURL("chrome://chrome-urls/");
}

class KoanSessionRestoreBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }

 protected:
  // Waits for the restored window to have `tabs` tabs and `spaces` Spaces.
  // Only for a state a fresh window isn't in (it has one tab and one Space),
  // else it returns before the session is restored; tests that restore
  // that much wait on what they restore.
  void WaitForRestore(int tabs, size_t spaces) {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return browser()->GetTabStripModel()->count() == tabs; }));
    ASSERT_TRUE(WaitForSpaceCount(browser(), spaces));
  }

  // The index of the tab in the Space called `name`, with the given URL, or
  // -1.
  int FindTab(const std::u16string& name, const GURL& url) {
    KoanWorkspaceController* spaces = GetSpaces(browser());
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    for (const auto& space : spaces->GetSpaces()) {
      if (space.name != name) {
        continue;
      }
      for (int index : TabsOfSpace(tab_strip, space.id)) {
        if (tab_strip->GetWebContentsAt(index)->GetVisibleURL() == url) {
          return index;
        }
      }
    }
    return -1;
  }

  // The tab pinned in its Space, or null.
  tabs::TabInterface* FindPinnedTab() {
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    for (int i = 0; i < tab_strip->count(); ++i) {
      if (IsPinnedInSpace(tab_strip->GetTabAtIndex(i))) {
        return tab_strip->GetTabAtIndex(i);
      }
    }
    return nullptr;
  }
};

}  // namespace

// Spaces, their icons and order, and the active Space.
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, PRE_SpacesComeBack) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  spaces->CreateSpace(u"Work", kRocket, {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  spaces->CreateSpace(u"Play", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  spaces->SwitchTo(spaces->GetSpaces()[1].id);
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, SpacesComeBack) {
  // Three tabs and three Spaces: more than a fresh window has.
  ASSERT_NO_FATAL_FAILURE(WaitForRestore(/*tabs=*/3, /*spaces=*/3));
  EXPECT_THAT(SpaceNames(browser()),
              testing::ElementsAre(u"Space", u"Work", u"Play"));
  KoanWorkspaceController* spaces = GetSpaces(browser());
  const auto infos = spaces->GetSpaces();
  EXPECT_EQ(kRocket, infos[1].icon);
  EXPECT_EQ(u"", infos[2].icon);
  // One of them is shown.
  EXPECT_EQ(1, std::ranges::count_if(
                   infos, &KoanWorkspaceController::SpaceInfo::active));
}

// Pinned in the Space, and away from the pinned URL.
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, PRE_PinnedTabComesBack) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  spaces->CreateSpace(u"Work", kRocket, {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PinnedUrl()));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  GetPinnedTabs(browser())->PinInSpace(tab_strip->GetActiveTab());
  ASSERT_TRUE(IsPinnedInSpace(tab_strip->GetActiveTab()));
  ASSERT_EQ(PinnedUrl(), GetTabState(tab_strip->GetActiveTab())->pinned_url);

  // Then away from it.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), OtherUrl()));
  EXPECT_TRUE(IsAwayFromPinnedUrl(tab_strip->GetActiveTab()));
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, PinnedTabComesBack) {
  ASSERT_NO_FATAL_FAILURE(WaitForRestore(/*tabs=*/2, /*spaces=*/2));
  // Pinned tabs stay in their Space, and are still pinned there; the pinned
  // URL came with the session, and the tab is where it was.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return FindTab(u"Work", OtherUrl()) != -1; }));
  const int index = FindTab(u"Work", OtherUrl());
  tabs::TabInterface* tab = browser()->GetTabStripModel()->GetTabAtIndex(index);
  EXPECT_TRUE(IsPinnedInSpace(tab));
  EXPECT_TRUE(IsPinnedForUser(tab));
  ASSERT_TRUE(GetTabState(tab));
  EXPECT_EQ(PinnedUrl(), GetTabState(tab)->pinned_url);
  EXPECT_TRUE(IsAwayFromPinnedUrl(tab));
  // The other Space's tab isn't pinned.
  EXPECT_FALSE(
      IsPinnedForUser(browser()->GetTabStripModel()->GetTabAtIndex(0)));
}

// Zen's "restore pinned tabs to their pinned URL" on startup.
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest,
                       PRE_PinnedTabRestoresToPinnedUrl) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kPinnedTabRestoreToPinnedUrl, true);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PinnedUrl()));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  GetPinnedTabs(browser())->PinInSpace(tab_strip->GetActiveTab());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), OtherUrl()));
  // A second tab, so the restored window differs from a fresh one.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest,
                       PinnedTabRestoresToPinnedUrl) {
  ASSERT_NO_FATAL_FAILURE(WaitForRestore(/*tabs=*/2, /*spaces=*/1));
  ASSERT_TRUE(base::test::RunUntil([&] { return FindPinnedTab() != nullptr; }));
  tabs::TabInterface* tab = FindPinnedTab();
  EXPECT_EQ(PinnedUrl(), tab->GetContents()->GetVisibleURL());
  EXPECT_FALSE(IsAwayFromPinnedUrl(tab));
}

// The data features keep in the session, per tab and per window.
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest,
                       PRE_SessionDataComesBack) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  SetTabSessionData(tab_strip->GetWebContentsAt(0), kTabDataKey, "tab data");
  SetWindowSessionData(browser(), kWindowDataKey, "window data");
  EXPECT_EQ("tab data",
            GetTabSessionData(tab_strip->GetWebContentsAt(0), kTabDataKey));
  EXPECT_EQ("window data", GetWindowSessionData(browser(), kWindowDataKey));
  // A second tab, so the restored window differs from a fresh one.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, SessionDataComesBack) {
  ASSERT_NO_FATAL_FAILURE(WaitForRestore(/*tabs=*/2, /*spaces=*/1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  // The first tab had the data; the second had none.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return GetTabSessionData(tab_strip->GetWebContentsAt(0), kTabDataKey) ==
           "tab data";
  }));
  EXPECT_EQ("", GetTabSessionData(tab_strip->GetWebContentsAt(1), kTabDataKey));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return GetWindowSessionData(browser(), kWindowDataKey) == "window data";
  }));
}

}  // namespace koan
