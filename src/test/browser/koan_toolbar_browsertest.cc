// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Z15 (.agent/notes/upstream/toolbar-chromium-ui.md): the Chromium-only UI
// that Zen has no equivalent of stays out of the toolbar and the URL bar, in
// either layout. Each check also guards the upstream behaviour its lock
// relies on.

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/page_action/page_action_container_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "content/public/test/browser_test.h"
#include "ui/actions/actions.h"
#include "url/gurl.h"

namespace koan {

namespace {

class KoanToolbarBrowserTest : public InProcessBrowserTest {
 protected:
  ToolbarView* toolbar() {
    return BrowserView::GetBrowserViewForBrowser(browser())->toolbar();
  }

  actions::ActionItem* FindAction(actions::ActionId id) {
    return actions::ActionManager::Get().FindAction(
        id, BrowserActions::From(browser())->root_action_item());
  }
};

}  // namespace

// No avatar: upstream sets its visibility only in ToolbarView::Init().
IN_PROC_BROWSER_TEST_F(KoanToolbarBrowserTest, NoAvatar) {
  if (AvatarToolbarButton* avatar = toolbar()->avatar_toolbar_button()) {
    EXPECT_FALSE(avatar->GetVisible());
  }
}

// No browser sign-in by default (the app menu's sign-in and sync rows).
IN_PROC_BROWSER_TEST_F(KoanToolbarBrowserTest, NoBrowserSignInByDefault) {
  EXPECT_FALSE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSigninAllowedOnNextStartup));
}

// No star, reading mode or cookie icon, and no text by the site icon, on a
// page where Chromium shows them (the star, and the chip's page name).
IN_PROC_BROWSER_TEST_F(KoanToolbarBrowserTest, NoUrlBarExtras) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  LocationBarView* location_bar = toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  page_actions::PageActionView* star =
      location_bar->page_action_container()->GetPageActionView(
          kActionBookmarkThisTab);
  ASSERT_TRUE(star);
  EXPECT_FALSE(star->GetVisible());
  EXPECT_TRUE(location_bar->location_icon_view()->GetText().empty());

  // Hidden by their items, which their page actions follow.
  for (const actions::ActionId id :
       {kActionSidePanelShowReadAnything, kActionShowCookieControls}) {
    actions::ActionItem* item = FindAction(id);
    ASSERT_TRUE(item) << id;
    EXPECT_FALSE(item->GetVisible()) << id;
  }
  // The star's item stays visible: it also backs the app menu's row.
  ASSERT_TRUE(FindAction(kActionBookmarkThisTab));
  EXPECT_TRUE(FindAction(kActionBookmarkThisTab)->GetVisible());
}

}  // namespace koan
