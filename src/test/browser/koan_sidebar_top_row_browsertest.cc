// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The sidebar's top row and URL pill (single toolbar), through the window's
// own hit test: the back / forward / reload buttons were window caption, so
// on macOS a real click dragged the window and never reached them (UI
// DevTools' synthetic clicks skip the hit test, so the e2e passed). Also
// guards the upstream behaviour the fix relies on:
// VerticalTabStripRegionView::IsPositionInWindowCaption() asking
// koan::IsSidebarClientArea() before its top row's own test.

#include <string>

#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_sidebar_nav_view.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_LINUX)
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "koan/browser/ui/views/koan_app_menu_button.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/menu/menu_runner.h"
#endif

namespace koan {

namespace {

template <typename T>
T* FindView(views::View* root) {
  if (T* view = views::AsViewClass<T>(root)) {
    return view;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindView<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

// `view` and its ancestors, innermost first, with their bounds in the
// widget: what took the event, for the failure message.
std::string Describe(const views::View* view) {
  std::string out;
  for (; view; view = view->parent()) {
    base::StrAppend(
        &out, {out.empty() ? "" : " < ", std::string(view->GetClassName()), " ",
               view->ConvertRectToWidget(view->GetLocalBounds()).ToString()});
  }
  return out.empty() ? "(none)" : out;
}

bool IsOnScreen(const views::View* view) {
  return view && view->IsDrawn() && !view->GetVisibleBounds().IsEmpty();
}

class KoanSidebarTopRowBrowserTest : public InProcessBrowserTest {
 protected:
  views::Widget* widget() {
    return BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  }

  template <typename T>
  T* WaitForView() {
    T* view = nullptr;
    const bool shown = base::test::RunUntil([&] {
      view = FindView<T>(widget()->GetRootView());
      return IsOnScreen(view);
    });
    views::test::RunScheduledLayout(widget());
    return shown ? view : nullptr;
  }

  // The bubble anchored at `anchor`, if one is open.
  views::Widget* BubbleAt(views::View* anchor) {
    for (views::Widget* bubble :
         views::Widget::GetAllOwnedWidgets(widget()->GetNativeView())) {
      views::BubbleDialogDelegate* delegate =
          bubble->widget_delegate()
              ? bubble->widget_delegate()->AsBubbleDialogDelegate()
              : nullptr;
      if (delegate && delegate->GetAnchorView() == anchor &&
          !bubble->IsClosed()) {
        return bubble;
      }
    }
    return nullptr;
  }
};

}  // namespace

// Each of back, forward and reload is client area at its centre, and the
// view a click there goes to is the button. On a page: on the empty tab
// Koan's URL bar floats, and its backdrop rightly takes a click anywhere
// (it closes the bar).
IN_PROC_BROWSER_TEST_F(KoanSidebarTopRowBrowserTest, NavButtonsAreClientArea) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  KoanSidebarNavButtons* row = WaitForView<KoanSidebarNavButtons>();
  ASSERT_TRUE(row);
  // Back, forward and reload; on Linux after Zen's app menu button.
  ASSERT_EQ(row->children().size(), BUILDFLAG(IS_LINUX) ? 4u : 3u);
  for (views::View* button : row->children()) {
    ASSERT_TRUE(IsOnScreen(button));
    gfx::Point centre = button->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(button, &centre);
    // What NativeWidgetMacNSWindowHost::GetHitTestResult() asks first: a
    // caption answer there makes AppKit drag the window.
    EXPECT_EQ(widget()->GetNonClientComponent(centre), HTCLIENT)
        << button->GetViewAccessibility().GetCachedName();
    views::View* target =
        widget()->GetRootView()->GetEventHandlerForPoint(centre);
    EXPECT_TRUE(target && button->Contains(target))
        << button->GetViewAccessibility().GetCachedName() << " at "
        << centre.ToString() << ": " << Describe(button)
        << "; the event went to " << Describe(target);
  }
}

// Zen's Extensions button is in the pill on a page; pressing it opens
// Chromium's extensions menu at it, and pressing it again closes the menu.
IN_PROC_BROWSER_TEST_F(KoanSidebarTopRowBrowserTest, ExtensionsButtonToggles) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  KoanSidebarNavView* nav = WaitForView<KoanSidebarNavView>();
  ASSERT_TRUE(nav);
  views::Button* button = nav->extensions_button();
  ASSERT_TRUE(button);
  ASSERT_TRUE(base::test::RunUntil([&] { return IsOnScreen(button); }));

  views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return !!BubbleAt(button); }));

  views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
  EXPECT_TRUE(base::test::RunUntil([&] { return !BubbleAt(button); }));
}

#if BUILDFLAG(IS_LINUX)
// Linux has no menu bar: Zen's app menu button in the top row opens
// Chromium's app menu, whose Exit shows Koan's Quit key, Ctrl+Q (#488).
IN_PROC_BROWSER_TEST_F(KoanSidebarTopRowBrowserTest, AppMenuButtonOffersExit) {
  KoanAppMenuButton* button = WaitForView<KoanAppMenuButton>();
  ASSERT_TRUE(button);
  ui::Accelerator key;
  ASSERT_TRUE(button->GetAcceleratorForCommandId(IDC_EXIT, &key));
  EXPECT_EQ(key, ui::Accelerator(ui::VKEY_Q, ui::EF_CONTROL_DOWN));

  button->ShowMenuWithFlags(views::MenuRunner::NO_FLAGS);
  ASSERT_TRUE(base::test::RunUntil([&] { return button->IsMenuShowing(); }));
  // (The block-style menu behind kAppMenuGlowUp has no AppMenuModel.)
  if (AppMenuModel* model = button->app_menu_model()) {
    EXPECT_TRUE(model->GetIndexOfCommandId(IDC_EXIT).has_value());
  }
  button->CloseMenu();
  EXPECT_TRUE(base::test::RunUntil([&] { return !button->IsMenuShowing(); }));
}
#endif  // BUILDFLAG(IS_LINUX)

}  // namespace koan
