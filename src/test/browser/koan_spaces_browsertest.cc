// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <vector>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"

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

}  // namespace koan
