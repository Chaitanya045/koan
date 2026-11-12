// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Window sync (koan_window_sync.h) across two windows. Pages are chrome://
// ones, which load without a server.

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
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

// Opens `url` in a new foreground tab of `browser` and pins it in its Space
// (the Space keeps the tab it had, so it never empties).
tabs::TabInterface* PinNewTab(BrowserWindowInterface* browser,
                              const GURL& url) {
  if (!ui_test_utils::NavigateToURLWithDisposition(
          browser, url, WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP)) {
    return nullptr;
  }
  tabs::TabInterface* tab = browser->GetTabStripModel()->GetActiveTab();
  GetPinnedTabs(browser)->PinInSpace(tab);
  return tab;
}

}  // namespace

using KoanWindowSyncBrowserTest = InProcessBrowserTest;

// Zen's TabClose: a pinned tab closed in one window closes in every window,
// also when another window's change reaches the model between the close and
// the closing window's report (its report was dropped while it was being
// reconciled, and the reconcile put the tab back: e2e window_sync C1).
IN_PROC_BROWSER_TEST_F(KoanWindowSyncBrowserTest,
                       CloseWinsOverAnotherWindowsChange) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(WaitForSpaceCount(other, 1));

  tabs::TabInterface* closing = PinNewTab(browser(), GURL("chrome://version/"));
  ASSERT_TRUE(closing);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return CountPinnedInSpace(other) == 1; }));

  // The other window changes the model first (its report is due before the
  // close's), then the pinned tab closes here.
  ASSERT_TRUE(PinNewTab(other, GURL("chrome://chrome-urls/")));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  tab_strip->CloseWebContentsAt(tab_strip->GetIndexOfTab(closing),
                                TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(CountPinnedInSpace(browser()), 0);

  // Both windows end with the other window's pinned tab only: the closed one
  // isn't put back here, and goes from the other window.
  EXPECT_TRUE(base::test::RunUntil([&] {
    return CountPinnedInSpace(browser()) == 1 && CountPinnedInSpace(other) == 1;
  })) << "pinned here: "
      << CountPinnedInSpace(browser())
      << ", in the other window: " << CountPinnedInSpace(other);
}

}  // namespace koan
