// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The sidebar's two-finger Space swipe on Linux, end to end: the touchpad's
// scroll events go in at the window's root (as Wayland's event source sends
// them: a fling cancel, the scrolls, a fling start at the lift) and through
// Aura, DesktopNativeWidgetAura and views::Widget, which also make wheel
// events of them (TouchpadWheels, koan_swipe_input.h). PR #529's first Linux
// build switched twice on a slow swipe: upstream's wheel switch acted on those
// wheel events too.

#include <cstdlib>
#include <memory>
#include <string>

#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_space_add_swipe.h"
#include "koan/browser/ui/views/koan_space_header.h"
#include "koan/browser/ui/views/koan_space_swipe.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/aura/window.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/test/event_generator.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// The backend's touchpad offsets for one compositor pixel of finger travel on
// Wayland (12 there, WaylandPointer::OnAxis()): a whole swipe is 275 of them
// on either backend (LinuxSwipePixelSize()).
float OffsetsPerPixel() {
  return KoanSpaceSwipe::PixelSize() / 275.f;
}

class KoanSpaceSwipeBrowserTest : public InProcessBrowserTest {
 protected:
  KoanWorkspaceController* spaces() { return GetSpaces(browser()); }

  // Three Spaces, "Space", "Work" and "Play", on `active`.
  void SetUpSpaces(const std::u16string& active) {
    ASSERT_TRUE(spaces());
    ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
    spaces()->CreateSpace(u"Work", u"", {});
    ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
    spaces()->CreateSpace(u"Play", u"", {});
    ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
    for (const auto& space : spaces()->GetSpaces()) {
      if (space.name == active) {
        spaces()->SwitchTo(space.id);
      }
    }
    Settle();
    ASSERT_EQ(active, ActiveName());
  }

  std::u16string ActiveName() {
    return spaces()->GetSpace(*spaces()->GetActiveSpace())->name;
  }

  // Lets a Space's switch slide (250 ms) end: a swipe can't start on a Space
  // still sliding in.
  void Settle() {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(600));
    run_loop.Run();
  }

  // The middle of the Space's tab list, on the screen.
  gfx::Point TabListPoint() {
    return spaces()
        ->space_header()
        ->parent()
        ->GetBoundsInScreen()
        .CenterPoint();
  }

  ui::test::EventGenerator& generator() {
    if (!generator_) {
      generator_ = std::make_unique<ui::test::EventGenerator>(
          BrowserView::GetBrowserViewForBrowser(browser())
              ->GetWidget()
              ->GetNativeWindow()
              ->GetRootWindow());
    }
    return *generator_;
  }

  // The tester's slow swipe over the Space's tab list: one pixel of finger
  // travel every 16 ms, `pixels` of them (negative: to the left, towards the
  // next Space), with the lift unless `hold`.
  void SlowSwipe(int pixels, bool hold = false) {
    generator().ScrollSequence(
        TabListPoint(), base::Milliseconds(16), pixels * OffsetsPerPixel(), 0,
        std::abs(pixels), /*num_fingers=*/2,
        hold ? ui::test::EventGenerator::ScrollSequenceType::StartAndScroll
             : ui::test::EventGenerator::ScrollSequenceType::UpToFling);
  }

  // The fingers lifting after a held SlowSwipe().
  void Lift() {
    ui::ScrollEvent lift(ui::EventType::kScrollFlingStart, TabListPoint(),
                         ui::EventTimeForNow(), 0, 0, 0, 0, 0,
                         /*finger_count=*/2);
    generator().Dispatch(&lift);
  }

  std::unique_ptr<ui::test::EventGenerator> generator_;

  // Whether the add-Space swipe's badge is showing.
  bool AddSpaceBadgeShown() {
    for (views::View* child : spaces()->space_header()->parent()->children()) {
      if (views::IsViewClass<KoanSpaceAddSwipeView>(child)) {
        return child->GetVisible();
      }
    }
    return false;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanSpaceSwipeBrowserTest, ShortSlowSwipeSpringsBack) {
  SetUpSpaces(u"Work");
  SlowSwipe(-30);
  Settle();
  EXPECT_EQ(u"Work", ActiveName());
  SlowSwipe(30);
  Settle();
  EXPECT_EQ(u"Work", ActiveName());
}

IN_PROC_BROWSER_TEST_F(KoanSpaceSwipeBrowserTest, SlowSwipeGoesOneSpace) {
  SetUpSpaces(u"Work");
  SlowSwipe(-120);
  Settle();
  EXPECT_EQ(u"Play", ActiveName());
  SlowSwipe(120);
  Settle();
  EXPECT_EQ(u"Work", ActiveName());
  SlowSwipe(120);
  Settle();
  EXPECT_EQ(u"Space", ActiveName());
}

IN_PROC_BROWSER_TEST_F(KoanSpaceSwipeBrowserTest,
                       SwipePastTheLastSpaceShowsTheAddSpaceBadge) {
  SetUpSpaces(u"Play");
  SlowSwipe(-260, /*hold=*/true);
  EXPECT_EQ(u"Play", ActiveName());
  EXPECT_TRUE(AddSpaceBadgeShown());
  // The lift past the badge's ready point opens the Create-a-Space form.
  Lift();
  EXPECT_TRUE(spaces()->IsCreateFormOpen());
}

}  // namespace koan
