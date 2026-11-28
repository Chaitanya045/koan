// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_

class BrowserView;

namespace gfx {
class Point;
}  // namespace gfx

namespace koan {

// Called at the end of the BrowserView constructor, once all child views
// exist. The widget is not attached yet.
void OnBrowserViewCreated(BrowserView* browser_view);

// Called first thing in the BrowserView destructor, while child views and
// the Browser are still alive. Koan has usually torn the window down already,
// when its widget was destroyed (see KoanBrowserViewController).
void OnBrowserViewDestroying(BrowserView* browser_view);

// Called when a drag of the vertical tab strip's resize area collapsed an
// expanded strip. Like Zen, Koan hides the sidebar in compact mode instead,
// with a one-time toast telling how to bring it back.
void OnSidebarDragCollapsed(BrowserView* browser_view);

// Whether Koan plays Zen's download animation in this window; Chrome's own
// "download started" animation is skipped then (it would fly from the
// toolbar's downloads button, which the single toolbar hides).
bool ShowsDownloadAnimation(BrowserView* browser_view);

// Called first in BrowserView::NonClientHitTest(), with its `point` (in the
// BrowserView's parent's coordinates): whether a Koan view covering the
// window is there (the first-run welcome, KoanWelcome), so that the point is
// client area. Otherwise the tab strip's and toolbar's empty space under it
// would be the window's caption, and on macOS a caption hit wins over the
// views under the point (NativeWidgetMacNSWindowHost::GetHitTestResult()):
// AppKit would drag the window instead of the page getting the click.
bool IsWindowCoveredAt(BrowserView* browser_view, const gfx::Point& point);

// Called in BrowserView::NonClientHitTest() with GetClientAreaTop(), the
// toolbar's top in the BrowserView: the top of what the hit test takes as
// client area. Never above the BrowserView: where Koan slides the toolbar out
// of the window (single toolbar, compact mode), the toolbar's top is negative,
// and the client area would cover the frame's resize border above the
// BrowserView, so the window's top edge wouldn't resize (#574).
int ClientAreaTopForHitTest(int client_area_top);

// Called in BrowserView::NonClientHitTest() after IsWindowCoveredAt(), with
// the same `point`: whether it is the window's caption, so that dragging there
// moves the window (and a double click maximizes it): the empty part of a
// quick window's top row and its read-only address, not the security chip or
// a page action (KoanQuickWindowController::IsCaptionAt()). Upstream makes
// everything below a popup's title bar client area, and a quick window has no
// title bar.
bool IsWindowCaptionAt(BrowserView* browser_view, const gfx::Point& point);

// Whether Koan draws the window's whole top row itself, so that the frame
// draws none of its own: a quick window (koan_quick_window_controller.h),
// whose one row holds the site, Move to Space and Close. Hooks:
// - BrowserView::ShouldShowWindowTitle(): no title (on Linux the frame's
//   title bar with the icon and title, on macOS AppKit's title text);
// - OpaqueBrowserFrameView::ShouldShowCaptionButtons() (Linux): none of the
//   frame's caption buttons, Koan's Close being the window's one close; the
//   frame keeps its border and so its resize edges;
// - BrowserNativeWidgetMac::PopulateCreateWindowParams(): a transparent title
//   bar over the full-size content, as a normal window's, so that the traffic
//   lights sit at the start of the row (GetTrafficLightsTitlebarHeight()
//   centres them in it).
bool OwnsWindowTopRow(const BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_
