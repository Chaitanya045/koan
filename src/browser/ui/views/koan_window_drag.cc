// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_drag.h"

#include <cmath>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_zen_window_drag.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/geometry/vector2d_conversions.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_LINUX)
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/hit_test.h"
#include "ui/platform_window/wm/wm_move_resize_handler.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_platform.h"
#endif

namespace koan {

KoanWindowDragController::KoanWindowDragController(
    BrowserView* browser_view,
    base::RepeatingCallback<void(const gfx::Point&)> on_page_press)
    : browser_view_(browser_view),
      on_page_press_(std::move(on_page_press)),
      mouse_event_callback_(
          base::BindRepeating(&KoanWindowDragController::OnMouseEvent,
                              base::Unretained(this))) {
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  ObserveActiveTab();
}

KoanWindowDragController::~KoanWindowDragController() {
  WatchWidget(nullptr);
}

void KoanWindowDragController::OnBrowserViewDestroying() {
  WatchWidget(nullptr);
  Observe(nullptr);
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
}

void KoanWindowDragController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed() ||
      change.type() == TabStripModelChange::kReplaced) {
    ObserveActiveTab();
  }
}

void KoanWindowDragController::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  // A cross-site navigation swaps the page's widget.
  if (new_host && new_host->IsInPrimaryMainFrame()) {
    WatchWidget(new_host->GetRenderWidgetHost());
  }
}

void KoanWindowDragController::RenderWidgetHostDestroyed(
    content::RenderWidgetHost* widget_host) {
  WatchWidget(nullptr);
}

void KoanWindowDragController::ObserveActiveTab() {
  content::WebContents* contents =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  Observe(contents);
  WatchWidget(contents ? contents->GetPrimaryMainFrame()->GetRenderWidgetHost()
                       : nullptr);
}

#if !BUILDFLAG(IS_MAC)
bool IsWindowSnapped(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  return widget && widget->IsMaximized();
}
#endif

void KoanWindowDragController::WatchWidget(content::RenderWidgetHost* host) {
  if (host == widget_host_) {
    return;
  }
  if (widget_host_) {
    widget_host_->RemoveMouseEventCallback(mouse_event_callback_);
    widget_host_observation_.Reset();
  }
  widget_host_ = host;
  state_ = State::kIdle;
#if BUILDFLAG(IS_MAC)
  native_drag_started_ = false;
  swallow_next_up_ = false;
#endif
  if (widget_host_) {
    widget_host_->AddMouseEventCallback(mouse_event_callback_);
    widget_host_observation_.Observe(widget_host_);
  }
}

bool KoanWindowDragController::OnMouseEvent(const blink::WebMouseEvent& event) {
  using Type = blink::WebInputEvent::Type;
  switch (event.GetType()) {
    case Type::kMouseDown: {
#if BUILDFLAG(IS_MAC)
      // A native drag that ended without a move after its release: the press
      // sets the page's view up again by itself (a release now would end the
      // click the user is making).
      native_drag_started_ = false;
      swallow_next_up_ = false;
#endif
      state_ = State::kIdle;
      content::RenderWidgetHostView* view =
          widget_host_ ? widget_host_->GetView() : nullptr;
      if (view) {
        // Where a download the page starts flies from.
        on_page_press_.Run(view->GetViewBounds().origin() +
                           gfx::ToRoundedVector2d(
                               event.PositionInWidget().OffsetFromOrigin()));
      }
      if (!view || !web_contents() ||
          event.button != blink::WebPointerProperties::Button::kLeft ||
          event.click_count != 1 ||
          (event.GetModifiers() & blink::WebInputEvent::kKeyModifiers) ||
          IsWindowFullscreen(browser_view_) ||
          !features::IsWindowDragEnabled() ||
          !browser_view_->GetProfile()->GetPrefs()->GetBoolean(
              prefs::kDragWindowFromContent)) {
        return false;
      }
      const gfx::PointF point = event.PositionInWidget();
      if (point.y() >
          view->GetViewBounds().height() * kWindowDragRegionFraction) {
        return false;
      }
      state_ = State::kPending;
      press_in_screen_ = event.PositionInScreen();
      // The page lays out in CSS pixels: undo its zoom.
      const double zoom = blink::ZoomLevelToZoomFactor(
          content::HostZoomMap::GetZoomLevel(web_contents()));
      web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
          IsEmptySpaceScript(point.x() / zoom, point.y() / zoom),
          base::BindOnce(&KoanWindowDragController::OnPressedPointChecked,
                         weak_factory_.GetWeakPtr(), ++gesture_),
          ISOLATED_WORLD_ID_CHROME_INTERNAL);
      // The page gets the press either way, like Zen's.
      return false;
    }
    case Type::kMouseMove: {
      if (state_ == State::kIdle) {
        return false;
      }
      if (!(event.GetModifiers() & blink::WebInputEvent::kLeftButtonDown)) {
        // The release happened somewhere we didn't see it (e.g. the window
        // manager took it).
#if BUILDFLAG(IS_MAC)
        if (state_ == State::kDragging && native_drag_started_) {
          // The page's view still has the button down: release it, from a
          // task (we are inside its input routing), and swallow the release
          // that sends the page (the page has its own).
          native_drag_started_ = false;
          swallow_next_up_ = true;
          base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(
                             [](base::WeakPtr<KoanWindowDragController> self) {
                               if (self) {
                                 EndNativeWindowMove(self->browser_view_);
                               }
                             },
                             weak_factory_.GetWeakPtr()));
        }
#endif
        state_ = State::kIdle;
        return false;
      }
      if (state_ == State::kDragging) {
        return true;
      }
      if (state_ != State::kArmed) {
        return false;
      }
      // Un-snapping a maximized or tiled window takes a firmer gesture (Zen's
      // SNAPPED_DRAG_START_THRESHOLD_PX).
      const float threshold = IsWindowSnapped(browser_view_)
                                  ? kWindowDragSnappedThreshold
                                  : kWindowDragThreshold;
      const gfx::Vector2dF moved = event.PositionInScreen() - press_in_screen_;
      if (std::hypot(moved.x(), moved.y()) < threshold) {
        return false;
      }
      state_ = State::kDragging;
      StartDrag(event);
      return true;
    }
    case Type::kMouseUp: {
#if BUILDFLAG(IS_MAC)
      if (swallow_next_up_) {
        swallow_next_up_ = false;
        state_ = State::kIdle;
        return true;
      }
#endif
      // The page already got its release when the drag started.
      const bool dragging = state_ == State::kDragging;
      state_ = State::kIdle;
      return dragging;
    }
    default:
      return false;
  }
}

void KoanWindowDragController::OnPressedPointChecked(int gesture,
                                                     base::Value draggable) {
  if (gesture != gesture_ || state_ != State::kPending) {
    return;
  }
  state_ =
      draggable.is_bool() && draggable.GetBool() ? State::kArmed : State::kIdle;
}

void KoanWindowDragController::StartDrag(const blink::WebMouseEvent& event) {
  VLOG(1) << "Koan: window drag from the page";
  // The window manager takes the pointer, and the page may never see the
  // button go up: release it for the page now (after this event).
  blink::WebMouseEvent release = event;
  release.SetType(blink::WebInputEvent::Type::kMouseUp);
  release.button = blink::WebPointerProperties::Button::kLeft;
  release.click_count = 1;
  release.SetModifiers(event.GetModifiers() &
                       ~blink::WebInputEvent::kLeftButtonDown);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanWindowDragController> self,
                        blink::WebMouseEvent up) {
                       if (self && self->widget_host_) {
                         // Past our own filter, which swallows releases
                         // while dragging.
                         const State state = self->state_;
                         self->state_ = State::kIdle;
                         self->widget_host_->ForwardMouseEvent(up);
                         self->state_ = state;
                       }
                     },
                     weak_factory_.GetWeakPtr(), release));

  views::Widget* widget = browser_view_->GetWidget();
  if (!widget) {
    return;
  }
#if BUILDFLAG(IS_LINUX)
  // Then do what a press on the title bar does (WindowEventFilterLinux): the
  // window manager moves the window.
  content::RenderWidgetHostView* view = widget_host_->GetView();
  aura::WindowTreeHost* host = widget->GetNativeWindow()->GetHost();
  auto* platform_host = views::DesktopWindowTreeHostPlatform::GetHostForWidget(
      host->GetAcceleratedWidget());
  ui::WmMoveResizeHandler* handler =
      platform_host && platform_host->platform_window()
          ? ui::GetWmMoveResizeHandler(*platform_host->platform_window())
          : nullptr;
  // Headless (the e2e tests) has no window manager.
  if (!handler || !view) {
    return;
  }
  aura::Window* page_window = view->GetNativeView();
  gfx::Point point = gfx::ToRoundedPoint(event.PositionInWidget());
  aura::Window::ConvertPointToTarget(page_window, page_window->GetRootWindow(),
                                     &point);
  host->ConvertDIPToScreenInPixels(&point);
  handler->DispatchHostWindowDragMovement(HTCAPTION, point);
  aura::Env::GetInstance()->set_mouse_button_flags(0);
#else
  // macOS: AppKit moves the window while the button is down (as Zen does,
  // Zen's StartNativeWindowMove), so tiling at the screen's edges and the
  // drag to another Space work. Not Widget::RunMoveLoop: its release CHECKs
  // (no capture, no other move loop) crash if a menu, bubble or popup took
  // the capture between this event and the loop, or a second drag started.
  // After this event; if the window or the button is gone by then, nothing.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanWindowDragController> self) {
                       if (!self || !self->browser_view_->GetWidget()) {
                         return;
                       }
                       // Where it can't start (the button is up already, or
                       // a PWA's invisible window), the gesture stays ours
                       // all the same, as in Zen: the page has had its
                       // release, so it gets no more of it (nothing was
                       // handed to AppKit, so no window sticks to the
                       // pointer). Synthetic input (the e2e tests) never
                       // holds the physical button.
                       self->native_drag_started_ = true;
                       const bool started =
                           StartNativeWindowMove(self->browser_view_);
                       // (If AppKit runs the drag in a nested loop, the
                       // window can close inside it, and this is gone.)
                       if (self) {
                         self->native_drag_started_ = started;
                       }
                       // (AppKit takes the rest of the gesture, release
                       // included: it ends with the next move that has no
                       // button down, or the next press.)
                     },
                     weak_factory_.GetWeakPtr()));
#endif
}

}  // namespace koan
