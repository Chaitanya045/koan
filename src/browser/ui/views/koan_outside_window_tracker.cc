// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_outside_window_tracker.h"

#include <utility>

#include "base/logging.h"
#include "base/time/time.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// How often the pointer's place is looked at while it is tracked: about
// three frames, so a pointer leaving the tolerance hides the sidebar about as
// soon as Zen's (which hears every move). Koan's own.
constexpr base::TimeDelta kPollInterval = base::Milliseconds(50);

// The window, if it is there and active.
views::Widget* GetActiveWidget(views::View* view) {
  views::Widget* widget = view->GetWidget();
  return widget && !widget->IsClosed() && widget->IsActive() ? widget : nullptr;
}

}  // namespace

KoanOutsideWindowTracker::KoanOutsideWindowTracker(views::View* view,
                                                   LeftCallback on_left,
                                                   EndedCallback on_ended)
    : view_(view),
      on_left_(std::move(on_left)),
      on_ended_(std::move(on_ended)) {}

KoanOutsideWindowTracker::~KoanOutsideWindowTracker() = default;

void KoanOutsideWindowTracker::Stop() {
  edge_ = WindowEdge::kNone;
  poll_timer_.Stop();
}

void KoanOutsideWindowTracker::OnPointerExited() {
  views::Widget* widget = GetActiveWidget(view_);
  if (!widget) {
    return;
  }
  // Only a real leave: an exit is also reported for AppKit's own views over
  // the window (the traffic lights) with the pointer still on it.
  const gfx::Rect window = widget->GetWindowBoundsInScreen();
  const gfx::Point point = display::Screen::Get()->GetCursorScreenPoint();
  if (window.Contains(point)) {
    return;
  }
  const WindowEdge edge = CrossedWindowEdge(window, point);
  if (edge == WindowEdge::kNone) {
    return;
  }
  const std::optional<int> max_offset = on_left_.Run(edge, point);
  if (!max_offset) {
    return;
  }
  VLOG(1) << "Koan: pointer tracked outside the window (edge "
          << static_cast<int>(edge) << ", " << *max_offset << "pt)";
  edge_ = edge;
  max_offset_ = *max_offset;
  poll_timer_.Start(FROM_HERE, kPollInterval, this,
                    &KoanOutsideWindowTracker::Check);
}

void KoanOutsideWindowTracker::Check() {
  if (!is_tracking()) {
    poll_timer_.Stop();
    return;
  }
  views::Widget* widget = GetActiveWidget(view_);
  const bool active = widget;
  const gfx::Rect window =
      active ? widget->GetWindowBoundsInScreen() : gfx::Rect();
  const gfx::Point point = display::Screen::Get()->GetCursorScreenPoint();
  if (active && IsWithinOutsideTolerance(window, edge_, max_offset_, point) &&
      !window.Contains(point)) {
    return;
  }
  // Back over the window, gone too far, or the window was deactivated (Zen's
  // `deactivate`). (Zen's tracker counts a pointer back over the window as
  // within bounds, and its first move there hides what isn't hovered: the
  // same as ending here.)
  const bool back_in_window = active && window.Contains(point);
  VLOG(1) << "Koan: pointer tracking outside the window ended"
          << (back_in_window ? " (back over it)" : "");
  Stop();
  on_ended_.Run(back_in_window);
}

}  // namespace koan
