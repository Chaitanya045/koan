// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_pinned_tabs_hooks.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_tab_drag.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace koan {

namespace {

constexpr char16_t kRocket[] = u"\U0001F680";

// A page that is not blank, so it is never taken for an empty tab.
constexpr char kPage[] = "data:text/html,<title>Page</title>";

// Zen's "Clear" (the sidebar's button above the tab list, Ctrl+Shift+K).
void Clear(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  ASSERT_TRUE(browser_view);
  KoanBrowserViewController::From(browser_view)
      ->shortcuts()
      ->CloseUnpinnedTabs();
}

// The Space's tabs that have a sidebar row: all but its empty tab.
std::vector<int> ListedTabs(TabStripModel* model,
                            const tab_groups::TabGroupId& space) {
  std::vector<int> listed = TabsOfSpace(model, space);
  std::erase_if(listed, [&](int index) {
    return IsEmptyTab(model->GetTabAtIndex(index));
  });
  return listed;
}

}  // namespace

using KoanSpacesBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, WindowAlwaysShowsOneSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(spaces->IsEnabled());
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));

  // Zen's name for a Space made by itself, with the window's tab in it.
  const auto infos = spaces->GetSpaces();
  EXPECT_EQ(u"Space", infos[0].name);
  EXPECT_TRUE(infos[0].active);
  EXPECT_THAT(TabsOfSpace(browser()->GetTabStripModel(), infos[0].id),
              testing::ElementsAre(0));
  // Zen keeps at least one Space.
  EXPECT_FALSE(spaces->CanDeleteSpace());
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, NewSpaceComesAfterTheActiveOne) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));

  spaces->CreateSpace(u"Work", kRocket, {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  spaces->CreateSpace(u"Play", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  EXPECT_THAT(SpaceNames(browser()),
              testing::ElementsAre(u"Space", u"Work", u"Play"));

  // The new Space is shown, on Zen's empty tab (its empty state, no row).
  EXPECT_EQ(u"Play", spaces->GetSpace(*spaces->GetActiveSpace())->name);
  EXPECT_TRUE(IsEmptyTab(browser()->GetTabStripModel()->GetActiveTab()));

  // After the active Space, not at the end.
  spaces->SwitchTo(spaces->GetSpaces()[0].id);
  spaces->CreateSpace(u"Mid", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 4));
  EXPECT_THAT(SpaceNames(browser()),
              testing::ElementsAre(u"Space", u"Mid", u"Work", u"Play"));
  EXPECT_TRUE(spaces->CanDeleteSpace());
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, SpacesKeepTheirTabsApart) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  spaces->CreateSpace(u"Work", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));

  // A Space is a run of tabs: the first Space's tabs, then the second's.
  const auto infos = spaces->GetSpaces();
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  EXPECT_THAT(TabsOfSpace(tab_strip, infos[0].id), testing::ElementsAre(0));
  EXPECT_THAT(TabsOfSpace(tab_strip, infos[1].id), testing::ElementsAre(1));
  EXPECT_EQ(infos[1].id, *spaces->GetActiveSpace());
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, SwitchingAndCycling) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  spaces->CreateSpace(u"Work", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  spaces->CreateSpace(u"Play", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));

  const auto active_name = [&] {
    return spaces->GetSpace(*spaces->GetActiveSpace())->name;
  };
  EXPECT_EQ(u"Play", active_name());
  // Wraps around, like Zen.
  spaces->Cycle(/*forward=*/true);
  EXPECT_EQ(u"Space", active_name());
  spaces->Cycle(/*forward=*/false);
  EXPECT_EQ(u"Play", active_name());

  EXPECT_TRUE(spaces->SwitchToNumber(2));
  EXPECT_EQ(u"Work", active_name());
  EXPECT_FALSE(spaces->SwitchToNumber(4));
  EXPECT_FALSE(spaces->SwitchToNumber(0));
  EXPECT_EQ(u"Work", active_name());
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, RenameAndIcon) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const auto id = spaces->GetSpaces()[0].id;

  spaces->RenameSpace(id, u"  Home  ");
  spaces->SetSpaceIcon(id, kRocket);
  auto info = spaces->GetSpace(id);
  ASSERT_TRUE(info);
  EXPECT_EQ(u"Home", info->name);
  EXPECT_EQ(kRocket, info->icon);

  // Zen ignores an empty name; an empty icon removes the icon.
  spaces->RenameSpace(id, u"   ");
  spaces->SetSpaceIcon(id, u"");
  info = spaces->GetSpace(id);
  EXPECT_EQ(u"Home", info->name);
  EXPECT_EQ(u"", info->icon);
}

// Zen's empty tab is the one Koan marks when it makes it, as Zen keys on
// `zen-empty-tab`, not any blank page: compact mode holds the sidebar out,
// and the sidebar hides the row, only for that one.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, OnlyTheMarkedTabIsTheEmptyTab) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  // The window opened on its empty tab (Zen swaps the startup page for it).
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return IsEmptyTab(model->GetTabAtIndex(0)); }));
  // The user's own about:blank page is an ordinary tab.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(url::kAboutBlankURL),
      WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_EQ(2, model->count());
  EXPECT_FALSE(IsEmptyTab(model->GetActiveTab()));
  // So is a new tab page opened later (Cmd+T without the floating bar).
  chrome::AddTabAt(browser(), GURL(), -1, /*foreground=*/true);
  EXPECT_FALSE(IsEmptyTab(model->GetActiveTab()));

  // Closing the last tab keeps the window, on the empty tab Koan makes.
  while (model->count() > 1) {
    model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);
  }
  model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 1 && IsEmptyTab(model->GetActiveTab());
  }));
}

// A new window opens on its empty tab too.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, NewWindowOpensOnTheEmptyTab) {
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(WaitForSpaceCount(other, 1));
  TabStripModel* model = other->GetTabStripModel();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return IsEmptyTab(model->GetActiveTab()); }));
  EXPECT_EQ(1, model->count());
}

// Many Spaces (30, from a bug that added one per launch, ran past "+" and
// the sidebar's edge): the bottom bar's Space buttons stay in the switcher
// between the Library button and "+", which clips them and scrolls, as Zen's
// does, and the current Space stays in view.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, ManySpacesStayInTheSwitcher) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  constexpr size_t kCount = 30;
  for (size_t i = 1; i < kCount; ++i) {
    spaces->CreateSpace(u"Space " + base::NumberToString16(i), u"", {});
  }
  ASSERT_TRUE(WaitForSpaceCount(browser(), kCount));
  KoanSpaceBarView* bar = spaces->space_bar();
  ASSERT_TRUE(bar);
  views::Widget* widget = bar->GetWidget();
  ASSERT_TRUE(widget);
  widget->LayoutRootViewIfNecessary();
  // The expanded sidebar's row (not the icons-only column).
  ASSERT_TRUE(bar->compact());
  EXPECT_TRUE(bar->StripClipsForTesting());

  const gfx::Rect strip = bar->GetStripBoundsForTesting();
  const gfx::Rect create = bar->GetCreateButtonBoundsForTesting();
  // The switcher is inside the bar and clear of "+", so nothing it clips
  // reaches either.
  EXPECT_TRUE(gfx::Rect(bar->size()).Contains(strip)) << strip.ToString();
  EXPECT_FALSE(strip.Intersects(create))
      << strip.ToString() << " " << create.ToString();
  EXPECT_LE(strip.right(), create.x());

  // Whether the current Space's button is wholly in the switcher (once a
  // smooth scroll is over).
  const auto active_shown = [&] {
    widget->LayoutRootViewIfNecessary();
    const std::optional<gfx::Rect> button =
        bar->GetSpaceButtonBoundsForTesting(*spaces->GetActiveSpace());
    return button && strip.Contains(*button);
  };

  // The buttons don't all fit: the switcher has to scroll.
  const std::vector<KoanWorkspaceController::SpaceInfo> infos =
      spaces->GetSpaces();
  ASSERT_EQ(kCount, infos.size());
  int left = strip.right();
  int right = strip.x();
  for (const KoanWorkspaceController::SpaceInfo& info : infos) {
    const std::optional<gfx::Rect> button =
        bar->GetSpaceButtonBoundsForTesting(info.id);
    ASSERT_TRUE(button);
    left = std::min(left, button->x());
    right = std::max(right, button->right());
  }
  EXPECT_GT(right - left, strip.width());

  // The new Space (the last) is shown, then the first, then one between.
  EXPECT_TRUE(base::test::RunUntil(active_shown));
  for (size_t index : {size_t{0}, kCount / 2, kCount - 1}) {
    spaces->SwitchTo(infos[index].id);
    EXPECT_TRUE(base::test::RunUntil(active_shown)) << index;
  }
}

// Koan's tab drag (koan_tab_drag.h) only shows another Space while it goes
// on: the tabs move there when they drop, so Esc (or any cancel) has nothing
// to undo but the switch. A cancelled drag used to leave them in the other
// Space (upstream's drag needed them in the Space it showed).
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       ACancelledDragGoesBackToItsSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url::kAboutBlankURL),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  const tab_groups::TabGroupId home = spaces->GetSpaces()[0].id;
  spaces->CreateSpace(u"Work", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work = spaces->GetSpaces()[1].id;
  spaces->SwitchTo(home);

  // Two of Home's tabs (rows, not its empty tab) selected, the second one
  // picked up.
  std::vector<int> home_tabs = TabsOfSpace(model, home);
  std::erase_if(home_tabs, [&](int index) {
    return IsEmptyTab(model->GetTabAtIndex(index));
  });
  ASSERT_GE(home_tabs.size(), 2u);
  tabs::TabInterface* first = model->GetTabAtIndex(home_tabs[0]);
  tabs::TabInterface* picked = model->GetTabAtIndex(home_tabs[1]);
  ui::ListSelectionModel selection;
  selection.AddIndexToSelection(static_cast<size_t>(home_tabs[0]));
  selection.AddIndexToSelection(static_cast<size_t>(home_tabs[1]));
  selection.set_active(static_cast<size_t>(home_tabs[1]));
  selection.set_anchor(static_cast<size_t>(home_tabs[1]));
  model->SetSelectionFromModel(std::move(selection));
  const int first_index = model->GetIndexOfTab(first);

  KoanTabDrag* drag = spaces->tab_drag();
  ASSERT_TRUE(drag->StartForTesting(picked));
  EXPECT_TRUE(drag->IsDragging());
  // Held over Work's Space button: Work shows, the tabs stay in Home.
  drag->SwitchSpaceForTesting(work);
  EXPECT_EQ(work, *spaces->GetActiveSpace());
  EXPECT_EQ(home, first->GetGroup());
  EXPECT_EQ(home, picked->GetGroup());

  // Esc: Home again, on the picked-up tab, both still selected, nothing
  // moved.
  drag->CancelForTesting();
  EXPECT_EQ(home, *spaces->GetActiveSpace());
  EXPECT_EQ(picked, model->GetActiveTab());
  EXPECT_TRUE(model->IsTabSelected(model->GetIndexOfTab(first)));
  EXPECT_TRUE(model->IsTabSelected(model->GetIndexOfTab(picked)));
  EXPECT_EQ(2u, model->selection_model().size());
  EXPECT_EQ(first_index, model->GetIndexOfTab(first));
  EXPECT_EQ(first_index + 1, model->GetIndexOfTab(picked));
  EXPECT_TRUE(base::test::RunUntil([&] { return !drag->IsDragging(); }));
}

// A drop in another Space: the tabs join the Space shown, keeping their
// selection, the picked-up one active (Zen's drop after a Space switch).
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, TabsDroppedInAnotherSpaceJoinIt) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url::kAboutBlankURL),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  const tab_groups::TabGroupId home = spaces->GetSpaces()[0].id;
  spaces->CreateSpace(u"Work", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work = spaces->GetSpaces()[1].id;
  spaces->SwitchTo(home);
  std::vector<int> home_tabs = TabsOfSpace(model, home);
  std::erase_if(home_tabs, [&](int index) {
    return IsEmptyTab(model->GetTabAtIndex(index));
  });
  ASSERT_GE(home_tabs.size(), 2u);
  tabs::TabInterface* first = model->GetTabAtIndex(home_tabs[0]);
  tabs::TabInterface* picked = model->GetTabAtIndex(home_tabs[1]);

  // What the drop does (the pointer's part needs the Mac): Work shows, the
  // tabs move to it, then to their place in it.
  spaces->SwitchTo(work);
  spaces->MoveDraggedTabsToSpace({first, picked}, work);
  GetPinnedTabs(browser())->DropTabsInSpace({first, picked}, nullptr,
                                            /*pinned=*/false);
  EXPECT_EQ(work, *spaces->GetActiveSpace());
  EXPECT_EQ(work, first->GetGroup());
  EXPECT_EQ(work, picked->GetGroup());
  EXPECT_EQ(picked, model->GetActiveTab());
  EXPECT_TRUE(model->IsTabSelected(model->GetIndexOfTab(first)));
  EXPECT_EQ(model->GetIndexOfTab(first) + 1, model->GetIndexOfTab(picked));
  EXPECT_EQ(TabsOfSpace(model, work).back(), model->GetIndexOfTab(picked));
}

// Zen's Clear empties the Space's list: what is left is its hidden empty
// tab, selected, and nothing to close. It used to add a plain new tab page
// instead, listed as "New tab", whose close button added another one.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, ClearLeavesOnlyTheEmptyTab) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return IsEmptyTab(model->GetTabAtIndex(0)); }));
  const tab_groups::TabGroupId space = *spaces->GetActiveSpace();
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  ASSERT_EQ(2u, ListedTabs(model, space).size());

  // The selected tab stays (Zen keeps it), the other goes; the empty tab
  // isn't one of the Space's tabs to Clear.
  Clear(browser());
  EXPECT_EQ(1u, ListedTabs(model, space).size());
  EXPECT_FALSE(IsEmptyTab(model->GetActiveTab()));
  EXPECT_EQ(2u, TabsOfSpace(model, space).size());

  // Only the kept tab left: it goes too, onto the Space's own empty tab,
  // not a second one.
  Clear(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 1 && IsEmptyTab(model->GetActiveTab());
  }));
  EXPECT_EQ(space, *spaces->GetActiveSpace());
  EXPECT_THAT(TabsOfSpace(model, space), testing::ElementsAre(0));
  EXPECT_TRUE(ListedTabs(model, space).empty());
  EXPECT_TRUE(KoanShortcutController::UnpinnedTabsForClear(model).empty());

  // Nothing more to clear.
  Clear(browser());
  EXPECT_EQ(1, model->count());
  EXPECT_TRUE(IsEmptyTab(model->GetActiveTab()));
}

// A Space without an empty tab (it navigated away): Clear gives it one, in
// the same Space, and the window stays.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       ClearAddsTheEmptyTabWhenNoneLeft) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !IsEmptyTab(model->GetActiveTab()); }));
  const tab_groups::TabGroupId space = *spaces->GetActiveSpace();
  ASSERT_EQ(1u, ListedTabs(model, space).size());

  Clear(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 1 && IsEmptyTab(model->GetActiveTab());
  }));
  EXPECT_EQ(space, model->GetActiveTab()->GetGroup());
  EXPECT_TRUE(ListedTabs(model, space).empty());
  EXPECT_EQ(1u, spaces->GetSpaces().size());
}

// Closing a Space's only tab while it is a blank page (the plain new tab page
// older Clears left): it becomes the empty tab where it is, rather than
// closing and having a tab added back.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       ClosingTheOnlyBlankTabKeepsItAsTheEmptyTab) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  const tab_groups::TabGroupId space = *spaces->GetActiveSpace();
  chrome::AddTabAt(browser(), GURL(), -1, /*foreground=*/true, space);
  tabs::TabInterface* blank = model->GetActiveTab();
  model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(1, model->count());
  ASSERT_FALSE(IsEmptyTab(blank));
  ASSERT_EQ(space, blank->GetGroup());

  // Its row's close button.
  EXPECT_TRUE(HandleTabCloseRequest(blank));
  EXPECT_EQ(1, model->count());
  EXPECT_EQ(blank, model->GetActiveTab());
  EXPECT_TRUE(IsEmptyTab(blank));
  EXPECT_TRUE(ListedTabs(model, space).empty());
  EXPECT_EQ(1u, spaces->GetSpaces().size());

  // A page is no blank tab: it closes as usual.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  EXPECT_FALSE(HandleTabCloseRequest(model->GetActiveTab()));
}

}  // namespace koan
