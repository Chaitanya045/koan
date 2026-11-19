// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's Actions mode: Tab in the empty URL bar switches to it (">"), and Tab
// again leaves it. Two handlers on the omnibox once handled the first Tab
// (KoanCommandPalette's and KoanUrlBarController's), so the second Tab's ">"
// came straight back (Mac e2e palette.mjs, "A second Tab leaves").

#include <string>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/events_test_utils.h"

namespace koan {

namespace {

class KoanCommandPaletteBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  OmniboxViewViews* omnibox() {
    return browser_view()->toolbar()->location_bar_view()->omnibox_view();
  }

  // A plain Tab through the omnibox's own pre-target handlers, in the order
  // and with the stop EventDispatcher uses: where both of Koan's handlers see
  // a key before the omnibox does.
  void PressTab() {
    ui::KeyEvent event(ui::EventType::kKeyPressed, ui::VKEY_TAB, ui::EF_NONE);
    ui::EventTargetTestApi api(omnibox());
    for (ui::EventHandler* handler : api.GetPreTargetHandlers()) {
      handler->OnEvent(&event);
      if (event.stopped_propagation()) {
        break;
      }
    }
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanCommandPaletteBrowserTest, SecondTabLeaves) {
  // The URL bar open for a new tab (Zen's replace-newtab, on by default):
  // both the palette's and the URL bar's key handlers are on the omnibox.
  // The window's first tab is empty, so the bar may already be open for it
  // (KoanUrlBarController::OpenOnStartup()). OpenForNewTab() would then close
  // it again (Zen's toggle), back to the page's address.
  KoanUrlBarController* url_bar =
      KoanBrowserViewController::From(browser_view())->url_bar();
  if (!url_bar->is_new_tab()) {
    ASSERT_TRUE(url_bar->OpenForNewTab());
  }
  ASSERT_TRUE(url_bar->is_new_tab());
  ASSERT_TRUE(omnibox()->HasFocus());
  ASSERT_EQ(u"", omnibox()->GetText());

  PressTab();
  EXPECT_EQ(u">", omnibox()->GetText());
  PressTab();
  EXPECT_EQ(u"", omnibox()->GetText());
  // And back again.
  PressTab();
  EXPECT_EQ(u">", omnibox()->GetText());
}

}  // namespace koan
