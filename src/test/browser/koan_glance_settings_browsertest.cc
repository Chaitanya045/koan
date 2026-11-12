// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// #326: Glance's settings (on or off, the trigger) reach pages already open,
// as in Zen. Blink decides whether a click is a Glance click, from the
// trigger the browser pushes to every renderer process on a change; a page
// used to keep the one its process started with.

#include <memory>

#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ipc/ipc_channel_proxy.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/common/koan_renderer_settings.mojom.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "url/gurl.h"

namespace koan {

namespace {

// "/": a link over the whole page, which a click in the page's middle hits;
// anything else a page to open.
std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      request.relative_url == "/"
          ? "<!doctype html><body style='margin: 0'><a href='/glanced' "
            "style='position: fixed; inset: 0'>glance me</a>"
          : "<!doctype html><title>Glanced</title>");
  return response;
}

KoanGlanceController* GlanceOf(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? controller->glance() : nullptr;
}

// Waits until `page`'s renderer has taken the settings pushed so far: they
// and this round trip share the process's IPC channel, in order.
void FlushRendererSettings(content::WebContents* page) {
  mojo::AssociatedRemote<mojom::RendererSettings> settings;
  page->GetPrimaryMainFrame()
      ->GetProcess()
      ->GetChannel()
      ->GetRemoteAssociatedInterface(&settings);
  settings.FlushForTesting();
}

}  // namespace

class KoanGlanceSettingsBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  // Opens the link page in the active tab, ready for clicks.
  content::WebContents* OpenLinkPage() {
    if (!ui_test_utils::NavigateToURL(browser(),
                                      embedded_test_server()->GetURL("/"))) {
      return nullptr;
    }
    content::WebContents* page =
        browser()->GetTabStripModel()->GetActiveWebContents();
    content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(page);
    return page;
  }

  // Clicks the page's link with `modifiers`; returns whether it opened in
  // Glance.
  bool ClickOpensGlance(content::WebContents* page, int modifiers) {
    content::SimulateMouseClick(page, modifiers,
                                blink::WebMouseEvent::Button::kLeft);
    KoanGlanceController* glance = GlanceOf(browser());
    return glance && base::test::RunUntil([&] { return glance->is_open(); });
  }
};

// A trigger change applies to a page opened before it: Shift+click, a new
// window before the change, opens Glance.
IN_PROC_BROWSER_TEST_F(KoanGlanceSettingsBrowserTest,
                       TriggerChangeReachesAnOpenPage) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  content::WebContents* page = OpenLinkPage();
  ASSERT_TRUE(page);
  ASSERT_TRUE(GlanceOf(browser()));
  ASSERT_FALSE(GlanceOf(browser())->is_open());

  prefs()->SetString(prefs::kGlanceActivationMethod, "shift");
  FlushRendererSettings(page);
  EXPECT_TRUE(ClickOpensGlance(page, blink::WebInputEvent::kShiftKey));
}

// Turning Glance on applies to a page opened while it was off, whose
// renderer had no trigger then (an Alt+click downloaded the link).
IN_PROC_BROWSER_TEST_F(KoanGlanceSettingsBrowserTest,
                       TurningGlanceOnReachesAPageOpenedWhileOff) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  prefs()->SetBoolean(prefs::kGlanceEnabled, false);
  content::WebContents* page = OpenLinkPage();
  ASSERT_TRUE(page);
  ASSERT_TRUE(GlanceOf(browser()));

  prefs()->SetBoolean(prefs::kGlanceEnabled, true);
  FlushRendererSettings(page);
  EXPECT_TRUE(ClickOpensGlance(page, blink::WebInputEvent::kAltKey));
}

}  // namespace koan
