// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Split views: Koan's pinned tabs (a split view is pinned as one, in its
// window and, through window sync, in the others) and Zen's one-tab split
// (an empty pane with the URL bar, koan_split_view.h). Pages are chrome://
// ones, which load without a server.

#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

// The window's tabs pinned in their Space.
int CountPinnedInSpace(BrowserWindowInterface* browser) {
  TabStripModel* tab_strip = browser->GetTabStripModel();
  int count = 0;
  for (int i = 0; i < tab_strip->count(); ++i) {
    count += IsPinnedInSpace(tab_strip->GetTabAtIndex(i)) ? 1 : 0;
  }
  return count;
}

// The window's Koan URL bar.
KoanUrlBarController* UrlBar(BrowserWindowInterface* browser) {
  KoanBrowserViewController* controller = KoanBrowserViewController::From(
      BrowserView::GetBrowserViewForBrowser(browser));
  return controller ? controller->url_bar() : nullptr;
}

// Zen's one-tab split, as every "new split" path asks for it.
void NewEmptySplit(BrowserWindowInterface* browser) {
  chrome::NewSplitTab(browser, split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
}

std::vector<tabs::TabInterface*> SplitTabs(TabStripModel* tab_strip,
                                           tabs::TabInterface* tab) {
  return tab->IsSplit() ? tab_strip->GetSplitData(*tab->GetSplit())->ListTabs()
                        : std::vector<tabs::TabInterface*>();
}

}  // namespace

using KoanSplitViewBrowserTest = InProcessBrowserTest;

// Zen: splitting a pinned tab with a normal one pins both. Picking the pinned
// tab as the second tab of the active normal tab's split (upstream's split
// tab picker, or "Add Split View..." on the normal tab) gives both upstream's
// state of the active tab, unpinned; Koan pins the split. Its tabs stay
// synced, so another window has both pinned (a split's tabs used to leave
// window sync, and the other windows closed their copy of the pinned tab).
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       SplitWithAPinnedTabPinsBothEverywhere) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* pinned = tab_strip->GetActiveTab();
  GetPinnedTabs(browser())->PinInSpace(pinned);
  ASSERT_TRUE(IsPinnedInSpace(pinned));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://chrome-urls/"),
      WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tabs::TabInterface* normal = tab_strip->GetActiveTab();
  ASSERT_FALSE(IsPinnedForUser(normal));

  tab_strip->AddToNewSplit({tab_strip->GetIndexOfTab(pinned)},
                           split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  ASSERT_TRUE(pinned->IsSplit());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return IsPinnedInSpace(pinned) && IsPinnedInSpace(normal); }));
  EXPECT_EQ(pinned->GetSplit(), normal->GetSplit());

  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(WaitForSpaceCount(other, 1));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return CountPinnedInSpace(other) == 2; }));
  // Still pinned and split where the split was made.
  EXPECT_TRUE(IsPinnedInSpace(pinned));
  EXPECT_TRUE(IsPinnedInSpace(normal));
  EXPECT_TRUE(pinned->IsSplit());
}

// Zen's createEmptySplit(): the Space's empty tab joins the tab on its
// right and is selected, with the URL bar open over it as for a new tab.
// Never upstream's tab picker page. Another new split from the empty pane
// does nothing (Zen disables cmd_zenNewEmptySplit there).
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       NewSplitTabOpensAnEmptyPaneWithTheUrlBar) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = tab_strip->GetActiveTab();

  NewEmptySplit(browser());
  tabs::TabInterface* pane = tab_strip->GetActiveTab();
  ASSERT_NE(pane, page);
  EXPECT_TRUE(IsEmptyTab(pane));
  EXPECT_EQ(SplitTabs(tab_strip, page),
            (std::vector<tabs::TabInterface*>{page, pane}));
  EXPECT_EQ(pane->GetGroup(), page->GetGroup());
  ASSERT_TRUE(UrlBar(browser()));
  EXPECT_TRUE(UrlBar(browser())->is_new_tab());
  for (int i = 0; i < tab_strip->count(); ++i) {
    EXPECT_NE(tab_strip->GetWebContentsAt(i)->GetVisibleURL().spec(),
              chrome::kChromeUISplitViewNewTabPageURL);
  }

  const int count = tab_strip->count();
  NewEmptySplit(browser());
  EXPECT_EQ(tab_strip->count(), count);
  EXPECT_EQ(tab_strip->GetActiveTab(), pane);
  EXPECT_EQ(SplitTabs(tab_strip, page).size(), 2u);
}

// What the URL bar opens from the empty pane (a new tab, as the empty tab
// never navigates) takes the pane's place in the split, keeps its layout, and
// is selected; the pane goes.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       TabOpenedFromTheEmptyPaneReplacesIt) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = tab_strip->GetActiveTab();
  NewEmptySplit(browser());
  ASSERT_TRUE(UrlBar(browser())->is_new_tab());
  const tabs::TabHandle pane = tab_strip->GetActiveTab()->GetHandle();

  ui_test_utils::SendToOmniboxAndSubmit(browser(), "chrome://chrome-urls/");
  ASSERT_TRUE(base::test::RunUntil([&] {
    tabs::TabInterface* active = tab_strip->GetActiveTab();
    return active && active != page && active->IsSplit() &&
           active->GetSplit() == page->GetSplit() &&
           active->GetContents()->GetVisibleURL() ==
               GURL("chrome://chrome-urls/");
  }));
  tabs::TabInterface* opened = tab_strip->GetActiveTab();
  EXPECT_EQ(SplitTabs(tab_strip, page),
            (std::vector<tabs::TabInterface*>{page, opened}));
  EXPECT_EQ(
      tab_strip->GetSplitData(*page->GetSplit())->visual_data()->split_layout(),
      split_tabs::SplitTabLayout::kSideBySide);
  EXPECT_TRUE(base::test::RunUntil([&] { return !pane.Get(); }));
  EXPECT_FALSE(UrlBar(browser())->is_new_tab());
}

// The URL bar closed without opening anything (Esc, a click on the page):
// the empty pane leaves the split, and the tab is selected again.
IN_PROC_BROWSER_TEST_F(KoanSplitViewBrowserTest,
                       ClosingTheUrlBarTakesTheEmptyPaneAway) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = tab_strip->GetActiveTab();
  NewEmptySplit(browser());
  ASSERT_TRUE(UrlBar(browser())->is_new_tab());
  tabs::TabInterface* pane = tab_strip->GetActiveTab();

  UrlBar(browser())->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !page->IsSplit() && tab_strip->GetActiveTab() == page; }));
  EXPECT_FALSE(UrlBar(browser())->is_new_tab());
  // The Space's empty tab stays, hidden, as before.
  EXPECT_FALSE(pane->IsSplit());
  EXPECT_TRUE(IsEmptyTab(pane));
}

}  // namespace koan
