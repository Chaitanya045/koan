// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpacesSwipe.mjs (the success threshold, the velocity weight and the
//   resistance of the Space dragged by a swipe)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_

#include "base/memory/raw_ptr.h"
#include "ui/events/event.h"

namespace koan {

class KoanWorkspaceController;

// Zen's two-finger swipe between Spaces (ZenSpacesSwipe): the Space follows
// the fingers across the sidebar, and lifting them switches Space or lets it
// spring back (KoanWorkspaceController::UpdateSwipe(), EndSwipe()).

// How far the Space is drawn from rest for fingers moved `offset` pixels
// (positive: to the right) across a sidebar `width` wide: it drags harder
// the further it is pulled (Zen's force multiplier, 1 - |x| / (4.5 * width)),
// and stops at the sidebar's edge.
float ResistedSwipeOffset(float offset, float width);

// Where a swipe that ended at `offset` (as drawn) with the fingers moving at
// `velocity` pixels per second counts as going to: the offset and the
// distance the speed carries it.
float ProjectedSwipeOffset(float offset, float velocity);

// Whether a swipe that is going to `projected` (as above) switches Space: a
// quarter of the sidebar's width (Zen's SUCCESS_THRESHOLD).
bool SwipeSucceeds(float projected, float width);

// The state of one swipe gesture, from the scroll events macOS sends. Those
// carry no scroll phase (ui::ScrollEvent leaves it at kNone, see the TODO in
// ui/events/event.cc); macOS puts the stream's phases in the momentum phase
// instead (ui/events/cocoa/events_mac.mm), which is what
// KoanSpaceSwipe translates into SwipePhase:
//
//   fingers down  MAY_BEGIN        kBegin
//   fingers move  NONE             kUpdate
//   fingers up    END              kEnd       (the system's momentum follows)
//   momentum      INERTIAL_UPDATE  kMomentum
//   momentum over END              kEnd
//
// Plain C++, so that it can be tested without a window.
enum class SwipePhase { kBegin, kUpdate, kMomentum, kEnd };

class KoanSwipeTracker {
 public:
  class Delegate {
   public:
    // The fingers moved `offset` pixels from where they went down.
    virtual void UpdateSwipe(float offset) = 0;
    // The fingers lifted at `velocity` pixels per second along x.
    virtual void EndSwipe(float velocity) = 0;

   protected:
    ~Delegate() = default;
  };

  explicit KoanSwipeTracker(Delegate* delegate);
  KoanSwipeTracker(const KoanSwipeTracker&) = delete;
  KoanSwipeTracker& operator=(const KoanSwipeTracker&) = delete;
  ~KoanSwipeTracker();

  // Takes a scroll event over the sidebar: `dx` and `dy` pixels (positive:
  // content moving right and down) at `seconds` on any steady clock. Returns
  // whether it belongs to a horizontal swipe, which the caller then handles:
  // nothing else sees it, like upstream's own swipe, and the tab list
  // underneath doesn't get the momentum that follows. Over the page there is
  // no such tracker: the page keeps Chromium's swipe back and forward.
  bool OnScroll(float dx, float dy, SwipePhase phase, double seconds);

 private:
  enum class State {
    // Between gestures.
    kIdle,
    // A gesture that hasn't gone far enough to tell which way it goes.
    kUndecided,
    // A horizontal swipe, following the fingers.
    kSwiping,
    // The swipe ended (fingers lifted); the momentum that follows is its.
    kEnded,
    // Anything else (scrolling the tabs): not ours.
    kPassthrough,
  };

  bool Move(float dx, float dy, double seconds);
  void Reset();

  const raw_ptr<Delegate> delegate_;
  State state_ = State::kIdle;
  float total_x_ = 0;
  float total_y_ = 0;
  // Pixels per second along x, smoothed over the last events.
  float velocity_x_ = 0;
  double last_move_seconds_ = 0;
  bool has_moved_ = false;
  // The system's momentum has begun (so the next end is its end).
  bool in_momentum_ = false;
};

// KoanWorkspaceController's side of it: translates ui::ScrollEvents.
class KoanSpaceSwipe : public KoanSwipeTracker::Delegate {
 public:
  explicit KoanSpaceSwipe(KoanWorkspaceController* controller);
  KoanSpaceSwipe(const KoanSpaceSwipe&) = delete;
  KoanSpaceSwipe& operator=(const KoanSpaceSwipe&) = delete;
  ~KoanSpaceSwipe();

  // Whether `event`, a touchpad scroll over the sidebar, belongs to a
  // horizontal swipe (see KoanSwipeTracker::OnScroll()). Mouse wheels aren't
  // swipes. A swipe that starts where the controller can't follow it (one
  // Space, another Space still sliding in) is still taken: left to upstream's
  // swipe controller it would switch Space, twice.
  bool OnScrollEvent(const ui::ScrollEvent& event);

  // KoanSwipeTracker::Delegate:
  void UpdateSwipe(float offset) override;
  void EndSwipe(float velocity) override;

 private:
  const raw_ptr<KoanWorkspaceController> controller_;
  KoanSwipeTracker tracker_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
