// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// What the sidebar shows after two of Zen's transitions, found wrong by the
// Mac screen recording of 2026-11-11 (scripts/mac/video): compact mode's
// revealed sidebar has Zen's separator with Clear, and a tab that becomes an
// Essential leaves its Space's list for the grid in one animation that the
// Space follows. The frames in between are the video flow's
// (scripts/mac/video/flows/essentials.mjs).

#include <string_view>
#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace koan {

namespace {

// The first view under `root` (itself included) of class `name`, by its
// metadata name: Koan's sidebar rows are private to koan_sidebar_tabs.cc.
views::View* FindByClassName(views::View* root, std::string_view name) {
  if (root->GetClassName() == name) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindByClassName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

template <typename T>
T* FindView(views::View* root) {
  if (T* view = views::AsViewClass<T>(root)) {
    return view;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindView<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

// The tab views of `tab` under `root`: the Essential's tile, and its old row
// while that is still animating out.
void CollectTabViews(views::View* root,
                     const tabs::TabInterface* tab,
                     std::vector<TabView*>& views) {
  if (auto* tab_view = views::AsViewClass<TabView>(root);
      tab_view && tab_view->GetTabInterface() == tab) {
    views.push_back(tab_view);
  }
  for (views::View* child : root->children()) {
    CollectTabViews(child, tab, views);
  }
}

class KoanSidebarTransitionsBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

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

  // Zen's separator with Clear: on screen, with a height.
  bool ClearRowShows() {
    views::View* row = FindByClassName(browser_view(), "KoanClearTabsRow");
    views::View* button =
        row ? FindByClassName(row, "KoanClearButton") : nullptr;
    return row && row->IsDrawn() && row->height() > 0 && button &&
           button->IsDrawn() && button->width() > 0;
  }
};

}  // namespace

// The recording (round 6, compact on): the revealed sidebar had no separator
// and no Clear between the Space and "+ New Tab" (KoanClearTabsRow hidden,
// because compact mode keeps upstream's strip "collapsed"). Zen's has both.
IN_PROC_BROWSER_TEST_F(KoanSidebarTransitionsBrowserTest,
                       CompactModeRevealShowsTheClearRow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  // A listed tab: Zen shows the separator only then.
  OpenTabs(1);
  ASSERT_TRUE(base::test::RunUntil([&] { return ClearRowShows(); }));

  KoanCompactModeController* compact =
      KoanBrowserViewController::From(browser_view())->compact_mode();
  ASSERT_TRUE(compact);
  compact->ToggleCompactMode();
  const auto* state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(state);
  ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));
  ASSERT_TRUE(compact->hides_sidebar());

  // Zen's "show the sidebar" shortcut: it comes out and stays out.
  ASSERT_TRUE(compact->PeekSidebar());
  auto* region = views::AsViewClass<VerticalTabStripRegionView>(
      browser_view()->tab_strip_view());
  ASSERT_TRUE(region);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return region->is_expanded_on_hover(); }));
  EXPECT_TRUE(state->IsCollapsed());
  EXPECT_TRUE(base::test::RunUntil([&] { return ClearRowShows(); }));

  // And back with compact mode off.
  compact->PeekSidebar();
  compact->ToggleCompactMode();
  ASSERT_TRUE(base::test::RunUntil([&] { return !state->IsCollapsed(); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return ClearRowShows(); }));
}

// The recording (calibrate, 49.3 s): the new Essential's tile appeared at
// once, the Space below jumped down a row in the same frame, and its old row
// faded out in the list. Now the grid grows with the tile's animation (upstream
// animates the row out and the tile in together) and ends with one view of
// the tab, a tile in the grid, the Space header below it.
IN_PROC_BROWSER_TEST_F(KoanSidebarTransitionsBrowserTest,
                       AnEssentialLeavesTheSpaceInOneTransition) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(3);
  TabStripView* strip = FindView<TabStripView>(browser_view());
  ASSERT_TRUE(strip);
  PinnedTabContainerView* grid = strip->GetPinnedTabsContainer();
  ASSERT_TRUE(grid);
  ASSERT_EQ(0, pinned->EssentialsCount());

  pinned->AddToEssentials(tabs[1]);
  ASSERT_TRUE(tabs[1]->IsPinned());
  if (gfx::Animation::ShouldRenderRichAnimation()) {
    // Upstream behaviour Koan relies on (koan_essentials.h): the first tile
    // animates in (no snap for the grid's width change), and the grid's
    // height follows it from 0 instead of being its final height at once.
    EXPECT_TRUE(TabCollectionAnimatingLayoutManager::IsViewAnimating(grid));
    EXPECT_LT(grid->GetPreferredSize().height(),
              static_cast<TabCollectionAnimatingLayoutManager*>(
                  grid->GetLayoutManager())
                  ->GetTargetPreferredSize()
                  .height());
  }

  std::vector<TabView*> views;
  ASSERT_TRUE(base::test::RunUntil([&] {
    views.clear();
    CollectTabViews(strip, tabs[1], views);
    return !TabCollectionAnimatingLayoutManager::IsViewAnimating(grid) &&
           views.size() == 1u;
  }));
  // One view of the tab: a tile in the grid, none left in the Space's list.
  EXPECT_EQ(grid, views[0]->parent());
  EXPECT_TRUE(views[0]->IsDrawn());
  // The grid at its full height, the Space header right below it.
  EXPECT_GT(grid->height(), 0);
  EXPECT_EQ(grid->GetPreferredSize().height(), grid->height());
  EXPECT_GE(strip->pinned_tabs_scroll_view()->height(), grid->height());
  views::View* header = FindByClassName(strip, "KoanSpaceHeaderView");
  ASSERT_TRUE(header);
  EXPECT_GE(header->GetBoundsInScreen().y(),
            grid->GetBoundsInScreen().bottom());
  // The other tabs are still the Space's rows.
  for (tabs::TabInterface* tab : {tabs[0], tabs[2]}) {
    views.clear();
    CollectTabViews(strip, tab, views);
    ASSERT_EQ(1u, views.size());
    EXPECT_NE(grid, views[0]->parent());
  }
}

}  // namespace koan
