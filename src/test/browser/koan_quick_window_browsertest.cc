// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Arc's Little Arc (#472, .agent/notes/upstream/quick-window.md): with "Links
// from other apps open in: a quick window", a link from another app that no
// Space route takes opens in a quick window (a popup kept out of session
// restore), the main window unchanged; Move to Space takes the same page to
// the Space. The links come in as on Linux, through the process singleton
// (StartupBrowserCreator::ProcessCommandLineAlreadyRunning()), which ends in
// the same StartupBrowserCreatorImpl::OpenTabsInBrowser() hook as macOS's
// application:openURLs:.

#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_init_state.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_quick_window_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "url/gurl.h"

namespace koan {

namespace {

class KoanQuickWindowBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }
  TabStripModel* tabs() { return browser()->GetTabStripModel(); }

  void ChooseQuickWindow() {
    profile()->GetPrefs()->SetString(prefs::kExternalLinksOpenIn,
                                     prefs::kExternalLinksInQuickWindow);
  }

  // `url` from another app, as Linux's `koan <url>` while Koan runs.
  void OpenFromOtherApp(const GURL& url) {
    base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
    command_line.AppendArg(url.spec());
    StartupBrowserCreator::ProcessCommandLineAlreadyRunning(
        command_line, base::FilePath(),
        {profile()->GetPath(), StartupProfileMode::kBrowserWindow});
  }

  size_t WindowCount() {
    return ProfileBrowserCollection::GetForProfile(profile())->GetSize();
  }

  // The profile's quick window, or null.
  BrowserWindowInterface* FindQuickWindow() {
    BrowserWindowInterface* found = nullptr;
    ProfileBrowserCollection::GetForProfile(profile())->ForEach(
        [&](BrowserWindowInterface* browser) {
          if (IsQuickWindow(browser)) {
            found = browser;
            return false;
          }
          return true;
        });
    return found;
  }

  GURL PageUrl() { return embedded_test_server()->GetURL("/title1.html"); }
};

// The default: the link opens as a tab, as before (Space routing).
IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, DefaultOpensInSpace) {
  const int count = tabs()->count();
  OpenFromOtherApp(PageUrl());
  EXPECT_EQ(tabs()->count(), count + 1);
  EXPECT_EQ(WindowCount(), 1u);
  EXPECT_FALSE(FindQuickWindow());
}

IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, OpensInQuickWindow) {
  ChooseQuickWindow();
  const int count = tabs()->count();
  content::WebContents* active = tabs()->GetActiveWebContents();
  ui_test_utils::BrowserCreatedObserver created;
  OpenFromOtherApp(PageUrl());
  BrowserWindowInterface* quick = created.Wait();
  ASSERT_TRUE(quick);
  EXPECT_TRUE(IsQuickWindow(quick));
  EXPECT_EQ(quick->GetType(), BrowserWindowInterface::TYPE_POPUP);
  // Not restored after a restart.
  EXPECT_TRUE(BrowserInitState::From(quick)->omit_from_session_restore());
  ASSERT_EQ(quick->GetTabStripModel()->count(), 1);
  EXPECT_EQ(quick->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL(),
            PageUrl());
  // The main window shows what it showed.
  EXPECT_EQ(tabs()->count(), count);
  EXPECT_EQ(tabs()->GetActiveWebContents(), active);
  // Not a Spaces window.
  EXPECT_FALSE(GetSpaces(quick) && GetSpaces(quick)->IsEnabled());
}

// A route still wins: the link goes to the Space, not to a quick window.
IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, RouteWins) {
  ChooseQuickWindow();
  {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kSpaceRoutes);
    base::DictValue route;
    route.Set("reference", PageUrl().host());
    route.Set("match_type", "contains");
    // "Most recent space".
    route.Set("space", "");
    update->Append(std::move(route));
  }
  const int count = tabs()->count();
  OpenFromOtherApp(PageUrl());
  EXPECT_EQ(tabs()->count(), count + 1);
  EXPECT_FALSE(FindQuickWindow());
}

// Only web pages: anything else opens as before.
IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, OnlyWebPages) {
  ChooseQuickWindow();
  const int count = tabs()->count();
  OpenFromOtherApp(GURL("data:text/html,<title>Data</title>"));
  EXPECT_EQ(tabs()->count(), count + 1);
  EXPECT_FALSE(FindQuickWindow());
}

// Move to Space: the same page (no reload, its state kept) becomes a tab of
// the Space, and the quick window closes.
IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, MoveToSpaceKeepsThePage) {
  ChooseQuickWindow();
  ui_test_utils::BrowserCreatedObserver created;
  OpenFromOtherApp(PageUrl());
  BrowserWindowInterface* quick = created.Wait();
  ASSERT_TRUE(quick);
  content::WebContents* page =
      quick->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  // State only the live page has.
  ASSERT_TRUE(content::ExecJs(page, "window.koanState = 42;"));
  const std::optional<tab_groups::TabGroupId> space =
      GetSpaces(browser())->GetActiveSpace();
  ASSERT_TRUE(space);

  KoanBrowserViewController* controller = KoanBrowserViewController::From(
      BrowserView::GetBrowserViewForBrowser(quick));
  ASSERT_TRUE(controller && controller->quick_window());
  ui_test_utils::BrowserDestroyedObserver closed(quick);
  controller->quick_window()->MoveToSpace(space);
  closed.Wait();

  EXPECT_EQ(tabs()->GetActiveWebContents(), page);
  EXPECT_EQ(tabs()->GetTabGroupForTab(tabs()->active_index()), space);
  EXPECT_EQ(content::EvalJs(page, "window.koanState"), 42);
  EXPECT_EQ(WindowCount(), 1u);
}

// The page opens in the profile (#318) of the most recent Space.
IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, TakesTheSpacesProfile) {
  ChooseQuickWindow();
  std::string container;
  {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> added =
        containers::AddUserContainer(update.Get(), u"Work");
    ASSERT_TRUE(added);
    container = containers::Serialize(added->id);
  }
  KoanWorkspaceController* spaces = GetSpaces(browser());
  spaces->SetSpaceContainer(*spaces->GetActiveSpace(), container);

  ui_test_utils::BrowserCreatedObserver created;
  OpenFromOtherApp(PageUrl());
  BrowserWindowInterface* quick = created.Wait();
  ASSERT_TRUE(quick);
  const std::optional<containers::ContainerId> id = containers::ContainerOf(
      quick->GetTabStripModel()->GetActiveWebContents());
  ASSERT_TRUE(id);
  EXPECT_EQ(containers::Serialize(*id), container);
}

// Esc closes it.
IN_PROC_BROWSER_TEST_F(KoanQuickWindowBrowserTest, EscCloses) {
  ChooseQuickWindow();
  ui_test_utils::BrowserCreatedObserver created;
  OpenFromOtherApp(PageUrl());
  BrowserWindowInterface* quick = created.Wait();
  ASSERT_TRUE(quick);
  KoanBrowserViewController* controller = KoanBrowserViewController::From(
      BrowserView::GetBrowserViewForBrowser(quick));
  ASSERT_TRUE(controller && controller->quick_window());
  ui_test_utils::BrowserDestroyedObserver closed(quick);
  EXPECT_TRUE(controller->quick_window()->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
  closed.Wait();
  EXPECT_EQ(WindowCount(), 1u);
}

}  // namespace

}  // namespace koan
