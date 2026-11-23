// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_agent_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_pinned_tabs_hooks.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_tab_drag.h"
#include "koan/browser/ui/views/koan_window_sync.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/base/page_transition_types.h"
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

std::optional<containers::AgentContainer> StartAgentTask(
    Profile* profile,
    containers::AgentMode mode) {
  base::test::TestFuture<containers::AgentContainerResult> started;
  containers::StartAgentContainer(
      profile, {.mode = mode, .space_name = u"Agent cleanup"},
      started.GetCallback());
  const containers::AgentContainerResult& result = started.Get();
  return result.has_value() ? std::make_optional(*result) : std::nullopt;
}

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

// Opens kPage in a new window as CDP's Target.createTarget with newWindow
// does (TargetHandler::CreateTarget(): a kNewWindow navigation of `profile`,
// no source window), as a link's new window; that window and its page.
void OpenLinkWindow(Profile* profile,
                    BrowserWindowInterface** window,
                    content::WebContents** page) {
  NavigateParams params(profile, GURL(kPage),
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::kNewWindow;
  Navigate(&params);
  ASSERT_TRUE(params.browser);
  ASSERT_TRUE(params.navigated_or_inserted_contents);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));
  *window = params.browser;
  *page = params.navigated_or_inserted_contents;
}

// The window's only tab made a blank page the user opened: about:blank
// opened from `opener` (window.open), which then closes. That tab.
void OpenBlankAndCloseOpener(BrowserWindowInterface* window,
                             content::WebContents* opener,
                             tabs::TabInterface** blank) {
  TabStripModel* model = window->GetTabStripModel();
  ASSERT_EQ(opener, model->GetActiveWebContents());
  ASSERT_TRUE(content::ExecJs(opener, "window.open('about:blank', '_blank')"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 2 && model->GetActiveWebContents() != opener;
  }));
  *blank = model->GetActiveTab();
  model->CloseWebContentsAt(model->GetIndexOfWebContents(opener),
                            TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(1, model->count());
  ASSERT_EQ(*blank, model->GetActiveTab());
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

// Cmd+W (IDC_CLOSE_TAB, what the Mac's File > Close Tab runs) on the only
// listed tab, opened from the empty state: back to the empty state, and the
// window stays (Zen: browser.tabs.closeWindowWithLastTab is false; only
// Cmd+W on the empty tab itself closes the window,
// zen.tabs.close-window-with-empty). empty_tab.mjs saw the Mac's window
// close here.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       CloseTabOnTheLastListedTabKeepsTheWindow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  // The empty state, as after closing every tab.
  while (model->count() > 1) {
    model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);
  }
  model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 1 && IsEmptyTab(model->GetActiveTab());
  }));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_EQ(2, model->count());
  ASSERT_FALSE(IsEmptyTab(model->GetActiveTab()));

  // The empty state left the window idle: Chromium's UnloadController took
  // the empty strip for the window's close, and the next closed tab finished
  // that close (KeepWindowOnLastTabClosed() in koan_workspaces.cc).
  const UnloadController* unload = UnloadController::From(browser());
  ASSERT_TRUE(unload);
  EXPECT_FALSE(unload->is_attempting_to_close_browser());

  // The model goes with the window: read it only while the window is there,
  // so a closed window fails the test instead of crashing it.
  base::WeakPtr<BrowserWindowInterface> window = browser()->GetWeakPtr();
  chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window || window->IsDeleteScheduled() ||
           window->GetTabStripModel()->count() == 1;
  }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(window) << "Cmd+W closed the window";
  ASSERT_FALSE(window->IsDeleteScheduled()) << "Cmd+W closed the window";
  ASSERT_EQ(1, window->GetTabStripModel()->count());
  EXPECT_TRUE(IsEmptyTab(window->GetTabStripModel()->GetActiveTab()));
  EXPECT_FALSE(
      UnloadController::From(window.get())->is_attempting_to_close_browser());
}

// The New Tab shortcut (⌘T) with Zen's replace-newtab off: a new tab. On
// macOS the shortcut has the key, not File > New Tab (koan_main_menu_mac.mm
// takes Koan's keys off Chrome's items), so it has to open the tab itself
// (tab_management.mjs and spaces.mjs found no tab after a real ⌘T).
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       NewTabShortcutOpensATabWithoutReplaceNewTab) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kUrlBarReplaceNewTab,
                                                  false);
  TabStripModel* model = browser()->GetTabStripModel();
  const int before = model->count();
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_TRUE(KoanBrowserViewController::From(browser_view)
                  ->shortcuts()
                  ->Run(shortcuts::Action::kNewTab));
  EXPECT_EQ(before + 1, model->count());
}

// A new window opens on its empty tab too.
// The Mac run (cycle 17, sidebar_tabs): Cmd+W on a second window's only tab,
// a blank page opened by a page there (window.open) that then closed, closed
// the window; Zen keeps it, on the Space's empty tab, and Cmd+W there closes
// it. Chrome's Close Tab command, as the menu's Cmd+W, in a window that is
// not the first.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       CloseTabOnASecondWindowsOnlyBlankTabKeepsTheWindow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(WaitForSpaceCount(other, 1));
  TabStripModel* model = other->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(other, GURL(kPage)));
  content::WebContents* opener = model->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(opener, "window.open('about:blank', '_blank')"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 2 && model->GetActiveWebContents() != opener;
  }));
  tabs::TabInterface* blank = model->GetActiveTab();
  model->CloseWebContentsAt(model->GetIndexOfWebContents(opener),
                            TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(1, model->count());
  ASSERT_EQ(blank, model->GetActiveTab());
  EXPECT_FALSE(IsEmptyTab(blank));
  EXPECT_TRUE(blank->GetGroup());

  base::WeakPtr<BrowserWindowInterface> window = other->GetWeakPtr();
  chrome::ExecuteCommand(other, IDC_CLOSE_TAB);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(window) << "Cmd+W closed the window";
  ASSERT_FALSE(window->IsDeleteScheduled()) << "Cmd+W closed the window";
  ASSERT_EQ(1, window->GetTabStripModel()->count());
  EXPECT_TRUE(IsEmptyTab(window->GetTabStripModel()->GetActiveTab()));

  // Cmd+W on the empty tab closes the window.
  chrome::ExecuteCommand(other, IDC_CLOSE_TAB);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !window || window->IsDeleteScheduled(); }));
}

// The e2e sidebar_tabs flow: a window opened as CDP's Target.createTarget
// (newWindow) opens it, a kNewWindow navigation (as a link's "Open Link in
// New Window") with a profile and no source window; then about:blank opened
// from its page (window.open), which closes. With Zen's
// open-link-in-new-unsynced-window off, the window is a synced one with
// Spaces: Cmd+W on its only tab, that blank page, keeps the window on the
// Space's empty tab (the tab itself, marked in place), and Cmd+W there closes
// the window.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       CloseTabOnALinkWindowsOnlyBlankTabKeepsTheWindow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kWindowSyncOpenLinksInBlankWindow, false);
  BrowserWindowInterface* other = nullptr;
  content::WebContents* opener = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      OpenLinkWindow(browser()->GetProfile(), &other, &opener));
  EXPECT_FALSE(IsBlankWindow(BrowserView::GetBrowserViewForBrowser(other)));
  ASSERT_TRUE(WaitForSpaceCount(other, 1));
  tabs::TabInterface* blank = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenBlankAndCloseOpener(other, opener, &blank));
  EXPECT_FALSE(IsEmptyTab(blank));
  EXPECT_TRUE(blank->GetGroup());

  base::WeakPtr<BrowserWindowInterface> window = other->GetWeakPtr();
  chrome::ExecuteCommand(other, IDC_CLOSE_TAB);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(window) << "Cmd+W closed the window";
  ASSERT_FALSE(window->IsDeleteScheduled()) << "Cmd+W closed the window";
  ASSERT_EQ(1, window->GetTabStripModel()->count());
  EXPECT_EQ(blank, window->GetTabStripModel()->GetActiveTab());
  EXPECT_TRUE(IsEmptyTab(blank));

  // Cmd+W on the empty tab closes the window.
  chrome::ExecuteCommand(other, IDC_CLOSE_TAB);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !window || window->IsDeleteScheduled(); }));
}

// The same window with Zen's open-link-in-new-unsynced-window on (its
// default): a New Blank Window, without Spaces. Cmd+W on its only tab closes
// it, as Zen closes an unsynced window with its last tab
// (gZenWorkspaces.shouldCloseWindow(): an unsynced window that isn't
// private). The e2e sidebar_tabs check failed on the Mac (cycle 18) because
// its window was this one.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       CloseTabOnANewBlankWindowsOnlyTabClosesTheWindow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kWindowSyncOpenLinksInBlankWindow));
  BrowserWindowInterface* other = nullptr;
  content::WebContents* opener = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      OpenLinkWindow(browser()->GetProfile(), &other, &opener));
  ASSERT_TRUE(IsBlankWindow(BrowserView::GetBrowserViewForBrowser(other)));
  EXPECT_FALSE(GetSpaces(other) && GetSpaces(other)->IsEnabled());
  tabs::TabInterface* blank = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenBlankAndCloseOpener(other, opener, &blank));
  EXPECT_FALSE(IsEmptyTab(blank));

  base::WeakPtr<BrowserWindowInterface> window = other->GetWeakPtr();
  chrome::ExecuteCommand(other, IDC_CLOSE_TAB);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !window || window->IsDeleteScheduled(); }));
  // The first window stays, as it was.
  EXPECT_FALSE(browser()->IsDeleteScheduled());
}

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

// A folder dragged by its row (koan_tab_drag.h): the drop puts it among its
// new parent's subfolders, and its tabs follow it in the tab strip.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, ADroppedFolderTakesItsTabsAlong) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  KoanFolderController* folders = KoanFolderController::FromBrowser(browser());
  ASSERT_TRUE(folders);
  std::vector<tabs::TabInterface*> pages;
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    pages.push_back(model->GetActiveTab());
  }
  const int first = folders->CreateFolder({pages[0]}, /*parent=*/0,
                                          folders->GetActiveSpace(), u"First",
                                          /*rename=*/false);
  const int second =
      folders->CreateFolder({pages[1]}, /*parent=*/0, folders->GetActiveSpace(),
                            u"Second", /*rename=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->GetIndexOfTab(pages[0]) < model->GetIndexOfTab(pages[1]);
  }));

  // Second above First, at the top level: its tab goes first too.
  folders->DropFolder(second, /*parent=*/0, /*before=*/first);
  std::vector<KoanFolderController::Entry> entries =
      folders->GetFolders(folders->GetActiveSpace());
  ASSERT_EQ(2u, entries.size());
  EXPECT_EQ(second, entries[0].id);
  EXPECT_EQ(first, entries[1].id);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return model->GetIndexOfTab(pages[1]) < model->GetIndexOfTab(pages[0]);
  }));

  // First into Second: a subfolder of it, its tab still in First.
  folders->DropFolder(first, /*parent=*/second, /*before=*/0);
  entries = folders->GetFolders(folders->GetActiveSpace());
  ASSERT_EQ(2u, entries.size());
  EXPECT_EQ(second, entries[0].id);
  EXPECT_EQ(first, entries[1].id);
  EXPECT_EQ(1, entries[1].depth);
  EXPECT_EQ(first, folders->GetFolderOf(pages[0]));
  EXPECT_TRUE(IsPinnedInSpace(pages[0]));

  // Never into itself or a folder of its own.
  EXPECT_FALSE(folders->CanHoldFolder(first, first));
  EXPECT_FALSE(folders->CanHoldFolder(first, second));
  folders->DropFolder(second, /*parent=*/first, /*before=*/0);
  EXPECT_EQ(0, folders->GetFolder(second)->parent);
}

// Zen's zen.folders.max-subfolders for a dropped folder: it and its
// subfolders stay within five levels.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, ADroppedFolderNestsAtMostFive) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanFolderController* folders = KoanFolderController::FromBrowser(browser());
  ASSERT_TRUE(folders);
  // A chain of five (depths 0 to 4), and a folder with one subfolder.
  std::vector<int> chain;
  for (int i = 0; i < KoanFolderController::kMaxDepth; ++i) {
    chain.push_back(folders->CreateFolder(
        {}, chain.empty() ? 0 : chain.back(), folders->GetActiveSpace(),
        base::NumberToString16(i), /*rename=*/false));
  }
  ASSERT_EQ(KoanFolderController::kMaxDepth - 1,
            folders->GetDepth(chain.back()));
  const int outer = folders->CreateFolder({}, 0, folders->GetActiveSpace(),
                                          u"Outer", /*rename=*/false);
  folders->CreateFolder({}, outer, folders->GetActiveSpace(), u"Inner",
                        /*rename=*/false);
  const int leaf = folders->CreateFolder({}, 0, folders->GetActiveSpace(),
                                         u"Leaf", /*rename=*/false);
  // Its subfolder would be at depth 4 under the third: fine; at 5 under the
  // fourth: refused. Nothing goes into the deepest.
  EXPECT_TRUE(folders->CanHoldFolder(chain[2], outer));
  EXPECT_FALSE(folders->CanHoldFolder(chain[3], outer));
  EXPECT_TRUE(folders->CanHoldFolder(chain[3], leaf));
  EXPECT_FALSE(folders->CanHoldFolder(chain[4], leaf));
  EXPECT_TRUE(folders->CanHoldFolder(0, outer));
  folders->DropFolder(outer, chain[3], 0);
  EXPECT_EQ(0, folders->GetFolder(outer)->parent);
  folders->DropFolder(outer, chain[2], 0);
  EXPECT_EQ(chain[2], folders->GetFolder(outer)->parent);
}

// Zen's collapsedByDrag: an open folder closes while it is dragged by its
// row, and opens again after it, unless it holds the active tab.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       ADraggedFolderClosesForTheDragOnly) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TabStripModel* model = browser()->GetTabStripModel();
  KoanFolderController* folders = KoanFolderController::FromBrowser(browser());
  KoanTabDrag* drag = GetSpaces(browser())->tab_drag();
  ASSERT_TRUE(folders && drag);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tabs::TabInterface* in_folder = model->GetActiveTab();
  const int folder = folders->CreateFolder({in_folder}, 0,
                                           folders->GetActiveSpace(), u"Folder",
                                           /*rename=*/false);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_NE(in_folder, model->GetActiveTab());
  ASSERT_FALSE(folders->GetFolder(folder)->collapsed);

  // (Its row shows once the folders are synced to the sidebar.)
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return drag->StartFolderForTesting(folder); }));
  EXPECT_TRUE(drag->IsDragging());
  EXPECT_TRUE(folders->GetFolder(folder)->collapsed);
  drag->CancelForTesting();
  EXPECT_FALSE(folders->GetFolder(folder)->collapsed);
  EXPECT_TRUE(base::test::RunUntil([&] { return !drag->IsDragging(); }));

  // Holding the active tab, it stays closed (showing that tab).
  model->ActivateTabAt(model->GetIndexOfTab(in_folder));
  ASSERT_TRUE(drag->StartFolderForTesting(folder));
  EXPECT_TRUE(folders->GetFolder(folder)->collapsed);
  drag->CancelForTesting();
  EXPECT_TRUE(folders->GetFolder(folder)->collapsed);
  EXPECT_TRUE(base::test::RunUntil([&] { return !drag->IsDragging(); }));
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

// EndAgentContainer is also the task/last-connection cleanup path. Ordinary
// user Spaces keep empty tabs, but an ended agent's Space must not get one.
IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest, EndingAgentTaskRemovesItsSpace) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  KoanWorkspaceController* spaces = GetSpaces(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  const tab_groups::TabGroupId user_space = *spaces->GetActiveSpace();
  content::WebContents* user_tab = model->GetActiveWebContents();
  for (const auto mode : {containers::AgentMode::kThrowaway,
                          containers::AgentMode::kUserPartition}) {
    const auto container = StartAgentTask(browser()->GetProfile(), mode);
    ASSERT_TRUE(container);
    ASSERT_TRUE(containers::OpenAgentTab(browser()->GetProfile(), container->id,
                                         GURL(url::kAboutBlankURL)));
    ASSERT_TRUE(containers::OpenAgentTab(browser()->GetProfile(), container->id,
                                         GURL(url::kAboutBlankURL)));
    ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
    EXPECT_EQ(user_space, *spaces->GetActiveSpace());

    containers::EndAgentContainer(browser()->GetProfile(), container->id);
    ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(1, model->count());
    EXPECT_EQ(user_tab, model->GetActiveWebContents());
    EXPECT_EQ(user_space, *spaces->GetActiveSpace());
    EXPECT_TRUE(
        containers::GetContainerTabs(browser()->GetProfile(), container->id)
            .empty());
  }
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       ClosingLastAgentTabRemovesItsSpace) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  const auto container = StartAgentTask(browser()->GetProfile(),
                                        containers::AgentMode::kUserPartition);
  ASSERT_TRUE(container);
  TabStripModel* model = browser()->GetTabStripModel();
  content::WebContents* agent_tab = containers::OpenAgentTab(
      browser()->GetProfile(), container->id, GURL(url::kAboutBlankURL));
  ASSERT_TRUE(agent_tab);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  model->CloseWebContentsAt(model->GetIndexOfWebContents(agent_tab),
                            TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  // The task may open another tab later: it gets a new temporary Space.
  ASSERT_TRUE(containers::OpenAgentTab(browser()->GetProfile(), container->id,
                                       GURL(url::kAboutBlankURL)));
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  containers::EndAgentContainer(browser()->GetProfile(), container->id);
  EXPECT_TRUE(WaitForSpaceCount(browser(), 1));
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       EndingAgentTaskKeepsUserTabsInItsSpace) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  const auto container = StartAgentTask(browser()->GetProfile(),
                                        containers::AgentMode::kUserPartition);
  ASSERT_TRUE(container);
  content::WebContents* agent_tab = containers::OpenAgentTab(
      browser()->GetProfile(), container->id, GURL(url::kAboutBlankURL));
  ASSERT_TRUE(agent_tab);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const auto agent_space =
      tabs::TabInterface::GetFromContents(agent_tab)->GetGroup();
  ASSERT_TRUE(agent_space);
  KoanWorkspaceController* spaces = GetSpaces(browser());
  spaces->SwitchTo(*agent_space);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(kPage), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  TabStripModel* model = browser()->GetTabStripModel();
  content::WebContents* user_tab = model->GetActiveWebContents();
  ASSERT_EQ(agent_space, model->GetActiveTab()->GetGroup());
  ASSERT_NE(containers::ContainerOf(user_tab), container->id);

  containers::EndAgentContainer(browser()->GetProfile(), container->id);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(spaces->GetSpace(*agent_space));
  ASSERT_EQ(1u, TabsOfSpace(model, *agent_space).size());
  EXPECT_EQ(user_tab,
            model->GetWebContentsAt(TabsOfSpace(model, *agent_space).front()));
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       EndingAgentTaskKeepsUserSpaceWithRobotIcon) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanWorkspaceController* spaces = GetSpaces(browser());
  spaces->CreateSpace(u"My robot", u"\U0001F916", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  const tab_groups::TabGroupId user_space = *spaces->GetActiveSpace();
  const auto container = StartAgentTask(browser()->GetProfile(),
                                        containers::AgentMode::kUserPartition);
  ASSERT_TRUE(container);
  content::WebContents* agent_tab = containers::OpenAgentTab(
      browser()->GetProfile(), container->id, GURL(url::kAboutBlankURL));
  ASSERT_TRUE(agent_tab);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  TabStripModel* model = browser()->GetTabStripModel();
  model->AddToExistingGroup({model->GetIndexOfWebContents(agent_tab)},
                            user_space);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  containers::EndAgentContainer(browser()->GetProfile(), container->id);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(spaces->GetSpace(user_space));
  EXPECT_EQ(u"My robot", spaces->GetSpace(user_space)->name);
  EXPECT_EQ(1u, TabsOfSpace(model, user_space).size());
}

IN_PROC_BROWSER_TEST_F(KoanSpacesBrowserTest,
                       EndingLastAgentSpaceLeavesOrdinaryEmptySpace) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kPage)));
  KoanWorkspaceController* spaces = GetSpaces(browser());
  const tab_groups::TabGroupId user_space = *spaces->GetActiveSpace();
  const auto container = StartAgentTask(browser()->GetProfile(),
                                        containers::AgentMode::kUserPartition);
  ASSERT_TRUE(container);
  content::WebContents* agent_tab = containers::OpenAgentTab(
      browser()->GetProfile(), container->id, GURL(url::kAboutBlankURL));
  ASSERT_TRUE(agent_tab);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const auto agent_space =
      tabs::TabInterface::GetFromContents(agent_tab)->GetGroup();
  ASSERT_TRUE(agent_space);
  spaces->DeleteSpaceForSync(user_space);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  containers::EndAgentContainer(browser()->GetProfile(), container->id);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 1 && spaces->GetSpaces().size() == 1 &&
           !spaces->GetSpace(*agent_space);
  }));
  EXPECT_TRUE(spaces->GetSpaces().front().icon.empty());
  EXPECT_TRUE(IsEmptyTab(model->GetActiveTab()));
}

}  // namespace koan
