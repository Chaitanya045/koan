// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace koan {

namespace {

constexpr char16_t kRocket[] = u"\U0001F680";

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

  // The new Space is shown.
  EXPECT_EQ(u"Play", spaces->GetSpace(*spaces->GetActiveSpace())->name);

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

// Zen's tab drag across Spaces (koan_space_drag.h) moves the dragged tabs
// into the Space it switches to while upstream's drag goes on. Upstream's
// drag takes the selection to be its tabs: entering a container,
// TabDragHandler::HandleDraggedTabsIntoNode() counts every dragged tab
// before the index it moves to, then calls MoveSelectedTabsTo(). With only
// the shown tab selected after the switch, that index fell inside the new
// Space while the move was to no group, and
// TabCollection::FindMovePositionRecursive() CHECKed (a crash on the Mac,
// dragging two tabs to another Space). Guards that upstream behaviour too.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       DragToAnotherSpaceKeepsTheDraggedTabsSelected) {
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
  spaces->CreateSpace(u"Play", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  // Work is the middle Space, so tabs come after it.
  const tab_groups::TabGroupId work = spaces->GetSpaces()[1].id;
  spaces->SwitchTo(home);

  // Two of Home's tabs (rows, not its empty tab) are dragged, the second one
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
  const std::vector<tabs::TabInterface*> dragged = {first, picked};

  // Held over Work's Space button.
  spaces->MoveDraggedTabsToSpace(dragged, work);
  EXPECT_EQ(work, *spaces->GetActiveSpace());
  EXPECT_EQ(picked, model->GetActiveTab());
  EXPECT_EQ(work, first->GetGroup());
  EXPECT_EQ(work, picked->GetGroup());
  EXPECT_TRUE(model->IsTabSelected(model->GetIndexOfTab(first)));
  EXPECT_TRUE(model->IsTabSelected(model->GetIndexOfTab(picked)));
  EXPECT_EQ(2u, model->selection_model().size());

  // What upstream's drag then does on entering the tab strip's container
  // (not a group's) with the picked-up tab in a group: the dragged tabs go
  // after that group.
  const auto dragged_before = [&](int index) {
    return static_cast<int>(std::ranges::count_if(
        dragged, [&](auto* tab) { return model->GetIndexOfTab(tab) < index; }));
  };
  const int work_end = TabsOfSpace(model, work).back() + 1;
  model->MoveSelectedTabsTo(
      std::clamp(work_end - dragged_before(work_end), 0, model->count() - 1),
      std::nullopt);
  EXPECT_EQ(model->GetIndexOfTab(first) + 1, model->GetIndexOfTab(picked));
  EXPECT_EQ(TabsOfSpace(model, work).back() + 1, model->GetIndexOfTab(first));

  // And on entering Work's own container again, from after it: they go in
  // at its end.
  const int work_end_now = TabsOfSpace(model, work).back() + 1;
  model->MoveSelectedTabsTo(work_end_now - dragged_before(work_end_now), work);
  EXPECT_EQ(work, first->GetGroup());
  EXPECT_EQ(work, picked->GetGroup());
  EXPECT_EQ(model->GetIndexOfTab(first) + 1, model->GetIndexOfTab(picked));
}

}  // namespace koan
