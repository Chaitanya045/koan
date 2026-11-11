// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// What Koan keeps in the session: Spaces, pinned tabs and the per-tab and
// per-window data of koan_session_data.h. Each test is a PRE_ and a main
// test: the first runs in a browser that is then closed, the second in the
// next one on the same profile, which restores the session ("continue where
// I left off"). Pages are chrome:// ones, which load without a server.

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
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
  // Waits for the restored window to have the session's `tabs` tabs, plus
  // the test launcher's about:blank (InProcessBrowserTest passes it on the
  // command line, and a restore opens command-line URLs after the session's
  // tabs, as upstream's StartupPrefSetAsLastAndURLs expects; it joins the
  // shown Space), and `spaces` Spaces. Only for a state a fresh window isn't
  // in (it has one tab and one Space), else it returns before the session is
  // restored; tests that restore that much wait on what they restore.
  //
  // On failure it lists the window's tabs (index, Space, URL, pinned, Zen's
  // empty tab, session data keys).
  void WaitForRestore(int tabs, size_t spaces) {
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    const int expected = tabs + 1;
    const bool restored =
        base::test::RunUntil([&] { return tab_strip->count() == expected; });
    ASSERT_TRUE(restored) << "expected " << expected << " tabs, got "
                          << tab_strip->count() << ":\n"
                          << DescribeTabs();
    ASSERT_TRUE(WaitForSpaceCount(browser(), spaces)) << DescribeTabs();
  }

  // Saves the session once every tab has loaded: a tab still on its initial
  // entry (one just added, its navigation not committed) isn't saved, as
  // upstream skips that entry.
  void SaveSession() {
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    for (int i = 0; i < tab_strip->count(); ++i) {
      EXPECT_TRUE(content::WaitForLoadStop(tab_strip->GetWebContentsAt(i)));
    }
    FlushSession(browser());
  }

  std::string DescribeTabs() {
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    std::map<int, std::u16string> space_of;
    if (KoanWorkspaceController* spaces = GetSpaces(browser())) {
      for (const auto& space : spaces->GetSpaces()) {
        for (int index : TabsOfSpace(tab_strip, space.id)) {
          space_of[index] = space.name;
        }
      }
    }
    std::string out;
    for (int i = 0; i < tab_strip->count(); ++i) {
      tabs::TabInterface* tab = tab_strip->GetTabAtIndex(i);
      std::string keys;
      for (const auto& [key, value] : GetTabSessionData(tab->GetContents())) {
        keys += " " + key;
      }
      out += base::StringPrintf(
          "  %d [%s] %s%s%s%s data:%s\n", i,
          base::UTF16ToUTF8(space_of.contains(i) ? space_of[i] : u"-").c_str(),
          tab->GetContents()->GetVisibleURL().spec().c_str(),
          tab->IsPinned() ? " essential" : "",
          IsPinnedInSpace(tab) ? " pinned" : "",
          IsEmptyTab(tab) ? " empty-tab" : "", keys.c_str());
    }
    return out;
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

// Without the test launcher's about:blank, which a restore opens after the
// session's tabs and selects.
class KoanSessionRestoreNoUrlBrowserTest
    : public KoanSessionRestoreBrowserTest {
 public:
  KoanSessionRestoreNoUrlBrowserTest() {
    set_open_about_blank_on_browser_launch(false);
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
  SaveSession();
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
  // The window joins window sync once the restore is done (upstream tells
  // its end while it still counts as restoring).
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return KoanWindowSyncClient::From(browser_view) != nullptr; }));
}

// A start brings back the session's Spaces and makes none, even when the
// saved window sync state (prefs::kWindowSyncState) has a Space the session
// doesn't: the restored window is the profile's first synced window, so its
// Spaces only take back their saved ids. (The saved state had drifted from
// the session before restored windows joined window sync, #270, and each
// start added its stale Spaces.)
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, PRE_RestoreMakesNoSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  spaces->CreateSpace(u"Work", kRocket, {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  const auto saved_spaces = [prefs] {
    return prefs->GetDict(prefs::kWindowSyncState).FindList("spaces");
  };
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return saved_spaces() && saved_spaces()->size() == 2u; }));
  base::DictValue state = prefs->GetDict(prefs::kWindowSyncState).Clone();
  state.FindList("spaces")->Append(
      base::DictValue()
          .Set("id", base::Uuid::GenerateRandomV4().AsLowercaseString())
          .Set("name", "Gone")
          .Set("icon", "")
          .Set("theme", ""));
  prefs->SetDict(prefs::kWindowSyncState, std::move(state));
  SaveSession();
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest, RestoreMakesNoSpace) {
  ASSERT_NO_FATAL_FAILURE(WaitForRestore(/*tabs=*/2, /*spaces=*/2));
  // Once the window has joined window sync and nothing is left to apply.
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    KoanWindowSyncClient* client = KoanWindowSyncClient::From(browser_view);
    return client && client->sync()->IsInSync(client);
  }));
  EXPECT_THAT(SpaceNames(browser()), testing::ElementsAre(u"Space", u"Work"))
      << DescribeTabs();
  // The Space shown last is shown.
  const std::optional<tab_groups::TabGroupId> active =
      GetSpaces(browser())->GetActiveSpace();
  ASSERT_TRUE(active);
  EXPECT_EQ(u"Work", GetSpaces(browser())->GetSpace(*active)->name);
  // The saved state is the window's Spaces now, without the stale one.
  std::vector<std::string> saved;
  for (const base::Value& space : *browser()
                                       ->GetProfile()
                                       ->GetPrefs()
                                       ->GetDict(prefs::kWindowSyncState)
                                       .FindList("spaces")) {
    saved.push_back(*space.GetDict().FindString("name"));
  }
  EXPECT_THAT(saved, testing::ElementsAre("Space", "Work"));
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
  SaveSession();
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
  SaveSession();
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
  SaveSession();
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

// A restored window keeps the tabs it had: its first is not taken for Zen's
// empty tab, as a new window's is (Zen's restore selects the session's tab).
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest,
                       PRE_RestoredNewTabPageIsNotTheEmptyTab) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PinnedUrl()));
  ASSERT_FALSE(IsEmptyTab(tab_strip->GetTabAtIndex(0)));
  // The user's own new tab page.
  chrome::AddTabAt(browser(), GURL(), -1, /*foreground=*/false);
  ASSERT_FALSE(IsEmptyTab(tab_strip->GetTabAtIndex(1)));
  SaveSession();
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest,
                       RestoredNewTabPageIsNotTheEmptyTab) {
  ASSERT_NO_FATAL_FAILURE(WaitForRestore(/*tabs=*/2, /*spaces=*/1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  for (int i = 0; i < tab_strip->count(); ++i) {
    EXPECT_FALSE(IsEmptyTab(tab_strip->GetTabAtIndex(i))) << i;
  }
}

// A folder's tabs are kept with their folder (koan.folders.list), not also
// among their Space's pinned tabs (koan.workspaces.list): a start without a
// session restore brings each list back, and would add them twice.
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreBrowserTest,
                       FolderTabsAreStoredOnlyWithTheirFolder) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PinnedUrl()));
  GetPinnedTabs(browser())->PinInSpace(tab_strip->GetActiveTab());
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), OtherUrl(), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  KoanFolderController* folders = KoanFolderController::FromBrowser(browser());
  ASSERT_TRUE(folders);
  folders->CreateFolder({tab_strip->GetActiveTab()}, /*parent=*/0,
                        folders->GetActiveSpace(), u"Folder",
                        /*rename=*/false);
  ASSERT_TRUE(IsPinnedInSpace(tab_strip->GetActiveTab()));

  const PrefService* prefs = browser()->GetProfile()->GetPrefs();
  const auto stored_urls = [](const base::ListValue* tabs) {
    std::vector<std::string> urls;
    if (!tabs) {
      return urls;
    }
    for (const base::Value& tab : *tabs) {
      if (const std::string* url = tab.GetDict().FindString("url")) {
        urls.push_back(*url);
      }
    }
    return urls;
  };
  const auto space_tabs = [&] {
    const base::ListValue& list = prefs->GetList(prefs::kWorkspaceList);
    return list.empty()
               ? std::vector<std::string>()
               : stored_urls(list.front().GetDict().FindList("pinned"));
  };
  const auto folder_tabs = [&] {
    const base::ListValue& list = prefs->GetList(prefs::kFolderList);
    return list.empty() ? std::vector<std::string>()
                        : stored_urls(list.front().GetDict().FindList("tabs"));
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    return space_tabs() == std::vector<std::string>{PinnedUrl().spec()} &&
           folder_tabs() == std::vector<std::string>{OtherUrl().spec()};
  }));
}

// A split view comes back (upstream restores it) and is what the window
// shows. A restore into a new window selects its first tab, an Essential
// here, as it adds it, then the session's selected tab; the Essential was
// taken for the one the window opened on (KeepStartupEssential()) and
// selected again, hiding the split.
IN_PROC_BROWSER_TEST_F(KoanSessionRestoreNoUrlBrowserTest,
                       PRE_SplitViewComesBackShown) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PinnedUrl()));
  GetPinnedTabs(browser())->AddToEssentials(tab_strip->GetActiveTab());
  ASSERT_TRUE(tab_strip->IsTabPinned(0));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), OtherUrl(), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const int other = tab_strip->active_index();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://credits/"),
      WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tab_strip->AddToNewSplit({other}, split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  ASSERT_TRUE(tab_strip->GetActiveTab()->IsSplit());
  SaveSession();
}

IN_PROC_BROWSER_TEST_F(KoanSessionRestoreNoUrlBrowserTest,
                       SplitViewComesBackShown) {
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  // The Essential, the Space's empty tab (Zen's, added when the Space's only
  // tab became an Essential; hidden, but in the session) and the split's two
  // tabs.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab_strip->count() == 4 && !tab_strip->ListSplits().empty();
  })) << DescribeTabs();
  EXPECT_TRUE(IsEmptyTab(tab_strip->GetTabAtIndex(1))) << DescribeTabs();
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  // Once the Spaces' first update (which puts back an Essential the window
  // opened on) has run.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(tab_strip->IsTabPinned(0)) << DescribeTabs();
  tabs::TabInterface* active = tab_strip->GetActiveTab();
  ASSERT_TRUE(active);
  EXPECT_FALSE(active->IsPinned()) << DescribeTabs();
  EXPECT_TRUE(active->IsSplit()) << DescribeTabs();
}

}  // namespace koan
