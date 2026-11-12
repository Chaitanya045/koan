// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A page that is already playing when Koan puts it into a tab strip: a Glance
// opened in a split or as a tab, and a pinned page window sync moves into
// another window. Upstream's TabStatsTracker starts watching a tab when it
// joins the strip, from the page's current video state, and its observers
// CHECK that a video stopping in a tab had started there (#347: a release
// build crashed on the pause after a Glance playing a video was split). The
// patch to tab_stats_tracker.cc tells the observers what is already playing.

#include <memory>

#include "base/files/file_path.h"
#include "base/test/run_until.h"
#include "chrome/browser/metrics/tab_stats/tab_stats_tracker.h"
#include "chrome/browser/metrics/usage_scenario/tab_usage_scenario_tracker.h"
#include "chrome/browser/metrics/usage_scenario/usage_scenario_data_store.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

// content/test/data's page with long media elements (upstream's
// TabUsageScenarioTrackerBrowserTest plays the same ones).
constexpr char kMediaPage[] = "/media/session/media-session.html";
constexpr char kPlayMutedVideo[] =
    "const video = document.getElementById('long-video-loop');"
    "video.muted = true;"
    "video.play();";
constexpr char kPauseVideo[] =
    "document.getElementById('long-video-loop').pause();";
constexpr char kPlayAudio[] =
    "const audio = document.getElementById('long-audio');"
    "audio.loop = true;"
    "audio.play();";
constexpr char kPauseAudio[] = "document.getElementById('long-audio').pause();";

KoanGlanceController* GlanceOf(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? controller->glance() : nullptr;
}

bool IsPlayingVideo(content::WebContents* contents) {
  return contents->GetCurrentlyPlayingVideoCount() > 0;
}

}  // namespace

class KoanPlayingPageAdoptionBrowserTest : public InProcessBrowserTest {
 public:
  void SetUp() override {
    embedded_test_server()->ServeFilesFromSourceDirectory(
        base::FilePath(FILE_PATH_LITERAL("content/test/data")));
    InProcessBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    // An observer of its own, as upstream's tests have: the one whose CHECK
    // failed in #347, with a data store the test can read.
    tab_usage_scenario_tracker_ =
        std::make_unique<metrics::TabUsageScenarioTracker>(&data_store_);
    metrics::TabStatsTracker::GetInstance()->AddObserverAndSetInitialState(
        tab_usage_scenario_tracker_.get());
  }

  void TearDownOnMainThread() override {
    metrics::TabStatsTracker::GetInstance()->RemoveObserver(
        tab_usage_scenario_tracker_.get());
    tab_usage_scenario_tracker_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  GURL MediaPage() const { return embedded_test_server()->GetURL(kMediaPage); }

  // Opens the media page in Glance over the active tab, as a link would, and
  // returns the Glance page once it has loaded.
  content::WebContents* OpenMediaPageInGlance() {
    KoanGlanceController* glance = GlanceOf(browser());
    if (!glance) {
      return nullptr;
    }
    glance->Open(content::OpenURLParams(
                     MediaPage(), content::Referrer(),
                     WindowOpenDisposition::kNewForegroundTab,
                     ui::PAGE_TRANSITION_LINK, /*is_renderer_initiated=*/false),
                 browser()->GetTabStripModel()->GetActiveWebContents());
    content::WebContents* page = glance->contents();
    if (!page || !content::WaitForLoadStop(page)) {
      return nullptr;
    }
    return page;
  }

  // Whether the data store counts a video playing in a visible tab.
  bool TracksVisibleVideo() {
    return data_store_.TrackingPlayingVideoInActiveTabForTesting();
  }

  // Whether the data store counted audio playing since the last call.
  bool CountedAudio() {
    return data_store_.ResetIntervalData().time_playing_audio.is_positive();
  }

  UsageScenarioDataStoreImpl data_store_;
  std::unique_ptr<metrics::TabUsageScenarioTracker> tab_usage_scenario_tracker_;
};

// #347: Glance's Split on a page playing a (muted) video, then a pause. The
// tracker never saw the video start, as the Glance page wasn't a tab then.
IN_PROC_BROWSER_TEST_F(KoanPlayingPageAdoptionBrowserTest,
                       GlancePlayingVideoOpenedInSplit) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  content::WebContents* page = OpenMediaPageInGlance();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::ExecJs(page, kPlayMutedVideo));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsPlayingVideo(page); }));
  // Not a tab yet: nothing counts it.
  EXPECT_FALSE(TracksVisibleVideo());

  GlanceOf(browser())->OpenInSplit();
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  const int index = tab_strip->GetIndexOfWebContents(page);
  ASSERT_NE(index, TabStripModel::kNoTab);
  EXPECT_TRUE(tab_strip->GetTabAtIndex(index)->IsSplit());
  ASSERT_TRUE(IsPlayingVideo(page));
  // The split's pane shows the video: it counts as playing in a visible tab.
  EXPECT_TRUE(base::test::RunUntil([&] { return TracksVisibleVideo(); }));

  // Crashed here: a stop for a video the tracker never saw start.
  ASSERT_TRUE(content::ExecJs(page, kPauseVideo));
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsPlayingVideo(page); }));
  EXPECT_FALSE(TracksVisibleVideo());
}

// The same through Glance's Expand (the page becomes a tab of its own).
IN_PROC_BROWSER_TEST_F(KoanPlayingPageAdoptionBrowserTest,
                       GlancePlayingVideoExpandedToTab) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  content::WebContents* page = OpenMediaPageInGlance();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::ExecJs(page, kPlayMutedVideo));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsPlayingVideo(page); }));

  GlanceOf(browser())->ExpandToTab();
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_EQ(tab_strip->GetActiveWebContents(), page);
  ASSERT_TRUE(IsPlayingVideo(page));
  EXPECT_TRUE(base::test::RunUntil([&] { return TracksVisibleVideo(); }));

  ASSERT_TRUE(content::ExecJs(page, kPauseVideo));
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsPlayingVideo(page); }));
  EXPECT_FALSE(TracksVisibleVideo());
}

// Audio has the same gap: a tab that joins the strip audible and then goes
// quiet was a stop with no start (UsageScenarioDataStoreImpl::OnAudioStops()
// CHECKs a count above 0).
IN_PROC_BROWSER_TEST_F(KoanPlayingPageAdoptionBrowserTest,
                       GlancePlayingAudioExpandedToTab) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  content::WebContents* page = OpenMediaPageInGlance();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::ExecJs(page, kPlayAudio));
  ASSERT_TRUE(base::test::RunUntil([&] { return page->IsCurrentlyAudible(); }));
  // Not a tab yet: nothing counts it.
  data_store_.ResetIntervalData();
  EXPECT_FALSE(CountedAudio());

  GlanceOf(browser())->ExpandToTab();
  ASSERT_EQ(browser()->GetTabStripModel()->GetActiveWebContents(), page);
  EXPECT_TRUE(base::test::RunUntil([&] { return CountedAudio(); }));

  ASSERT_TRUE(content::ExecJs(page, kPauseAudio));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !page->IsCurrentlyAudible(); }));
  // Quiet now: no more audio time.
  data_store_.ResetIntervalData();
  EXPECT_FALSE(CountedAudio());
}

// Window sync's move: a window closes with a pinned page playing a video, and
// the page replaces its placeholder in the other window (a kReplaced change,
// TabStatsTracker::OnTabReplaced()). The tab it was in had stopped being
// tracked when the page left it.
IN_PROC_BROWSER_TEST_F(KoanPlayingPageAdoptionBrowserTest,
                       PinnedPagePlayingVideoMovedByWindowSync) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(WaitForSpaceCount(other, 1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(other, MediaPage()));
  TabStripModel* other_tabs = other->GetTabStripModel();
  content::WebContents* page = other_tabs->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(page, kPlayMutedVideo));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsPlayingVideo(page); }));
  GetPinnedTabs(other)->PinInSpace(other_tabs->GetActiveTab());

  // This window gets the pinned tab, as a placeholder.
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (int i = 0; i < tab_strip->count(); ++i) {
      if (IsPinnedInSpace(tab_strip->GetTabAtIndex(i))) {
        return true;
      }
    }
    return false;
  }));

  CloseBrowserSynchronously(other);
  // The page moved here, still playing.
  ASSERT_NE(tab_strip->GetIndexOfWebContents(page), TabStripModel::kNoTab);

  ASSERT_TRUE(content::ExecJs(page, kPauseVideo));
  ASSERT_TRUE(base::test::RunUntil([&] { return !IsPlayingVideo(page); }));
}

}  // namespace koan
