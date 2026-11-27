// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A start after a crash, against a clean restart: each brings back the
// Spaces, their pinned and normal tabs, titles and order once, as they were
// (#554). After a crash Koan restores the session without asking
// (koan_crash_restore.h) into the window the launch opened; that window had
// been made as for a start without a session (its own Space, and window
// sync's saved pinned tabs as placeholders), and kept both next to the
// restored Spaces.
//
// Each test is a PRE_ and a main test (see
// koan_session_restore_browsertest.cc): the PRE_ test sets up two Spaces with a
// pinned tab, saves the session, and writes down what the window holds; it ends
// cleanly or as a crash would. The main test is the next start on the same
// profile, which must hold the same.

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_window_sync.h"
#include "koan/browser/ui/views/koan_window_sync_client.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr char16_t kRocket[] = u"\U0001F680";

GURL PinnedUrl() {
  return GURL("chrome://version/");
}

GURL OtherUrl() {
  return GURL("chrome://chrome-urls/");
}

GURL WorkUrl() {
  return GURL("chrome://credits/");
}

GURL ReadUrl() {
  return GURL("chrome://version/?read");
}

GURL ReadUrl2() {
  return GURL("chrome://credits/?read2");
}

class KoanCrashRestoreBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }

 protected:
  // Two Spaces: "Space", with a tab pinned in it and a normal tab, and
  // "Work" (with an icon) with a normal tab, shown. With `with_read`, a third
  // one, "Read", with two tabs, the first selected. `shown` names the Space
  // shown last (empty: "Work"). The session is on disk and the window's
  // state written down (ExpectedFile()).
  void SetUpSpaces(bool with_read = false,
                   const std::u16string& shown = std::u16string()) {
    KoanWorkspaceController* spaces = GetSpaces(browser());
    ASSERT_TRUE(spaces);
    ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PinnedUrl()));
    tabs::TabInterface* pinned = tab_strip->GetActiveTab();
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), OtherUrl(), WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    GetPinnedTabs(browser())->PinInSpace(pinned);
    ASSERT_TRUE(IsPinnedInSpace(pinned));
    spaces->CreateSpace(u"Work", kRocket, {});
    ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkUrl()));
    if (with_read) {
      spaces->CreateSpace(u"Read", u"", {});
      ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
      ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), ReadUrl()));
      tabs::TabInterface* read = tab_strip->GetActiveTab();
      ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), ReadUrl2(), WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
      tab_strip->ActivateTabAt(tab_strip->GetIndexOfTab(read));
    }
    if (!shown.empty()) {
      for (const KoanWorkspaceController::SpaceInfo& space :
           spaces->GetSpaces()) {
        if (space.name == shown) {
          spaces->SwitchTo(space.id);
        }
      }
    }
    for (int i = 0; i < tab_strip->count(); ++i) {
      ASSERT_TRUE(content::WaitForLoadStop(tab_strip->GetWebContentsAt(i)));
    }
    // Settled: the Spaces' updates and window sync's reports have run.
    base::RunLoop().RunUntilIdle();
    FlushSession(browser());
    const std::string state = Describe(/*launch_tabs=*/nullptr);
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(ExpectedFile(), state));
  }

  // Ends this run as a second crash within a minute of a silent restore:
  // the next start asks with the bubble.
  void EndAsSecondCrash() {
    PrefService* prefs = browser()->GetProfile()->GetPrefs();
    prefs->SetInteger(prefs::kResumedCrashes, 1);
    prefs->SetTime(prefs::kLastResumedTime, base::Time::Now());
    EndAsCrash();
  }

  void CheckLateRestore(const std::u16string& session_shown,
                        const GURL& session_tab);

  // Ends this run as a crash would: the last session's exit type stays
  // "crashed" (ExitTypeService writes "clean" at exit only once the crash
  // of the run before is acknowledged), as upstream's ExitTypeServiceTest
  // does.
  void EndAsCrash() {
    ExitTypeService::GetInstanceForProfile(browser()->GetProfile())
        ->SetWaitingForUserToAckCrashForTest(true);
  }

  base::FilePath ExpectedFile() {
    return browser()->GetProfile()->GetPath().AppendASCII(
        "koan_test_expected_tabs.txt");
  }

  std::string Expected() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string state;
    EXPECT_TRUE(base::ReadFileToString(ExpectedFile(), &state));
    return state;
  }

  // The window's Essentials and Spaces in order, each Space's tabs with their
  // URL, title and kind. Tabs on about:blank (the test launcher's URL, given
  // on the command line as a user's would be) are left out and listed in
  // `launch_tabs` as "<Space name>[ active]".
  std::string Describe(std::vector<std::string>* launch_tabs) {
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    std::string out;
    for (int i = 0; i < tab_strip->count(); ++i) {
      if (tab_strip->IsTabPinned(i)) {
        out += base::StrCat(
            {"essential ",
             tab_strip->GetWebContentsAt(i)->GetVisibleURL().spec(), "\n"});
      }
    }
    KoanWorkspaceController* spaces = GetSpaces(browser());
    if (!spaces) {
      return out + "no Spaces\n";
    }
    for (const KoanWorkspaceController::SpaceInfo& space :
         spaces->GetSpaces()) {
      const std::string name = base::UTF16ToUTF8(space.icon + space.name);
      out += base::StrCat({"space ", name, space.active ? " shown" : "", "\n"});
      for (int index : TabsOfSpace(tab_strip, space.id)) {
        tabs::TabInterface* tab = tab_strip->GetTabAtIndex(index);
        content::WebContents* contents = tab->GetContents();
        if (contents->GetVisibleURL().IsAboutBlank()) {
          if (launch_tabs) {
            launch_tabs->push_back(base::StrCat(
                {name, tab_strip->active_index() == index ? " active" : ""}));
          }
          continue;
        }
        out += base::StrCat({"  ", contents->GetVisibleURL().spec(), " \"",
                             base::UTF16ToUTF8(contents->GetTitle()), "\"",
                             IsPinnedInSpace(tab) ? " pinned" : "",
                             IsEmptyTab(tab) ? " empty-tab" : "", "\n"});
      }
    }
    return out;
  }

  // The URLs of the tabs of the Space called `name`, in order.
  std::vector<std::string> SpaceUrls(const std::u16string& name) {
    std::vector<std::string> urls;
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    for (const KoanWorkspaceController::SpaceInfo& space :
         GetSpaces(browser())->GetSpaces()) {
      if (space.name == name) {
        for (int index : TabsOfSpace(tab_strip, space.id)) {
          urls.push_back(
              tab_strip->GetWebContentsAt(index)->GetVisibleURL().spec());
        }
      }
    }
    return urls;
  }

  // Waits until the window holds what the last run wrote down, has joined
  // window sync with nothing left to apply (which would add the saved pinned
  // tabs as placeholders, if it had none of them), and still holds it then.
  // Returns the launch tabs (Describe()).
  std::vector<std::string> WaitForRestoredState() {
    const std::string expected = Expected();
    std::vector<std::string> launch_tabs;
    const auto matches = [&] {
      launch_tabs.clear();
      return Describe(&launch_tabs) == expected;
    };
    EXPECT_TRUE(base::test::RunUntil(matches)) << "expected:\n"
                                               << expected << "got:\n"
                                               << Describe(nullptr);
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    EXPECT_TRUE(base::test::RunUntil([&] {
      KoanWindowSyncClient* client = KoanWindowSyncClient::From(browser_view);
      return client && client->sync()->IsInSync(client);
    }));
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(matches()) << "expected:\n"
                           << expected << "got:\n"
                           << Describe(nullptr);
    return launch_tabs;
  }
};

// Without the test launcher's about:blank: the launch opens on the New Tab
// page, as a start without a URL does.
class KoanCrashRestoreNoUrlBrowserTest : public KoanCrashRestoreBrowserTest {
 public:
  KoanCrashRestoreNoUrlBrowserTest() {
    set_open_about_blank_on_browser_launch(false);
  }
};

}  // namespace

// A clean restart restores the session at launch.
IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       PRE_CleanRestartBringsBackEachSpaceOnce) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces());
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       CleanRestartBringsBackEachSpaceOnce) {
  // The launch's URL opens once, in the Space shown, and is selected.
  EXPECT_THAT(WaitForRestoredState(),
              testing::ElementsAre(
                  base::StrCat({base::UTF16ToUTF8(kRocket), "Work active"})));
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreNoUrlBrowserTest,
                       PRE_CleanRestartBringsBackEachSpaceOnce) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces());
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreNoUrlBrowserTest,
                       CleanRestartBringsBackEachSpaceOnce) {
  EXPECT_THAT(WaitForRestoredState(), testing::IsEmpty());
}

// After a crash the same comes back, once: the launch's own Space goes, its
// URL's tab joins the restored Space shown, and no saved pinned tab comes
// back a second time.
IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       PRE_CrashBringsBackEachSpaceOnce) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces());
  EndAsCrash();
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       CrashBringsBackEachSpaceOnce) {
  ASSERT_EQ(ExitType::kCrashed,
            ExitTypeService::GetLastSessionExitType(browser()->GetProfile()));
  EXPECT_THAT(WaitForRestoredState(),
              testing::ElementsAre(
                  base::StrCat({base::UTF16ToUTF8(kRocket), "Work active"})));
  // Restored without asking.
  EXPECT_EQ(1, browser()->GetProfile()->GetPrefs()->GetInteger(
                   prefs::kResumedCrashes));
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreNoUrlBrowserTest,
                       PRE_CrashBringsBackEachSpaceOnce) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces());
  EndAsCrash();
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreNoUrlBrowserTest,
                       CrashBringsBackEachSpaceOnce) {
  ASSERT_EQ(ExitType::kCrashed,
            ExitTypeService::GetLastSessionExitType(browser()->GetProfile()));
  EXPECT_THAT(WaitForRestoredState(), testing::IsEmpty());
}

// A crash soon after a silent restore asks first (the bubble, as upstream),
// and the window waits for the answer: nothing restored, and not joined to
// window sync, whose saved pinned tabs would come first. Its Restore brings
// back each Space once.
IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       PRE_SecondCrashAsksAndRestoresOnce) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces());
  EndAsSecondCrash();
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       SecondCrashAsksAndRestoresOnce) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, browser()->GetProfile()->GetPrefs()->GetInteger(
                   prefs::kResumedCrashes));
  EXPECT_EQ(1, browser()->GetTabStripModel()->count());
  EXPECT_FALSE(KoanWindowSyncClient::From(
      BrowserView::GetBrowserViewForBrowser(browser())));
  // The bubble's Restore.
  SessionRestore::RestoreSessionAfterCrash(browser());
  const std::string expected = Expected();
  std::vector<std::string> launch_tabs;
  EXPECT_TRUE(base::test::RunUntil([&] {
    launch_tabs.clear();
    return Describe(&launch_tabs) == expected;
  })) << "expected:\n"
      << expected << "got:\n"
      << Describe(nullptr);
  EXPECT_THAT(launch_tabs, testing::ElementsAre(base::StrCat(
                               {base::UTF16ToUTF8(kRocket), "Work active"})));
}

// The bubble answered late (#554, steps 5 and 5b of the PR's test): the
// user opened tabs in the launch's Space, then made another Space, which
// Create Space shows, and pressed Restore from there. The session comes
// back as it was: the launch Space's tabs go to the end of the Space the
// session showed, which is shown again on the session's selected tab; the
// Space the user made stays, neither merged nor shown. `session_shown` is
// that Space, `session_tab` its selected tab.
void KoanCrashRestoreBrowserTest::CheckLateRestore(
    const std::u16string& session_shown,
    const GURL& session_tab) {
  const GURL first("chrome://version/?l1");
  const GURL second("chrome://version/?l2");
  const GURL later("chrome://credits/?later");
  KoanWorkspaceController* spaces = GetSpaces(browser());
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  base::RunLoop().RunUntilIdle();
  // Not restored: the bubble asks.
  ASSERT_EQ(1, tab_strip->count()) << Describe(nullptr);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), first, WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), second, WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  spaces->CreateSpace(u"Later", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  // On "Later", as a user is after Create Space.
  const auto shown_name = [&] {
    const std::optional<tab_groups::TabGroupId> active =
        spaces->GetActiveSpace();
    return active ? spaces->GetSpace(*active)->name : std::u16string();
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return shown_name() == u"Later"; }));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), later));
  ASSERT_THAT(SpaceNames(browser()), testing::ElementsAre(u"Space", u"Later"));
  ASSERT_EQ(later, tab_strip->GetActiveWebContents()->GetVisibleURL());

  // The bubble's Restore.
  SessionRestore::RestoreSessionAfterCrash(browser());
  const std::vector<std::u16string> names = {u"Later", u"Space", u"Work",
                                             u"Read"};
  ASSERT_TRUE(base::test::RunUntil([&] {
    return SpaceNames(browser()) == names;
  })) << Describe(nullptr);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(names, SpaceNames(browser())) << Describe(nullptr);
  // Each tab once: the session's, then in the Space it showed, the launch's
  // about:blank and the tabs opened beside it.
  const std::vector<std::string> launch_tabs = {"about:blank", first.spec(),
                                                second.spec()};
  std::vector<std::string> space = {PinnedUrl().spec(), OtherUrl().spec()};
  std::vector<std::string> read = {ReadUrl().spec(), ReadUrl2().spec()};
  std::vector<std::string>& merged = session_shown == u"Read" ? read : space;
  merged.insert(merged.end(), launch_tabs.begin(), launch_tabs.end());
  EXPECT_EQ(space, SpaceUrls(u"Space")) << Describe(nullptr);
  EXPECT_EQ(read, SpaceUrls(u"Read")) << Describe(nullptr);
  EXPECT_THAT(SpaceUrls(u"Work"), testing::ElementsAre(WorkUrl().spec()));
  EXPECT_THAT(SpaceUrls(u"Later"), testing::ElementsAre(later.spec()));
  // Still pinned in its Space.
  bool pinned = false;
  for (int i = 0; i < tab_strip->count(); ++i) {
    if (tab_strip->GetWebContentsAt(i)->GetVisibleURL() == PinnedUrl()) {
      pinned = IsPinnedInSpace(tab_strip->GetTabAtIndex(i));
    }
  }
  EXPECT_TRUE(pinned) << Describe(nullptr);
  // The Space the session showed, on its selected tab; not "Later".
  EXPECT_EQ(session_shown, shown_name()) << Describe(nullptr);
  EXPECT_EQ(session_tab, tab_strip->GetActiveWebContents()->GetVisibleURL())
      << Describe(nullptr);
}

// Step 5: the session showed "Space".
IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       PRE_LateRestoreMergesTheLaunchSpace) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces(/*with_read=*/true, u"Space"));
  EndAsSecondCrash();
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       LateRestoreMergesTheLaunchSpace) {
  CheckLateRestore(u"Space", OtherUrl());
}

// Step 5b: the session showed "Read", on its first tab.
IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       PRE_LateRestoreMergesTheLaunchSpaceIntoTheShownOne) {
  ASSERT_NO_FATAL_FAILURE(SetUpSpaces(/*with_read=*/true, u"Read"));
  EndAsSecondCrash();
}

IN_PROC_BROWSER_TEST_F(KoanCrashRestoreBrowserTest,
                       LateRestoreMergesTheLaunchSpaceIntoTheShownOne) {
  CheckLateRestore(u"Read", ReadUrl());
}

}  // namespace koan
