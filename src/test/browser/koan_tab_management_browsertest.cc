// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's "When closing a tab, switch to the most recently used tab instead of
// the next tab" (koan.tabs.select_recently_used_on_close, Settings > Tab
// Management), in a window with Spaces: the hook in
// TabStripModel::DetermineNewSelectedIndex (koan::PickRecentlyUsedTab())
// against upstream's neighbour rule. The tabs are switched with a user
// gesture, as a click in the sidebar does, which makes upstream forget the
// openers (an opener still comes first, as in Zen).

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_prefs.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

GURL Page(const std::string& title) {
  return GURL("data:text/html,<title>" + title + "</title>");
}

class KoanTabManagementBrowserTest : public InProcessBrowserTest {
 protected:
  TabStripModel* model() { return browser()->GetTabStripModel(); }

  // A tab at the end, in front, in the active Space.
  tabs::TabInterface* AddTab(const std::string& title) {
    chrome::AddTabAt(browser(), Page(title), -1, /*foreground=*/true);
    tabs::TabInterface* tab = model()->GetActiveTab();
    // Koan adds new tabs to the active Space in a posted task.
    EXPECT_TRUE(
        base::test::RunUntil([&] { return tab->GetGroup().has_value(); }));
    return tab;
  }

  void Show(tabs::TabInterface* tab) {
    model()->ActivateTabAt(
        model()->GetIndexOfTab(tab),
        TabStripUserGestureDetails(
            TabStripUserGestureDetails::GestureType::kMouse));
  }

  void CloseActive() {
    model()->CloseWebContentsAt(model()->active_index(),
                                TabCloseTypes::CLOSE_USER_GESTURE);
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanTabManagementBrowserTest,
                       ClosingSelectsTheMostRecentlyUsedTab) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSelectRecentlyUsedOnClose));
  AddTab("A");
  tabs::TabInterface* b = AddTab("B");
  AddTab("C");
  tabs::TabInterface* d = AddTab("D");
  Show(b);
  Show(d);
  CloseActive();
  EXPECT_EQ(model()->GetActiveTab(), b);
}

IN_PROC_BROWSER_TEST_F(KoanTabManagementBrowserTest,
                       ClosingSelectsTheNeighbourWhenTheSettingIsOff) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kSelectRecentlyUsedOnClose, false);
  AddTab("A");
  tabs::TabInterface* b = AddTab("B");
  tabs::TabInterface* c = AddTab("C");
  tabs::TabInterface* d = AddTab("D");
  Show(b);
  Show(d);
  CloseActive();
  EXPECT_EQ(model()->GetActiveTab(), c);
}

// What the e2e suite does on macOS: the tab closes through WebContents::Close
// (chrome.tabs.remove), and the tabs were switched without a gesture
// (DevTools' bringToFront).
IN_PROC_BROWSER_TEST_F(
    KoanTabManagementBrowserTest,
    ClosingThroughTheExtensionApiSelectsTheMostRecentlyUsed) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  AddTab("A");
  tabs::TabInterface* b = AddTab("B");
  AddTab("C");
  tabs::TabInterface* d = AddTab("D");
  model()->ActivateTabAt(model()->GetIndexOfTab(b));
  model()->ActivateTabAt(model()->GetIndexOfTab(d));
  content::WebContents* closing = d->GetContents();
  closing->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model()->GetIndexOfWebContents(closing) == TabStripModel::kNoTab;
  }));
  EXPECT_EQ(model()->GetActiveTab(), b);
}

// The e2e suite's setup (settings_effects_behaviour.mjs), which failed on the
// Mac (2026-11-12): a second window opened and closed first (its settings
// page), tabs opened the way DevTools' Target.createTarget opens them
// (Navigate in the window, AUTO_TOPLEVEL, a new foreground tab: each gets the
// active tab as its opener, the same one for all), A and then D shown with a
// click (switching between tabs of one opener keeps the openers), D closed
// through WebContents::Close() (chrome.tabs.remove). Upstream's "a tab the
// opener opened" picked C, D's neighbour; Zen picks A, the one used last.
IN_PROC_BROWSER_TEST_F(
    KoanTabManagementBrowserTest,
    ClosingAfterTabsOpenedLikeDevToolsSelectsTheRecentlyUsed) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  CloseBrowserSynchronously(other);
  const auto open = [&](const std::string& title) {
    // With the browser, Navigate() takes its active tab as the source, and
    // the new tab gets it as its opener, as for Target.createTarget.
    NavigateParams params(browser(), Page(title),
                          ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    Navigate(&params);
    tabs::TabInterface* tab = model()->GetActiveTab();
    EXPECT_TRUE(
        base::test::RunUntil([&] { return tab->GetGroup().has_value(); }));
    return tab;
  };
  tabs::TabInterface* a = open("A");
  open("B");
  open("C");
  tabs::TabInterface* d = open("D");
  Show(a);
  Show(d);
  content::WebContents* closing = d->GetContents();
  closing->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model()->GetIndexOfWebContents(closing) == TabStripModel::kNoTab;
  }));
  EXPECT_EQ(model()->GetActiveTab(), a);
}

// The e2e's failure on the Mac (2026-11-12): tabs opened from one page in
// the background (as with Cmd+click) share that opener, and upstream's
// second choice ("a tab the closing tab's opener opened") picked D's
// neighbour C over the tab used last. Zen has no such rule: the opener only
// if it is the closing tab's owner, then the most recently used tab.
IN_PROC_BROWSER_TEST_F(KoanTabManagementBrowserTest,
                       ClosingATabOpenedFromALinkSelectsTheRecentlyUsed) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  tabs::TabInterface* page = AddTab("Page");
  const auto open_from_page = [&](const std::string& title) {
    NavigateParams params(browser(), Page(title), ui::PAGE_TRANSITION_LINK);
    params.source_contents = page->GetContents();
    params.disposition = WindowOpenDisposition::kNewBackgroundTab;
    Navigate(&params);
    tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(
        params.navigated_or_inserted_contents);
    EXPECT_TRUE(
        base::test::RunUntil([&] { return tab->GetGroup().has_value(); }));
    return tab;
  };
  tabs::TabInterface* a = open_from_page("A");
  open_from_page("B");
  open_from_page("C");
  tabs::TabInterface* d = open_from_page("D");
  ASSERT_EQ(model()->GetOpenerOfTabAt(model()->GetIndexOfTab(d)), page);
  Show(a);
  Show(d);
  CloseActive();
  EXPECT_EQ(model()->GetActiveTab(), a);
}

// The tab right after the closing one counts too (the candidates used to
// leave it out: an empty gfx::Range at a block's end is "contained").
IN_PROC_BROWSER_TEST_F(KoanTabManagementBrowserTest,
                       TheTabAfterTheClosingOneCanBeTheRecentlyUsed) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  AddTab("A");
  tabs::TabInterface* b = AddTab("B");
  tabs::TabInterface* c = AddTab("C");
  AddTab("D");
  Show(c);
  Show(b);
  CloseActive();
  EXPECT_EQ(model()->GetActiveTab(), c);
}

}  // namespace koan
