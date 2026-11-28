// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The window's buttons stay hidden until the pointer reaches the top edge
// (Zen's zen.view.hide-window-controls) after a colour mode change (#556). On
// Linux Chromium rebuilds the frame view on every theme change
// (BrowserWidget::RegenerateFrameOnThemeChange()), and the new frame's
// caption buttons are new views: KoanCompactModeController follows them and
// hides them again once the frame places them. Also guards the upstream
// behaviour the fix relies on: a colour scheme change replaces the frame view
// on Linux. macOS draws AppKit's buttons and doesn't rebuild the frame for a
// colour mode change: no test there.

#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "ui/compositor/layer.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"

#if BUILDFLAG(IS_LINUX)

namespace koan {

namespace {

using BrowserColorScheme = ThemeService::BrowserColorScheme;

class KoanCaptionButtonsBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  KoanCompactModeController* compact() {
    return KoanBrowserViewController::From(browser_view())->compact_mode();
  }
  BrowserFrameView* frame() {
    return browser_view()->browser_widget()->GetFrameView();
  }
  views::View* close_button() {
    return frame()->GetViewByID(VIEW_ID_CLOSE_BUTTON);
  }

  // Koan hides a button by fading its layer out, and lets it ignore the
  // pointer (the frame's layout sets its visibility).
  bool CloseButtonHidden() {
    views::test::RunScheduledLayout(browser_view()->GetWidget());
    views::View* button = close_button();
    return button && button->GetVisible() && button->layer() &&
           button->layer()->GetTargetOpacity() == 0.0f &&
           !button->GetCanProcessEventsWithinSubtree();
  }
  bool CloseButtonShown() {
    views::test::RunScheduledLayout(browser_view()->GetWidget());
    views::View* button = close_button();
    return button && button->GetVisible() &&
           (!button->layer() || button->layer()->GetTargetOpacity() == 1.0f) &&
           button->GetCanProcessEventsWithinSubtree();
  }

  // Settings > Look and Feel > Mode sets Chromium's colour scheme. On Linux
  // that rebuilds the frame view (the upstream behaviour behind #556).
  void SetColorScheme(BrowserColorScheme scheme) {
    const void* old_frame = frame();
    ThemeServiceFactory::GetForProfile(browser()->GetProfile())
        ->SetBrowserColorScheme(scheme);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return static_cast<const void*>(frame()) != old_frame;
    })) << "the colour scheme change no longer rebuilds the frame view";
  }

  // Single toolbar (the default) with its strip for the window buttons.
  void WaitForCollapsedStrip() {
    ASSERT_TRUE(base::test::RunUntil([&] {
      return compact()->mode() ==
                 KoanCompactModeController::Mode::kSingleToolbar &&
             compact()->caption_strip_collapsed();
    }));
  }
};

}  // namespace

// Sidebar on the left: the close button (at the right, over the page) is
// hidden, and stays hidden after a switch to Light and back to Dark.
IN_PROC_BROWSER_TEST_F(KoanCaptionButtonsBrowserTest,
                       StayHiddenAfterColorModeChange) {
  WaitForCollapsedStrip();
  ASSERT_TRUE(base::test::RunUntil([&] { return CloseButtonHidden(); }));

  SetColorScheme(BrowserColorScheme::kLight);
  EXPECT_TRUE(compact()->caption_strip_collapsed());
  EXPECT_TRUE(base::test::RunUntil([&] { return CloseButtonHidden(); }));

  SetColorScheme(BrowserColorScheme::kDark);
  EXPECT_TRUE(compact()->caption_strip_collapsed());
  EXPECT_TRUE(base::test::RunUntil([&] { return CloseButtonHidden(); }));
}

// Sidebar on the right: the close button is in the sidebar's top row, shown,
// and stays shown (and takes the pointer) in the rebuilt frame.
IN_PROC_BROWSER_TEST_F(KoanCaptionButtonsBrowserTest,
                       StayInSidebarRowAfterColorModeChange) {
  WaitForCollapsedStrip();
  browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kTabsOnRight, true);
  WaitForCollapsedStrip();
  ASSERT_TRUE(base::test::RunUntil([&] { return CloseButtonShown(); }));

  SetColorScheme(BrowserColorScheme::kLight);
  EXPECT_TRUE(base::test::RunUntil([&] { return CloseButtonShown(); }));
}

// Compact mode hides the sidebar, and with it the top row: the button is
// hidden before and after the change.
IN_PROC_BROWSER_TEST_F(KoanCaptionButtonsBrowserTest,
                       StayHiddenInCompactModeAfterColorModeChange) {
  WaitForCollapsedStrip();
  compact()->ToggleCompactMode();
  ASSERT_TRUE(base::test::RunUntil([&] { return compact()->hides_sidebar(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return CloseButtonHidden(); }));

  SetColorScheme(BrowserColorScheme::kLight);
  EXPECT_TRUE(compact()->hides_sidebar());
  EXPECT_TRUE(base::test::RunUntil([&] { return CloseButtonHidden(); }));
}

}  // namespace koan

#endif  // BUILDFLAG(IS_LINUX)
