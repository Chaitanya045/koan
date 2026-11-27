// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Fullscreen on Linux (#518), as on macOS and as Zen does on Linux: the
// window's fullscreen (F11) keeps Koan's layout (the sidebar beside the
// page, the card, compact mode borderless with its reveals, the URL bar),
// and the page's own fullscreen (a video) hides it all, inside F11 or from a
// window, with the layout back as it was when the page leaves it. Chromium
// alone hides its UI in every fullscreen on Linux (no immersive mode there):
// koan::KeepsLayoutInFullscreen(), the
// BrowserWindowFullscreenController::ShouldHideUIForFullscreen() hook, says
// which. macOS has its own runs (scripts/mac).

#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/fullscreen/browser_window_fullscreen_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/test/views_test_utils.h"

#if BUILDFLAG(IS_LINUX)

namespace koan {

namespace {

class KoanFullscreenBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  KoanCompactModeController* compact() {
    return KoanBrowserViewController::From(browser_view())->compact_mode();
  }
  KoanUrlBarController* url_bar() {
    return KoanBrowserViewController::From(browser_view())->url_bar();
  }
  content::WebContents* page() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }
  bool HidesUI() {
    return BrowserWindowFullscreenController::From(browser())
        ->ShouldHideUIForFullscreen();
  }

  // F11.
  void ToggleWindowFullscreen() {
    ui_test_utils::ToggleFullscreenModeAndWait(browser());
    views::test::RunScheduledLayout(browser_view());
  }

  // A video's fullscreen button: the page asks, as upstream's tests do.
  void EnterPageFullscreen() {
    ui_test_utils::FullscreenWaiter waiter(browser(), {.tab_fullscreen = true});
    page()->GetDelegate()->EnterFullscreenModeForTab(
        page()->GetPrimaryMainFrame(), {});
    waiter.Wait();
    views::test::RunScheduledLayout(browser_view());
  }
  void ExitPageFullscreen() {
    ui_test_utils::FullscreenWaiter waiter(browser(),
                                           {.tab_fullscreen = false});
    page()->GetDelegate()->ExitFullscreenModeForTab(page());
    waiter.Wait();
    views::test::RunScheduledLayout(browser_view());
  }

  // The sidebar is on screen beside the page, with its width.
  bool SidebarShows() {
    views::test::RunScheduledLayout(browser_view());
    const views::View* region = browser_view()->tab_strip_view();
    return region && region->IsDrawn() && region->width() > 0;
  }

  void TurnOnCompactMode() {
    compact()->ToggleCompactMode();
    ASSERT_TRUE(
        base::test::RunUntil([&] { return compact()->hides_sidebar(); }));
  }
};

}  // namespace

// F11 keeps the sidebar beside the page, which stays a card with Zen's
// gutter, and the URL bar opens (Ctrl+L, Ctrl+T) as in a window.
IN_PROC_BROWSER_TEST_F(KoanFullscreenBrowserTest, WindowFullscreenKeepsLayout) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ToggleWindowFullscreen();
  ASSERT_TRUE(browser_view()->IsFullscreen());

  EXPECT_FALSE(HidesUI());
  EXPECT_FALSE(IsContentFullscreen(browser_view()));
  EXPECT_TRUE(browser_view()->ShouldDrawVerticalTabStrip());
  EXPECT_TRUE(base::test::RunUntil([&] { return SidebarShows(); }));
  EXPECT_TRUE(UseContentCard(browser_view()));
  EXPECT_TRUE(KeepsTopContainerInFullscreen(browser_view()));
  EXPECT_TRUE(HidesFullscreenTopChrome(browser_view()));
  // Zen keeps the card's margins in fullscreen without compact mode.
  EXPECT_FALSE(ContentCardInsets(browser_view()).IsEmpty());

  // Ctrl+L: Chromium keeps the command (its main UI shows), Koan's URL bar
  // opens.
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FOCUS_LOCATION));
  chrome::ExecuteCommand(browser(), IDC_FOCUS_LOCATION);
  EXPECT_TRUE(base::test::RunUntil([&] { return url_bar()->is_open(); }));
  url_bar()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !url_bar()->is_open(); }));
  // Ctrl+T: the URL bar for a new tab.
  EXPECT_TRUE(url_bar()->OpenForNewTab());
  EXPECT_TRUE(url_bar()->is_new_tab());
  url_bar()->Close();

  // Leaving it: the window as before.
  ToggleWindowFullscreen();
  ASSERT_FALSE(browser_view()->IsFullscreen());
  EXPECT_TRUE(base::test::RunUntil([&] { return SidebarShows(); }));
  EXPECT_TRUE(UseContentCard(browser_view()));
}

// Compact mode in F11: the page fills the screen (Zen's
// zen.view.borderless-fullscreen, also on Linux), and the hidden sidebar
// still comes out (here by Zen's "show the sidebar" shortcut; the screen's
// edge is the PR's manual step).
IN_PROC_BROWSER_TEST_F(KoanFullscreenBrowserTest, CompactModeIsBorderless) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  TurnOnCompactMode();
  ToggleWindowFullscreen();
  ASSERT_TRUE(browser_view()->IsFullscreen());

  EXPECT_FALSE(HidesUI());
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return IsCompactModeActive(browser_view()); }));
  EXPECT_EQ(ContentCardInsets(browser_view()), gfx::Insets());
  const gfx::RoundedCornersF square;
  EXPECT_EQ(ActiveContentsRadii(browser_view(), square), square);

  ASSERT_TRUE(compact()->PeekSidebar());
  EXPECT_TRUE(base::test::RunUntil([&] { return SidebarShows(); }));
  compact()->PeekSidebar();

  ToggleWindowFullscreen();
  ASSERT_FALSE(browser_view()->IsFullscreen());
  EXPECT_TRUE(IsCompactModeActive(browser_view()));
  // Out of fullscreen the card has its gutter again.
  EXPECT_FALSE(ContentCardInsets(browser_view()).IsEmpty());
}

// A video's fullscreen inside F11 shows only the video: no sidebar, toolbar,
// bookmarks bar, card or URL bar, and compact mode stays as it was. Leaving
// it, F11's layout is back, with Chromium's commands and bookmarks bar.
IN_PROC_BROWSER_TEST_F(KoanFullscreenBrowserTest,
                       PageFullscreenInsideWindowFullscreenHidesAll) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  // Koan's bookmarks bar (off by default) shows in F11 as in a window.
  browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kBookmarksBarShow,
                                                  true);
  TurnOnCompactMode();
  ToggleWindowFullscreen();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return browser_view()->IsBookmarkBarVisible(); }));

  EnterPageFullscreen();
  auto* const fullscreen =
      ExclusiveAccessManager::From(browser())->fullscreen_controller();
  ASSERT_TRUE(fullscreen->IsTabFullscreen());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_TRUE(IsContentFullscreen(browser_view()));
  EXPECT_TRUE(HidesUI());
  EXPECT_FALSE(browser_view()->ShouldDrawVerticalTabStrip());
  EXPECT_FALSE(SidebarShows());
  EXPECT_FALSE(browser_view()->IsToolbarVisible());
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !browser_view()->IsBookmarkBarVisible(); }));
  EXPECT_EQ(ContentCardInsets(browser_view()), gfx::Insets());
  EXPECT_FALSE(url_bar()->OpenForTabSearch());
  EXPECT_FALSE(url_bar()->is_open());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !chrome::IsCommandEnabled(browser(), IDC_FOCUS_LOCATION);
  }));
  // Koan's layout is suspended, not dropped: compact mode stays on, so
  // nothing slides when the video ends.
  EXPECT_TRUE(UseContentCard(browser_view()));
  EXPECT_NE(compact()->mode(), KoanCompactModeController::Mode::kNone);

  ExitPageFullscreen();
  ASSERT_TRUE(fullscreen->IsFullscreenForBrowser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_FALSE(IsContentFullscreen(browser_view()));
  EXPECT_FALSE(HidesUI());
  EXPECT_TRUE(browser_view()->ShouldDrawVerticalTabStrip());
  EXPECT_TRUE(IsCompactModeActive(browser_view()));
  EXPECT_EQ(ContentCardInsets(browser_view()).top(),
            GetDockedBookmarksBarHeight(browser_view()));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return browser_view()->IsBookmarkBarVisible(); }));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return chrome::IsCommandEnabled(browser(), IDC_FOCUS_LOCATION); }));
}

// A video's fullscreen from a window: the same, and the window comes back
// with its sidebar and card.
IN_PROC_BROWSER_TEST_F(KoanFullscreenBrowserTest,
                       PageFullscreenFromWindowHidesAll) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_TRUE(base::test::RunUntil([&] { return SidebarShows(); }));

  EnterPageFullscreen();
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_TRUE(IsContentFullscreen(browser_view()));
  EXPECT_TRUE(HidesUI());
  EXPECT_FALSE(SidebarShows());
  EXPECT_FALSE(browser_view()->IsToolbarVisible());
  EXPECT_EQ(ContentCardInsets(browser_view()), gfx::Insets());
  EXPECT_FALSE(url_bar()->OpenForTabSearch());

  ExitPageFullscreen();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !browser_view()->IsFullscreen(); }));
  EXPECT_FALSE(IsContentFullscreen(browser_view()));
  EXPECT_TRUE(base::test::RunUntil([&] { return SidebarShows(); }));
  EXPECT_FALSE(ContentCardInsets(browser_view()).IsEmpty());
}

}  // namespace koan

#endif  // BUILDFLAG(IS_LINUX)
