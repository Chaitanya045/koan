// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's tab drops between a Space and the Essentials
// (ZenPinnedTabManager.moveToAnotherTabContainerIfNecessary()), as
// KoanSpaceDrag makes them once a tab drag is over: the drop itself needs a
// real pointer (the Mac steps in the PR), these check what it does to the
// tabs. And that a drag in Koan's sidebar never tears off into a new window.

#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_pinned.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_tab_drag_hooks.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

class KoanEssentialsDropBrowserTest : public InProcessBrowserTest {
 protected:
  // Opens `count` normal tabs in the active Space and returns them.
  std::vector<tabs::TabInterface*> OpenTabs(int count) {
    std::vector<tabs::TabInterface*> tabs;
    for (int i = 0; i < count; ++i) {
      EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), GURL("chrome://version/"),
          WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
      tabs.push_back(browser()->GetTabStripModel()->GetActiveTab());
    }
    return tabs;
  }

  // The Essentials, in the grid's order.
  std::vector<tabs::TabInterface*> Essentials() {
    TabStripModel* model = browser()->GetTabStripModel();
    std::vector<tabs::TabInterface*> essentials;
    for (int i = 0; i < model->IndexOfFirstNonPinnedTab(); ++i) {
      essentials.push_back(model->GetTabAtIndex(i));
    }
    return essentials;
  }
};

}  // namespace

// The user's ask: a tab dragged out of Koan's sidebar or window stays in it
// (TabDragController's hook asks this at the point it would tear off).
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       TabDragsNeverTearOffIntoANewWindow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  views::Widget* widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  ASSERT_TRUE(widget);
  EXPECT_TRUE(KeepsTabDragInWindow(widget));
  EXPECT_FALSE(KeepsTabDragInWindow(nullptr));
}

// The user's report: the first tab dropped on the (empty) Essentials worked,
// a second one didn't. Each next tab joins the grid where it drops: before
// the tile it is dropped on, or after the last one.
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       EveryDroppedTabBecomesAnEssentialWhereItDrops) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(6);

  // The first, on the promo.
  pinned->DropInEssentials({tabs[0]}, nullptr);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[0]));
  // The second and third, after the last tile.
  pinned->DropInEssentials({tabs[1]}, nullptr);
  pinned->DropInEssentials({tabs[2]}, nullptr);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[0], tabs[1], tabs[2]));
  // On the first tile's near half: first.
  pinned->DropInEssentials({tabs[3]}, tabs[0]);
  EXPECT_THAT(Essentials(),
              testing::ElementsAre(tabs[3], tabs[0], tabs[1], tabs[2]));
  // Two selected tabs between tiles: together, in their order.
  pinned->DropInEssentials({tabs[4], tabs[5]}, tabs[1]);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[3], tabs[0], tabs[4],
                                                 tabs[5], tabs[1], tabs[2]));
  for (tabs::TabInterface* tab : tabs) {
    EXPECT_TRUE(tab->IsPinned());
    EXPECT_FALSE(tab->GetGroup());
  }
}

// Zen's zen.tabs.essentials.max: a drop on a full grid leaves the tabs in
// their Space; of several, those that fit go.
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       DropsStopAtTwelveEssentials) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  const tab_groups::TabGroupId space = *GetSpaces(browser())->GetActiveSpace();
  std::vector<tabs::TabInterface*> tabs =
      OpenTabs(KoanPinnedTabController::kMaxEssentials + 1);
  const std::vector<tabs::TabInterface*> first(tabs.begin(), tabs.end() - 2);
  pinned->DropInEssentials(first, nullptr);
  ASSERT_EQ(KoanPinnedTabController::kMaxEssentials - 1,
            pinned->EssentialsCount());

  // Two more, room for one.
  pinned->DropInEssentials({tabs.end()[-2], tabs.end()[-1]}, nullptr);
  EXPECT_EQ(KoanPinnedTabController::kMaxEssentials, pinned->EssentialsCount());
  EXPECT_TRUE(tabs.end()[-2]->IsPinned());
  EXPECT_FALSE(tabs.end()[-1]->IsPinned());
  EXPECT_EQ(space, tabs.end()[-1]->GetGroup());
}

// Zen: an Essential dropped among the Space's pinned tabs becomes one of
// them, keeping its pinned URL; dropped among its other tabs, a normal tab.
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       AnEssentialDroppedInTheSpaceTakesThatPlace) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  TabStripModel* model = browser()->GetTabStripModel();
  const tab_groups::TabGroupId space = *GetSpaces(browser())->GetActiveSpace();
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(4);
  tabs::TabInterface* space_pinned = tabs[0];
  tabs::TabInterface* normal = tabs[1];
  pinned->PinInSpace(space_pinned);
  ASSERT_TRUE(IsPinnedInSpace(space_pinned));
  pinned->DropInEssentials({tabs[2], tabs[3]}, nullptr);
  ASSERT_TRUE(tabs[2]->IsPinned());
  ASSERT_TRUE(tabs[3]->IsPinned());

  // On the Space's pinned tab: before it, one of the Space's pinned tabs.
  pinned->DropEssentialInSpace(tabs[2], space_pinned);
  EXPECT_FALSE(tabs[2]->IsPinned());
  EXPECT_EQ(space, tabs[2]->GetGroup());
  EXPECT_EQ(model->GetIndexOfTab(space_pinned) - 1,
            model->GetIndexOfTab(tabs[2]));
  EXPECT_TRUE(base::test::RunUntil([&] { return IsPinnedInSpace(tabs[2]); }));

  // Below the Space's last tab: a normal tab there. The active tab, as a
  // dragged one is: unpinning it makes upstream leave the Space's focus mode,
  // and the window must still show the Space, on that tab.
  ASSERT_EQ(tabs[3], model->GetActiveTab());
  pinned->DropEssentialInSpace(tabs[3], nullptr);
  EXPECT_EQ(space, GetSpaces(browser())->GetActiveSpace());
  EXPECT_EQ(tabs[3], model->GetActiveTab());
  EXPECT_FALSE(tabs[3]->IsPinned());
  EXPECT_EQ(space, tabs[3]->GetGroup());
  EXPECT_EQ(TabsOfSpace(model, space).back(), model->GetIndexOfTab(tabs[3]));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !IsPinnedForUser(tabs[3]) &&
           model->GetIndexOfTab(tabs[3]) > model->GetIndexOfTab(normal);
  }));
  EXPECT_TRUE(IsPinnedInSpace(tabs[2]));
  EXPECT_FALSE(IsPinnedForUser(normal));
}

}  // namespace koan
