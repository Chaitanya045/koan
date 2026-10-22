// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_DRAG_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_DRAG_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"

class BrowserView;

namespace koan {

// Zen's window dragging from the page (zen.view.drag-window-from-content):
// pressing the left button on empty space near the top of the page (the top
// 5% of its height) and moving the pointer moves the window, as a title bar
// would. Links, text, form controls, media, frames and anything else the
// page makes interactive keep their own behaviour; so does a plain click.
//
// Watches the mouse events Chromium forwards to the active tab's page.
// Whether the pressed point is empty space is decided in the page (an
// isolated world, so pages can't interfere) while the button is held; the
// move then works like a title bar drag: on macOS the window follows the
// pointer (Widget::RunMoveLoop), on Linux the window manager moves it. It
// only looks at the page, so the sidebar's side, compact mode and the
// window buttons' hot zone above the page don't matter.
class KoanWindowDragController : public TabStripModelObserver,
                                 public content::WebContentsObserver,
                                 public content::RenderWidgetHostObserver {
 public:
  // `on_page_press` gets every left-button press on the page, in screen
  // coordinates.
  KoanWindowDragController(
      BrowserView* browser_view,
      base::RepeatingCallback<void(const gfx::Point&)> on_page_press);
  KoanWindowDragController(const KoanWindowDragController&) = delete;
  KoanWindowDragController& operator=(const KoanWindowDragController&) = delete;
  ~KoanWindowDragController() override;

  // Called by KoanBrowserViewController.
  void OnBrowserViewDestroying();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;

  // content::RenderWidgetHostObserver:
  void RenderWidgetHostDestroyed(
      content::RenderWidgetHost* widget_host) override;

 private:
  enum class State {
    kIdle,
    // Pressed in the drag region; waiting for the page to say what's there.
    kPending,
    // Pressed on empty space: moving past the threshold starts the drag.
    kArmed,
    // The window manager moves the window; the rest of the gesture isn't
    // the page's.
    kDragging,
  };

  // Watches the active tab's page.
  void ObserveActiveTab();
  void WatchWidget(content::RenderWidgetHost* host);

  // Runs for every mouse event forwarded to the page; returns true to keep
  // it from the page.
  bool OnMouseEvent(const blink::WebMouseEvent& event);
  void OnPressedPointChecked(int gesture, base::Value draggable);
  void StartDrag(const blink::WebMouseEvent& event);

  const raw_ptr<BrowserView> browser_view_;
  const base::RepeatingCallback<void(const gfx::Point&)> on_page_press_;
  raw_ptr<content::RenderWidgetHost> widget_host_ = nullptr;
  const content::RenderWidgetHost::MouseEventCallback mouse_event_callback_;
  base::ScopedObservation<content::RenderWidgetHost,
                          content::RenderWidgetHostObserver>
      widget_host_observation_{this};

  State state_ = State::kIdle;
  // Identifies the current press, so a late answer for an earlier one is
  // ignored.
  int gesture_ = 0;
  gfx::PointF press_in_screen_;

  base::WeakPtrFactory<KoanWindowDragController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_DRAG_H_
