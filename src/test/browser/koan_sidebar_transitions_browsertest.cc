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
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/views_test_utils.h"
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
    // Upstream behaviour Koan relies on (koan_essentials.h): the row and the
    // tile animate with the strip's shared animation, which is running now,
    // and the grid (a scroll view sized by the strip's layout) is given an
    // eased height, still 0 at the start, instead of its final row at once.
    views::test::RunScheduledLayout(browser_view());
    const auto* manager = static_cast<TabCollectionAnimatingLayoutManager*>(
        grid->GetLayoutManager());
    EXPECT_LT(manager->animation_coordinator().current_offset(), 1.0);
    EXPECT_LT(strip->pinned_tabs_scroll_view()->height(),
              manager->GetTargetPreferredSize().height());
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

// The recording (compact mode, 30.0 s): the revealed sidebar showed the
// Essentials as full-width rows with titles, because upstream lays the grid
// out one per row while the strip is collapsed. Zen's revealed compact
// sidebar keeps its grid of tiles.
IN_PROC_BROWSER_TEST_F(KoanSidebarTransitionsBrowserTest,
                       CompactModeRevealKeepsTheEssentialsGrid) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(pinned);
  const std::vector<tabs::TabInterface*> tabs = OpenTabs(4);
  for (int i = 0; i < 3; ++i) {
    pinned->AddToEssentials(tabs[i]);
  }
  TabStripView* strip = FindView<TabStripView>(browser_view());
  ASSERT_TRUE(strip);
  PinnedTabContainerView* grid = strip->GetPinnedTabsContainer();
  ASSERT_TRUE(grid);

  KoanCompactModeController* compact =
      KoanBrowserViewController::From(browser_view())->compact_mode();
  ASSERT_TRUE(compact);
  compact->ToggleCompactMode();
  const auto* state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));
  ASSERT_TRUE(compact->PeekSidebar());
  auto* region = views::AsViewClass<VerticalTabStripRegionView>(
      browser_view()->tab_strip_view());
  ASSERT_TRUE(region);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return region->is_expanded_on_hover(); }));
  ASSERT_TRUE(state->IsCollapsed());

  // One row of three tiles, each well under half the grid's width, no titles.
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(browser_view());
    return !TabCollectionAnimatingLayoutManager::IsViewAnimating(grid) &&
           grid->width() > 0;
  }));
  std::vector<TabView*> tiles;
  for (int i = 0; i < 3; ++i) {
    std::vector<TabView*> views;
    CollectTabViews(grid, tabs[i], views);
    ASSERT_EQ(1u, views.size());
    tiles.push_back(views[0]);
  }
  for (TabView* tile : tiles) {
    EXPECT_EQ(tiles[0]->y(), tile->y());
    EXPECT_GT(tile->width(), 0);
    EXPECT_LT(tile->width() * 2, grid->width());
    TabTitle* title = FindView<TabTitle>(tile);
    EXPECT_TRUE(!title || !title->GetVisible());
  }
}

// The recording: compact mode off with the sidebar hidden showed the sidebar
// in a frame while the page's old picture showed through beside it (upstream's
// expand moves the card's edge over the page), and off with the sidebar out
// slid it out and back. Zen docks it at once: a hidden one slides in over no
// page, one that is out stays.
IN_PROC_BROWSER_TEST_F(KoanSidebarTransitionsBrowserTest,
                       CompactModeOffDocksTheSidebarAtOnce) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanCompactModeController* compact =
      KoanBrowserViewController::From(browser_view())->compact_mode();
  ASSERT_TRUE(compact);
  const auto* state = tabs::VerticalTabStripStateController::From(browser());
  auto* region = browser_view()->tab_strip_view();
  ASSERT_TRUE(region);
  BrowserAnimationController* animations =
      BrowserAnimationController::From(browser());
  ASSERT_TRUE(animations);

  for (const bool revealed : {false, true}) {
    SCOPED_TRACE(revealed ? "revealed" : "hidden");
    compact->ToggleCompactMode();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return state->IsCollapsed() &&
             !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
    }));
    if (revealed) {
      ASSERT_TRUE(compact->PeekSidebar());
      ASSERT_TRUE(base::test::RunUntil([&] {
        return !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
      }));
    }
    compact->ToggleCompactMode();
    // Upstream's expand is over at once: the strip is docked and the page laid
    // out beside it in this turn.
    ASSERT_TRUE(base::test::RunUntil([&] { return !compact->hides_sidebar(); }));
    EXPECT_FALSE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
    EXPECT_FALSE(state->IsCollapsed());
    views::test::RunScheduledLayout(browser_view());
    EXPECT_EQ(state->GetUncollapsedWidth(), region->width());
    if (revealed && region->layer()) {
      // Out already: no slide.
      EXPECT_TRUE(region->layer()->GetTargetTransform().IsIdentity());
    }
    // A hidden one slides in from the edge, and ends in place.
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !region->layer() || region->layer()->transform().IsIdentity();
    }));
  }
}

}  // namespace koan
