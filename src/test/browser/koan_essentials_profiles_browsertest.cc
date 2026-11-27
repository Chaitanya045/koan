// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's container-specific Essentials (zen.workspaces.separate-essentials,
// #479, koan_essentials_profiles.h): the grid shows the active Space's
// profile's Essentials only, Spaces with the same profile share it, Zen's 12
// are counted per profile, another profile's tab can't join the grid, the
// setting off shows every Essential, ⌘1-⌘9 count only the tiles shown, an
// Essential of another profile that becomes active shows its Space, and each
// Essential keeps its profile after a restart.

#include <optional>
#include <string>
#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_essentials_profiles.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr char kPage[] = "data:text/html,<title>Page</title>";

class KoanEssentialsProfilesBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }
  TabStripModel* tab_strip() { return browser()->GetTabStripModel(); }

  // A new profile (New Profile...), Serialize()d.
  std::string AddProfile(const std::u16string& name) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), name);
    return container ? containers::Serialize(container->id) : std::string();
  }

  // Opens a page in a new foreground tab of the active Space (Cmd+T and the
  // URL bar): in the Space's profile.
  tabs::TabInterface* OpenTab() {
    NavigateParams params(browser(), GURL(kPage), ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    ui_test_utils::NavigateToURL(&params);
    return params.navigated_or_inserted_contents
               ? tabs::TabInterface::GetFromContents(
                     params.navigated_or_inserted_contents)
               : nullptr;
  }

  // Waits for the next update of the Spaces (the grid follows the Space and
  // the setting there).
  [[nodiscard]] bool WaitForGrid(
      const std::optional<containers::ContainerId>& shown,
      bool separate = true) {
    return base::test::RunUntil([&] {
      const containers::EssentialsProfiles& profiles =
          GetSpaces(browser())->essentials_profiles();
      return profiles.separate == separate &&
             (!separate || profiles.shown == shown);
    });
  }

  // The tile of `tab` in the grid.
  views::View* TileOf(tabs::TabInterface* tab) {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->tab_strip_view()
        ->GetTabAnchorView(tab->GetHandle());
  }

  // Waits until the grid shows `tab`'s tile (`shown`) or hides it.
  [[nodiscard]] bool WaitForTile(tabs::TabInterface* tab, bool shown) {
    return base::test::RunUntil([&] {
      views::View* tile = TileOf(tab);
      return tile && tile->GetVisible() == shown;
    });
  }
};

}  // namespace

// The grid shows the active Space's profile's Essentials; Spaces with the
// same profile share them, and a Space without one shows those without one.
IN_PROC_BROWSER_TEST_F(KoanEssentialsProfilesBrowserTest,
                       TheGridFollowsTheSpacesProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(spaces && pinned);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const tab_groups::TabGroupId first = spaces->GetSpaces()[0].id;
  const std::string work = AddProfile(u"Work");
  const std::optional<containers::ContainerId> work_id =
      containers::SpaceContainerFromString(work);
  ASSERT_TRUE(work_id);

  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work_space = *spaces->GetActiveSpace();
  tabs::TabInterface* mail = OpenTab();
  ASSERT_TRUE(mail);
  ASSERT_EQ(work_id, containers::ContainerOf(mail->GetContents()));
  pinned->AddToEssentials(mail);
  ASSERT_TRUE(mail->IsPinned());

  spaces->SwitchTo(first);
  ASSERT_TRUE(WaitForGrid(std::nullopt));
  tabs::TabInterface* news = OpenTab();
  ASSERT_TRUE(news);
  pinned->AddToEssentials(news);
  ASSERT_TRUE(news->IsPinned());

  // The first Space (no profile): its own Essential only.
  EXPECT_TRUE(IsEssentialShown(news));
  EXPECT_FALSE(IsEssentialShown(mail));
  EXPECT_EQ(1, pinned->EssentialsCount());
  EXPECT_TRUE(WaitForTile(news, true));
  EXPECT_TRUE(WaitForTile(mail, false));

  // Work: Work's.
  spaces->SwitchTo(work_space);
  ASSERT_TRUE(WaitForGrid(work_id));
  EXPECT_TRUE(IsEssentialShown(mail));
  EXPECT_FALSE(IsEssentialShown(news));
  EXPECT_EQ(1, pinned->EssentialsCount());
  EXPECT_TRUE(WaitForTile(mail, true));
  EXPECT_TRUE(WaitForTile(news, false));

  // A second Work Space shares Work's grid.
  spaces->CreateSpace(u"Work 2", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  ASSERT_TRUE(WaitForGrid(work_id));
  EXPECT_TRUE(IsEssentialShown(mail));
  EXPECT_FALSE(IsEssentialShown(news));
}

// Zen's 12 are counted per profile: a full default grid leaves room in
// Work's. Another profile's tab doesn't join the grid shown (Zen's
// canEssentialBeAdded()), from the menu or a drop.
IN_PROC_BROWSER_TEST_F(KoanEssentialsProfilesBrowserTest,
                       TheLimitIsPerProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(spaces && pinned);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const tab_groups::TabGroupId first = spaces->GetSpaces()[0].id;
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work_space = *spaces->GetActiveSpace();
  tabs::TabInterface* work_tab = OpenTab();
  ASSERT_TRUE(work_tab);

  spaces->SwitchTo(first);
  ASSERT_TRUE(WaitForGrid(std::nullopt));
  for (int i = 0; i < KoanPinnedTabController::kMaxEssentials; ++i) {
    tabs::TabInterface* tab = OpenTab();
    ASSERT_TRUE(tab);
    pinned->AddToEssentials(tab);
    ASSERT_TRUE(tab->IsPinned());
  }
  EXPECT_EQ(KoanPinnedTabController::kMaxEssentials, pinned->EssentialsCount());
  tabs::TabInterface* extra = OpenTab();
  ASSERT_TRUE(extra);
  EXPECT_FALSE(pinned->CanAddToEssentials(extra));
  pinned->DropInEssentials({extra}, nullptr);
  EXPECT_FALSE(extra->IsPinned());

  // A Work tab moved into the first Space keeps its profile: not in this
  // grid, even with room.
  spaces->MoveTabsToSpace({work_tab}, first);
  ASSERT_EQ(first, work_tab->GetGroup());
  EXPECT_FALSE(pinned->CanAddToEssentials(work_tab));

  // Work's grid is empty: its tab fits there.
  spaces->MoveTabsToSpace({work_tab}, work_space);
  spaces->SwitchTo(work_space);
  ASSERT_TRUE(WaitForGrid(containers::SpaceContainerFromString(work)));
  EXPECT_EQ(0, pinned->EssentialsCount());
  EXPECT_TRUE(pinned->CanAddToEssentials(work_tab));
  pinned->AddToEssentials(work_tab);
  EXPECT_TRUE(work_tab->IsPinned());
  EXPECT_EQ(1, pinned->EssentialsCount());
  EXPECT_EQ(KoanPinnedTabController::kMaxEssentials,
            pinned->EssentialsCountIn(std::nullopt));
  EXPECT_EQ(KoanPinnedTabController::kMaxEssentials + 1,
            tab_strip()->IndexOfFirstNonPinnedTab());
}

// The setting off: every Space shows every Essential, and the 12 are counted
// together, as before.
IN_PROC_BROWSER_TEST_F(KoanEssentialsProfilesBrowserTest,
                       TheSettingOffShowsEveryEssential) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(spaces && pinned);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  EXPECT_TRUE(
      profile()->GetPrefs()->GetBoolean(prefs::kWorkspacesSeparateEssentials));
  const tab_groups::TabGroupId first = spaces->GetSpaces()[0].id;
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  tabs::TabInterface* mail = OpenTab();
  ASSERT_TRUE(mail);
  pinned->AddToEssentials(mail);
  tabs::TabInterface* work_tab = OpenTab();
  ASSERT_TRUE(work_tab);
  spaces->SwitchTo(first);
  ASSERT_TRUE(WaitForGrid(std::nullopt));
  EXPECT_FALSE(IsEssentialShown(mail));

  profile()->GetPrefs()->SetBoolean(prefs::kWorkspacesSeparateEssentials,
                                    false);
  ASSERT_TRUE(WaitForGrid(std::nullopt, /*separate=*/false));
  EXPECT_TRUE(IsEssentialShown(mail));
  EXPECT_EQ(1, pinned->EssentialsCount());
  EXPECT_TRUE(WaitForTile(mail, true));
  spaces->MoveTabsToSpace({work_tab}, first);
  EXPECT_TRUE(pinned->CanAddToEssentials(work_tab));

  // And back on: Work's Essential leaves this Space's grid again.
  profile()->GetPrefs()->SetBoolean(prefs::kWorkspacesSeparateEssentials, true);
  ASSERT_TRUE(WaitForGrid(std::nullopt));
  EXPECT_FALSE(IsEssentialShown(mail));
  EXPECT_TRUE(WaitForTile(mail, false));
}

// ⌘1-⌘9 count only the tiles shown; an Essential of another profile made
// active (Switch to Tab, an extension) shows its Space, on it (Zen's
// switchTabIfNeeded()).
IN_PROC_BROWSER_TEST_F(KoanEssentialsProfilesBrowserTest,
                       HiddenEssentialsAreSkippedAndShowTheirSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(spaces && pinned);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const tab_groups::TabGroupId first = spaces->GetSpaces()[0].id;
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work_space = *spaces->GetActiveSpace();
  tabs::TabInterface* mail = OpenTab();
  ASSERT_TRUE(mail);
  pinned->AddToEssentials(mail);
  spaces->SwitchTo(first);
  ASSERT_TRUE(WaitForGrid(std::nullopt));
  tabs::TabInterface* news = OpenTab();
  ASSERT_TRUE(news);
  pinned->AddToEssentials(news);
  ASSERT_EQ(mail, tab_strip()->GetTabAtIndex(0));
  ASSERT_EQ(news, tab_strip()->GetTabAtIndex(1));

  // ⌘1: the first tile shown, not Work's hidden one before it.
  chrome::SelectNumberedTab(browser(), 0);
  EXPECT_EQ(news, tab_strip()->GetActiveTab());
  EXPECT_EQ(first, spaces->GetActiveSpace());

  // Work's Essential made active: Work shows, on it.
  tab_strip()->ActivateTabAt(tab_strip()->GetIndexOfTab(mail));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return spaces->GetActiveSpace() == work_space; }));
  EXPECT_EQ(mail, tab_strip()->GetActiveTab());
  EXPECT_TRUE(IsEssentialShown(mail));
}

// After a restart each Essential is back in its profile, and so in its grid.
IN_PROC_BROWSER_TEST_F(KoanEssentialsProfilesBrowserTest,
                       PRE_EssentialsKeepTheirProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  KoanPinnedTabController* pinned = GetPinnedTabs(browser());
  ASSERT_TRUE(spaces && pinned);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  tabs::TabInterface* mail = OpenTab();
  ASSERT_TRUE(mail);
  pinned->AddToEssentials(mail);
  ASSERT_TRUE(mail->IsPinned());
  for (int i = 0; i < tab_strip()->count(); ++i) {
    EXPECT_TRUE(content::WaitForLoadStop(tab_strip()->GetWebContentsAt(i)));
  }
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanEssentialsProfilesBrowserTest,
                       EssentialsKeepTheirProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const std::vector<containers::UserContainer> listed =
      containers::ParseUserContainers(
          profile()->GetPrefs()->GetList(prefs::kUserContainers));
  ASSERT_EQ(1u, listed.size());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tab_strip()->IndexOfFirstNonPinnedTab() == 1; }));
  tabs::TabInterface* mail = tab_strip()->GetTabAtIndex(0);
  EXPECT_EQ(listed[0].id, containers::ContainerOf(mail->GetContents()));

  spaces->SwitchTo(spaces->GetSpaces()[0].id);
  ASSERT_TRUE(WaitForGrid(std::nullopt));
  EXPECT_FALSE(IsEssentialShown(mail));
  spaces->SwitchTo(spaces->GetSpaces()[1].id);
  ASSERT_TRUE(WaitForGrid(listed[0].id));
  EXPECT_TRUE(IsEssentialShown(mail));
}

}  // namespace koan
