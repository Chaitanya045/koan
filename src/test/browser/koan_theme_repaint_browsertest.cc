// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Koan's views that paint to a layer of their own and read their colours
// when they paint: a theme change doesn't repaint such a layer by itself
// (View::OnThemeChanged() schedules no paint, and the widget's repaint only
// reaches its root layer), so after a Mode change they kept the old mode's
// colours until hovered (#564). Each schedules a paint in its
// OnThemeChanged(); these tests check that the whole layer repaints when the
// Mode changes, for the Library and the sidebar notification.

#include <string_view>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_view.h"
#include "koan/browser/ui/views/koan_sidebar_notification.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_delegate.h"
#include "ui/compositor/paint_context.h"
#include "ui/compositor/property_change_reason.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Stands in for a view as its layer's delegate, to count the paints of the
// whole layer. A child without a layer of its own (a label that takes the
// new colours) repaints only its own part of it, which doesn't count: the
// view's own background there would stay as it was.
class WholeLayerPaintCounter : public ui::LayerDelegate {
 public:
  WholeLayerPaintCounter(ui::Layer* layer, ui::LayerDelegate* view)
      : layer_(layer), view_(view) {}

  int paints() const { return paints_; }
  void reset() { paints_ = 0; }

  // ui::LayerDelegate:
  void OnPaintLayer(const ui::PaintContext& context) override {
    if (context.InvalidationForTesting().Contains(
            gfx::Rect(layer_->bounds().size()))) {
      ++paints_;
    }
    view_->OnPaintLayer(context);
  }
  void OnDeviceScaleFactorChanged(float old_device_scale_factor,
                                  float new_device_scale_factor) override {
    view_->OnDeviceScaleFactorChanged(old_device_scale_factor,
                                      new_device_scale_factor);
  }
  void OnLayerTransformed(const gfx::Transform& old_transform,
                          ui::PropertyChangeReason reason) override {
    view_->OnLayerTransformed(old_transform, reason);
  }
  void OnLayerClipRectChanged(const gfx::Rect& old_rect,
                              ui::PropertyChangeReason reason) override {
    view_->OnLayerClipRectChanged(old_rect, reason);
  }
  void UpdateVisualState() override { view_->UpdateVisualState(); }

 private:
  const raw_ptr<ui::Layer> layer_;
  const raw_ptr<ui::LayerDelegate> view_;
  int paints_ = 0;
};

// Runs the loop for `delay`.
void RunFor(base::TimeDelta delay) {
  base::RunLoop loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), delay);
  loop.Run();
}

class KoanThemeRepaintBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  void SetColorScheme(ThemeService::BrowserColorScheme scheme) {
    ThemeServiceFactory::GetForProfile(browser()->GetProfile())
        ->SetBrowserColorScheme(scheme);
  }

  // With the window in dark mode and `view` on screen: once nothing paints
  // the whole of `view`'s layer any more (an opening animation is over), a
  // switch to light mode repaints all of it.
  void ExpectRepaintOnModeChange(views::View* view) {
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->layer());
    views::Widget* widget = view->GetWidget();
    ASSERT_TRUE(widget);
    ASSERT_EQ(widget->GetColorProviderKeyForTesting().color_mode,
              ui::ColorProviderKey::ColorMode::kDark);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return view->IsDrawn() && !view->layer()->bounds().IsEmpty(); }));

    ui::Layer* layer = view->layer();
    ui::LayerDelegate* view_delegate = layer->delegate();
    WholeLayerPaintCounter counter(layer, view_delegate);
    layer->set_delegate(&counter);
    const base::ScopedClosureRunner restore(base::BindOnce(
        [](ui::Layer* layer, ui::LayerDelegate* view_delegate) {
          layer->set_delegate(view_delegate);
        },
        base::Unretained(layer), base::Unretained(view_delegate)));

    // Quiet: a while with no paint of the whole layer.
    bool quiet = false;
    for (int i = 0; i < 40 && !quiet; ++i) {
      counter.reset();
      RunFor(base::Milliseconds(250));
      quiet = counter.paints() == 0;
    }
    ASSERT_TRUE(quiet) << "Something keeps repainting the view";

    SetColorScheme(ThemeService::BrowserColorScheme::kLight);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return widget->GetColorProviderKeyForTesting().color_mode ==
             ui::ColorProviderKey::ColorMode::kLight;
    }));
    EXPECT_TRUE(base::test::RunUntil([&] { return counter.paints() > 0; }));
  }
};

// The Library's panel, its background and side column (#564).
IN_PROC_BROWSER_TEST_F(KoanThemeRepaintBrowserTest,
                       LibraryRepaintsWhenTheModeChanges) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view());
  ASSERT_TRUE(controller);
  KoanLibraryController* library = controller->library();
  ASSERT_TRUE(library);
  ASSERT_TRUE(library->IsAvailable());
  SetColorScheme(ThemeService::BrowserColorScheme::kDark);
  base::RunLoop().RunUntilIdle();

  library->Open();
  ASSERT_TRUE(library->is_open());
  ExpectRepaintOnModeChange(library->view());
}

// The sidebar notification's card, as the updater shows it (#564).
IN_PROC_BROWSER_TEST_F(KoanThemeRepaintBrowserTest,
                       SidebarNotificationRepaintsWhenTheModeChanges) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(spaces->space_bar());
  SetColorScheme(ThemeService::BrowserColorScheme::kDark);
  base::RunLoop().RunUntilIdle();

  SidebarNotificationParams params;
  params.heading = u"Koan has been updated";
  params.delay = base::TimeDelta();
  ASSERT_TRUE(ShowSidebarNotification(browser_view(), std::move(params)));
  views::View* notification = nullptr;
  for (views::View* child : spaces->space_bar()->parent()->children()) {
    if (std::string_view(child->GetClassName()) == "KoanSidebarNotification") {
      notification = child;
    }
  }
  ExpectRepaintOnModeChange(notification);
}

}  // namespace

}  // namespace koan
