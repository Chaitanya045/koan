// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_OUTSIDE_WINDOW_TRACKER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_OUTSIDE_WINDOW_TRACKER_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "koan/browser/ui/views/koan_window_edges.h"

namespace gfx {
class Point;
}  // namespace gfx

namespace views {
class View;
}  // namespace views

namespace koan {

// Zen's native mouse tracker for compact mode (ZenCompactMode.mjs
// _trackMouseOutsideWindow(), ZenMouseTracker): when the pointer leaves the
// window through the edge of the hidden sidebar or toolbar, what is there
// stays revealed (or is revealed, for a pointer flung past the edge) while the
// pointer stays within a few hundred points of the window on that side. It
// hides once the pointer goes farther, comes back over the window (then the
// hover inside the window decides) or the window is no longer active.
//
// Zen gets the pointer's moves outside the window from NSEvent's global and
// local monitors. Here the pointer's place is looked at a few times a second
// while it is tracked (display::Screen, the same on every platform), which is
// only from such a leave until it ends. The window's leaves come from its
// owner's pointer monitor (OnPointerExited()), so the owner hears of a leave
// that is tracked before it decides anything else about it.
class KoanOutsideWindowTracker {
 public:
  // The pointer left the window through `edge` (screen coordinates at `point`):
  // returns how far outside that edge to track it, after revealing what it
  // left from, or nullopt not to track it.
  using LeftCallback =
      base::RepeatingCallback<std::optional<int>(WindowEdge edge,
                                                 const gfx::Point& point)>;
  // Tracking ended (`back_in_window`: the pointer is over the window again).
  using EndedCallback = base::RepeatingCallback<void(bool back_in_window)>;

  // `view` is a view in the window (the BrowserView); it outlives this.
  KoanOutsideWindowTracker(views::View* view,
                           LeftCallback on_left,
                           EndedCallback on_ended);
  KoanOutsideWindowTracker(const KoanOutsideWindowTracker&) = delete;
  KoanOutsideWindowTracker& operator=(const KoanOutsideWindowTracker&) = delete;
  ~KoanOutsideWindowTracker();

  // The window heard that the pointer left it (a kMouseExited, before the
  // views under the pointer do: what it leaves can be held by then, before the
  // sidebar's own hover collapses it). Starts tracking if `on_left` says so.
  void OnPointerExited();
  // Stops tracking, with no callback.
  void Stop();
  bool is_tracking() const { return edge_ != WindowEdge::kNone; }
  WindowEdge edge() const { return edge_; }

 private:
  void Check();

  const raw_ptr<views::View> view_;
  const LeftCallback on_left_;
  const EndedCallback on_ended_;
  // What is tracked: the edge the pointer left through, and how far past it
  // it may go.
  WindowEdge edge_ = WindowEdge::kNone;
  int max_offset_ = 0;
  base::RepeatingTimer poll_timer_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_OUTSIDE_WINDOW_TRACKER_H_
