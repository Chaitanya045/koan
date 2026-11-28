// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The window's top edge resizes with the toolbar slid away (#574). Single
// toolbar (the default) and compact mode move the top container above the
// window with a layer transform, so BrowserView::GetClientAreaTop() (the
// toolbar's top, transform included) is negative there. Upstream's
// BrowserView::NonClientHitTest() took everything below that top as client
// area, also the frame's resize border above the BrowserView, and the frame
// (OpaqueBrowserFrameView) returns the client view's answer before its own
// resize-border check. koan::ClientAreaTopForHitTest(), the hook there, keeps
// that top at the BrowserView's. macOS resizes in AppKit, not through this
// hit test, and has its own runs (scripts/mac).

#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if BUILDFLAG(IS_LINUX)

namespace koan {

namespace {

class KoanTopEdgeHitTestBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  KoanCompactModeController* compact() {
    return KoanBrowserViewController::From(browser_view())->compact_mode();
  }

  // The toolbar's top in the BrowserView, as GetClientAreaTop() computes it
  // (that one is private).
  int ToolbarTop() {
    views::View* toolbar = browser_view()->toolbar();
    return views::View::ConvertPointToTarget(toolbar, browser_view(),
                                             toolbar->origin())
        .y();
  }

  // The toolbar is slid out of the window, above it.
  bool ToolbarIsAway() {
    views::test::RunScheduledLayout(browser_view());
    return compact()->hides_toolbar() && !compact()->is_toolbar_revealed() &&
           ToolbarTop() < 0;
  }

  // The BrowserView's and the frame's answers at `point`, in the
  // BrowserView's parent's coordinates (the client view's, which the frame
  // view shares).
  int BrowserViewHitTest(const gfx::Point& point) {
    return browser_view()->NonClientHitTest(point);
  }
  int FrameHitTest(const gfx::Point& point) {
    return browser_view()->GetWidget()->non_client_view()->NonClientHitTest(
        point);
  }
};

}  // namespace

// With the default layout (single toolbar, toolbar hidden), a point just
// above the BrowserView is not client area, over the sidebar and over the
// page, so the frame's resize border there is the frame's again; the
// BrowserView's own points stay client area.
IN_PROC_BROWSER_TEST_F(KoanTopEdgeHitTestBrowserTest,
                       AboveTheWindowIsNotClientWithToolbarAway) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  // Guards the upstream behaviour the bug came from: the toolbar's top
  // includes the slide's transform, so it is above the BrowserView.
  ASSERT_TRUE(base::test::RunUntil([&] { return ToolbarIsAway(); }));
  ASSERT_EQ(compact()->mode(), KoanCompactModeController::Mode::kSingleToolbar);

  const gfx::Rect bounds = browser_view()->bounds();
  // Over the sidebar (the window's left) and over the page.
  for (const int x :
       {bounds.x() + 20, bounds.CenterPoint().x(), bounds.right() - 20}) {
    SCOPED_TRACE(x);
    const gfx::Point above(x, bounds.y() - 1);
    EXPECT_NE(BrowserViewHitTest(above), HTCLIENT);
    // The frame decides there: its resize border, or its caption where the
    // frame has no border (never the client area, as before the fix).
    if (bounds.y() > 0) {
      EXPECT_NE(FrameHitTest(above), HTCLIENT);
    }
  }
  // Inside the page the client area is unchanged.
  EXPECT_EQ(BrowserViewHitTest(bounds.CenterPoint()), HTCLIENT);
}

}  // namespace koan

#endif  // BUILDFLAG(IS_LINUX)
