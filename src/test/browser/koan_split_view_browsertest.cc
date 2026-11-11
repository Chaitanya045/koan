// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Split views and Koan's pinned tabs: a split view is pinned as one, in its
// window and, through window sync, in the others. Pages are chrome:// ones,
// which load without a server.

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
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

}  // namespace koan
