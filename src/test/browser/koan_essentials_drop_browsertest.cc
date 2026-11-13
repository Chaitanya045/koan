// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's tab drops between a Space and the Essentials
// (ZenPinnedTabManager.moveToAnotherTabContainerIfNecessary()), as Koan's
// tab drag (koan_tab_drag.h) makes them at the drop: the drop itself needs a
// real pointer (the Mac steps in the PR), these check what it does to the
// tabs. And that a drag in Koan's sidebar never tears off into a new window.

#include <optional>
#include <vector>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
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

// Zen: an Essential dropped in the Space's pinned section becomes one of its
// pinned tabs, keeping its pinned URL; dropped in its normal section, a
// normal tab.
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
  pinned->DropEssentialInSpace(tabs[2], space_pinned, /*pinned=*/true);
  EXPECT_FALSE(tabs[2]->IsPinned());
  EXPECT_EQ(space, tabs[2]->GetGroup());
  EXPECT_EQ(model->GetIndexOfTab(space_pinned) - 1,
            model->GetIndexOfTab(tabs[2]));
  EXPECT_TRUE(IsPinnedInSpace(tabs[2]));

  // Below the Space's last tab: a normal tab there. The active tab, as a
  // dragged one is: unpinning it makes upstream leave the Space's focus mode,
  // and the window must still show the Space, on that tab.
  ASSERT_EQ(tabs[3], model->GetActiveTab());
  pinned->DropEssentialInSpace(tabs[3], nullptr, /*pinned=*/false);
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

// The user's report: an Essential dragged to the Space's normal tabs became a
// pinned tab. Right below the separator and right above it are the same
// place among the tabs (after the last pinned one, before the first normal
// one): the section it was dropped in decides, and only a pinned tab keeps
// the pinned URL.
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       AnEssentialDroppedAtTheSeparatorTakesItsSection) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  TabStripModel* model = browser()->GetTabStripModel();
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(4);
  pinned->PinInSpace(tabs[0]);
  pinned->DropInEssentials({tabs[2], tabs[3]}, nullptr);
  ASSERT_TRUE(tabs[2]->IsPinned() && tabs[3]->IsPinned());
  // The first normal tab, right after the pinned one.
  const int boundary = model->GetIndexOfTab(tabs[0]) + 1;
  tabs::TabInterface* first_normal = model->GetTabAtIndex(boundary);
  ASSERT_FALSE(IsPinnedForUser(first_normal));

  // Above the separator: the Space's last pinned tab, with its pinned URL.
  pinned->DropEssentialInSpace(tabs[2], first_normal, /*pinned=*/true);
  EXPECT_TRUE(IsPinnedInSpace(tabs[2]));
  EXPECT_EQ(model->GetIndexOfTab(tabs[0]) + 1, model->GetIndexOfTab(tabs[2]));
  ASSERT_TRUE(GetTabState(tabs[2]));
  EXPECT_TRUE(GetTabState(tabs[2])->pinned_url.is_valid());

  // Below it: the first normal tab, without one.
  pinned->DropEssentialInSpace(tabs[3], first_normal, /*pinned=*/false);
  EXPECT_FALSE(IsPinnedForUser(tabs[3]));
  EXPECT_EQ(model->GetIndexOfTab(tabs[2]) + 1, model->GetIndexOfTab(tabs[3]));
  EXPECT_EQ(model->GetIndexOfTab(tabs[3]) + 1,
            model->GetIndexOfTab(first_normal));
  ASSERT_TRUE(GetTabState(tabs[3]));
  EXPECT_FALSE(GetTabState(tabs[3])->pinned_url.is_valid());

  // And so it stays once Koan re-checks the moved tabs.
  base::RunLoop run_loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), base::Seconds(1));
  run_loop.Run();
  EXPECT_TRUE(IsPinnedInSpace(tabs[2]));
  EXPECT_FALSE(IsPinnedForUser(tabs[3]));
}

// Zen's drops on a Space's list: the section under the pointer pins or
// unpins the tabs, and they go before the row the line is at (or to the end
// of the section), in their order.
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       DroppedTabsTakeTheSectionTheyDropIn) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  TabStripModel* model = browser()->GetTabStripModel();
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(5);
  pinned->PinInSpace(tabs[0]);
  const int first = model->GetIndexOfTab(tabs[0]);
  const auto at = [&](int offset) {
    return model->GetTabAtIndex(first + offset);
  };
  tabs::TabInterface* p = at(0);
  tabs::TabInterface* a = at(1);
  tabs::TabInterface* b = at(2);
  tabs::TabInterface* c = at(3);
  ASSERT_FALSE(IsPinnedForUser(a) || IsPinnedForUser(b));

  // At the end of the pinned section: pinned, after the pinned tab, on its
  // current page.
  pinned->DropTabsInSpace({a}, nullptr, /*pinned=*/true);
  EXPECT_TRUE(IsPinnedInSpace(a));
  EXPECT_EQ(a, at(1));
  ASSERT_TRUE(GetTabState(a));
  EXPECT_TRUE(GetTabState(a)->pinned_url.is_valid());
  // Before the first normal tab: unpinned, its pinned URL gone (Zen's
  // unpinTab()), where it was.
  pinned->DropTabsInSpace({a}, b, /*pinned=*/false);
  EXPECT_FALSE(IsPinnedForUser(a));
  EXPECT_EQ(a, at(1));
  EXPECT_FALSE(GetTabState(a)->pinned_url.is_valid());

  // Two dropped together before the pinned tab: both pinned, above it, in
  // their order.
  pinned->DropTabsInSpace({b, c}, p, /*pinned=*/true);
  EXPECT_TRUE(IsPinnedInSpace(b));
  EXPECT_TRUE(IsPinnedInSpace(c));
  EXPECT_EQ(b, at(0));
  EXPECT_EQ(c, at(1));
  EXPECT_EQ(p, at(2));

  // The pinned one dropped at the end of the normal section: last, unpinned.
  pinned->DropTabsInSpace({b}, nullptr, /*pinned=*/false);
  EXPECT_FALSE(IsPinnedForUser(b));
  const auto last_of_space = [&] {
    return model->GetTabAtIndex(TabsOfSpace(model, *b->GetGroup()).back());
  };
  EXPECT_EQ(b, last_of_space());

  // Where they were dropped, once Koan re-checks the moved tabs.
  base::RunLoop run_loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), base::Seconds(1));
  run_loop.Run();
  EXPECT_EQ(c, at(0));
  EXPECT_EQ(p, at(1));
  EXPECT_TRUE(IsPinnedInSpace(c));
  EXPECT_TRUE(IsPinnedInSpace(p));
  EXPECT_FALSE(IsPinnedForUser(a));
  EXPECT_FALSE(IsPinnedForUser(b));
  EXPECT_EQ(b, last_of_space());
}

// Essentials dropped on the grid again: a reorder, before the tile the line
// is at, or last.
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       EssentialsDroppedOnTheGridReorder) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(3);
  pinned->DropInEssentials(tabs, nullptr);
  ASSERT_THAT(Essentials(), testing::ElementsAre(tabs[0], tabs[1], tabs[2]));
  pinned->DropInEssentials({tabs[2]}, tabs[0]);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[2], tabs[0], tabs[1]));
  pinned->DropInEssentials({tabs[2], tabs[0]}, nullptr);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[1], tabs[2], tabs[0]));
}

// Zen's addToEssentials() for a pinned tab dropped on the grid: Koan's
// Space-pinned tabs are not Chromium-pinned, so the drag counts them with the
// Space's tabs (not Essentials) and the drop makes them Essentials like any
// other, out of the Space's pinned section, keeping the pinned URL they had
// (not the page they show now).
IN_PROC_BROWSER_TEST_F(KoanEssentialsDropBrowserTest,
                       APinnedTabDroppedOnTheGridKeepsItsPinnedUrl) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(3);
  pinned->PinInSpace(tabs[0]);
  pinned->PinInSpace(tabs[1]);
  ASSERT_TRUE(IsPinnedInSpace(tabs[0]) && IsPinnedInSpace(tabs[1]));
  // A pinned URL of its own ("Edit Pinned URL…").
  pinned->SetPinnedUrl(tabs[0], GURL("chrome://version/?pinned"));
  ASSERT_TRUE(GetTabState(tabs[0]));
  const GURL kept = GetTabState(tabs[0])->pinned_url;
  ASSERT_TRUE(kept.is_valid());

  // No Essentials yet (the promo's drop): the first one.
  ASSERT_EQ(0, pinned->EssentialsCount());
  pinned->DropInEssentials({tabs[0]}, nullptr);
  EXPECT_TRUE(tabs[0]->IsPinned());
  EXPECT_FALSE(IsPinnedInSpace(tabs[0]));
  EXPECT_FALSE(tabs[0]->GetGroup());
  EXPECT_EQ(kept, GetTabState(tabs[0])->pinned_url);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[0]));
  // The other pinned tab, on the tile's near half: before it.
  pinned->DropInEssentials({tabs[1]}, tabs[0]);
  EXPECT_THAT(Essentials(), testing::ElementsAre(tabs[1], tabs[0]));
  EXPECT_FALSE(IsPinnedInSpace(tabs[1]));
  EXPECT_TRUE(GetTabState(tabs[1])->pinned_url.is_valid());
}

}  // namespace koan
