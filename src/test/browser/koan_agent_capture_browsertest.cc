// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Koan.captureScaled's copy of a tab's surface (koan_agent_surface_capture.h)
// against a real window: the image is the size PlanCapture() says, it is not
// blank, a clip of a box of one colour is that colour, a hidden agent tab is
// copied under its lease, and the page sees no resize and no change of
// devicePixelRatio while it is copied.

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/test/test_future.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/agent/koan_agent_capture.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "koan/browser/agent/koan_agent_surface_capture.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/codec/png_codec.h"
#include "url/gurl.h"

namespace koan {

namespace {

using agent::CaptureRequest;
using agent::CaptureSurface;
using agent::PageViewport;
using agent::SurfaceCaptureJob;
using agent::SurfaceCaptureResult;
using agent::SurfaceGeometry;
using agent::SurfaceStatus;
using agent::ViewportRect;

// A white page with a box of one colour at (100, 100), 200x100 CSS px, that
// counts its resize events and devicePixelRatio changes.
std::unique_ptr<net::test_server::HttpResponse> ServeBox(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      "<!doctype html><title>Box</title>"
      "<style>html,body{margin:0;background:#fff}"
      "#box{position:absolute;left:100px;top:100px;width:200px;"
      "height:100px;background:rgb(0,128,255)}</style>"
      "<div id='box'></div><script>"
      "window.resizes=0;window.dprChanges=0;"
      "addEventListener('resize',()=>++window.resizes);"
      "matchMedia(`(resolution: ${devicePixelRatio}dppx)`)"
      ".addEventListener('change',()=>++window.dprChanges);"
      "</script>");
  return response;
}

constexpr SkColor kBoxColor = SkColorSetRGB(0, 128, 255);

bool Near(SkColor a, SkColor b) {
  auto near = [](int x, int y) { return std::abs(x - y) <= 6; };
  return near(SkColorGetR(a), SkColorGetR(b)) &&
         near(SkColorGetG(a), SkColorGetG(b)) &&
         near(SkColorGetB(a), SkColorGetB(b));
}

}  // namespace

class KoanAgentCaptureBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeBox));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/box")));
  }

 protected:
  content::WebContents* active() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }

  // The page's viewport, as Page.getLayoutMetrics would give it.
  static PageViewport ViewportOf(content::WebContents* contents) {
    PageViewport viewport;
    viewport.width =
        content::EvalJs(contents, "visualViewport.width").ExtractDouble();
    viewport.height =
        content::EvalJs(contents, "visualViewport.height").ExtractDouble();
    viewport.pinch =
        content::EvalJs(contents, "visualViewport.scale").ExtractDouble();
    return viewport;
  }

  // Plans `request` and copies the surface; the image, decoded.
  static std::optional<SkBitmap> Capture(content::WebContents* contents,
                                         const CaptureRequest& request,
                                         agent::CapturePlan* plan_out,
                                         bool fresh_frame = false) {
    std::optional<SurfaceGeometry> surface = agent::SurfaceGeometryOf(contents);
    EXPECT_TRUE(surface);
    if (!surface) {
      return std::nullopt;
    }
    auto plan = agent::PlanCapture(ViewportOf(contents), *surface, request);
    EXPECT_TRUE(plan.has_value());
    if (!plan.has_value()) {
      return std::nullopt;
    }
    *plan_out = *plan;
    SurfaceCaptureJob job;
    job.source = plan->source;
    job.whole = plan->whole;
    job.image_width = plan->image_width;
    job.image_height = plan->image_height;
    job.fresh_frame = fresh_frame;
    base::test::TestFuture<SurfaceCaptureResult> copied;
    CaptureSurface(contents, std::move(job), copied.GetCallback());
    SurfaceCaptureResult result = copied.Take();
    EXPECT_EQ(result.status, SurfaceStatus::kOk);
    if (result.status != SurfaceStatus::kOk) {
      return std::nullopt;
    }
    SkBitmap bitmap = gfx::PNGCodec::Decode(result.encoded);
    EXPECT_FALSE(bitmap.drawsNothing());
    EXPECT_EQ(bitmap.width(), result.width);
    EXPECT_EQ(bitmap.height(), result.height);
    return bitmap;
  }

  static void ExpectPageSawNothing(content::WebContents* contents) {
    EXPECT_EQ(content::EvalJs(contents, "window.resizes"), 0);
    EXPECT_EQ(content::EvalJs(contents, "window.dprChanges"), 0);
  }
};

IN_PROC_BROWSER_TEST_F(KoanAgentCaptureBrowserTest, VisibleTabAtEachSize) {
  const double dpr =
      content::EvalJs(active(), "devicePixelRatio").ExtractDouble();
  for (int long_edge : {320, 640, 1568}) {
    CaptureRequest request;
    request.long_edge = long_edge;
    agent::CapturePlan plan;
    std::optional<SkBitmap> image = Capture(active(), request, &plan);
    ASSERT_TRUE(image);
    // The size the plan says, at most the long edge, and not blank: the box
    // and the white page are both in it.
    EXPECT_EQ(image->width(), plan.image_width);
    EXPECT_EQ(image->height(), plan.image_height);
    EXPECT_LE(std::max(image->width(), image->height()), long_edge);
    const int box_x = (100 + 100) / plan.css_per_pixel;
    const int box_y = (100 + 50) / plan.css_per_pixel;
    EXPECT_TRUE(Near(image->getColor(box_x, box_y), kBoxColor)) << long_edge;
    EXPECT_TRUE(Near(image->getColor(2, 2), SK_ColorWHITE)) << long_edge;
  }
  EXPECT_EQ(content::EvalJs(active(), "devicePixelRatio").ExtractDouble(), dpr);
  ExpectPageSawNothing(active());
}

IN_PROC_BROWSER_TEST_F(KoanAgentCaptureBrowserTest, ClipOfTheBox) {
  CaptureRequest request;
  request.scale = agent::kMaxCaptureScale;
  request.clip = ViewportRect{100, 100, 200, 100};
  agent::CapturePlan plan;
  std::optional<SkBitmap> image = Capture(active(), request, &plan);
  ASSERT_TRUE(image);
  const double dpr =
      content::EvalJs(active(), "devicePixelRatio").ExtractDouble();
  // Device pixels: 200x100 CSS px at the display's scale.
  EXPECT_EQ(image->width(), static_cast<int>(200 * dpr));
  EXPECT_EQ(image->height(), static_cast<int>(100 * dpr));
  // The box's colour everywhere but a pixel's rounding at the edges.
  for (int y = 2; y < image->height() - 2; y += 7) {
    for (int x = 2; x < image->width() - 2; x += 7) {
      ASSERT_TRUE(Near(image->getColor(x, y), kBoxColor)) << x << "," << y;
    }
  }
  ExpectPageSawNothing(active());
}

IN_PROC_BROWSER_TEST_F(KoanAgentCaptureBrowserTest, HiddenAgentTabUnderLease) {
  content::WebContents* first = active();
  // A background tab, never shown: an agent's tab.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/box"),
      WindowOpenDisposition::kNewBackgroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* hidden =
      browser()->GetTabStripModel()->GetWebContentsAt(
          browser()->GetTabStripModel()->count() - 1);
  ASSERT_NE(hidden, first);
  EXPECT_NE(hidden->GetVisibility(), content::Visibility::VISIBLE);
  agent::AgentLeaseManager::Get().Attach(hidden, "client", "task",
                                         /*agent_owned=*/true);
  CaptureRequest request;
  request.long_edge = 640;
  agent::CapturePlan plan;
  std::optional<SkBitmap> image =
      Capture(hidden, request, &plan, /*fresh_frame=*/true);
  ASSERT_TRUE(image);
  EXPECT_EQ(image->width(), plan.image_width);
  EXPECT_EQ(image->height(), plan.image_height);
  const int box_x = (100 + 100) / plan.css_per_pixel;
  const int box_y = (100 + 50) / plan.css_per_pixel;
  EXPECT_TRUE(Near(image->getColor(box_x, box_y), kBoxColor));
  EXPECT_TRUE(Near(image->getColor(2, 2), SK_ColorWHITE));
  // The user's tab stays where it was.
  EXPECT_EQ(active(), first);
  agent::AgentLeaseManager::Get().Detach(hidden);
}

IN_PROC_BROWSER_TEST_F(KoanAgentCaptureBrowserTest, MasksArePainted) {
  CaptureRequest request;
  request.long_edge = 640;
  std::optional<SurfaceGeometry> surface = agent::SurfaceGeometryOf(active());
  ASSERT_TRUE(surface);
  auto plan = agent::PlanCapture(ViewportOf(active()), *surface, request);
  ASSERT_TRUE(plan.has_value());
  const ViewportRect box{100, 100, 200, 100};
  SurfaceCaptureJob job;
  job.whole = true;
  job.source = plan->source;
  job.image_width = plan->image_width;
  job.image_height = plan->image_height;
  job.masks = agent::MaskPixels(*plan, base::span_from_ref(box), 0);
  ASSERT_EQ(job.masks.size(), 1u);
  base::test::TestFuture<SurfaceCaptureResult> copied;
  CaptureSurface(active(), std::move(job), copied.GetCallback());
  SurfaceCaptureResult result = copied.Take();
  ASSERT_EQ(result.status, SurfaceStatus::kOk);
  SkBitmap image = gfx::PNGCodec::Decode(result.encoded);
  const int box_x = (100 + 100) / plan->css_per_pixel;
  const int box_y = (100 + 50) / plan->css_per_pixel;
  // Painted over: not the box's colour any more.
  EXPECT_FALSE(Near(image.getColor(box_x, box_y), kBoxColor));
}

}  // namespace koan
