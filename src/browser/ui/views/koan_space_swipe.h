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

// Zen's two-finger swipe between Spaces (ZenSpacesSwipe.mjs), with Gecko's
// swipe tracker (widget/SwipeTracker.cpp, patched by Zen) that feeds it. All
// numbers are theirs.
//
// The tracker turns the fingers' travel into a gesture amount: -x / 550 on
// macOS (widget.swipe.pixel-size, in points), so 550pt of travel is 1; moving
// the fingers left makes it positive, right negative. The Space is shown
// translated by a function of that amount (SwipeTranslate()), and when the
// fingers lift the swipe succeeds (SwipeSucceeds()) if the amount plus the
// last event's velocity (amounts per second) times Zen's contribution reaches
// Zen's threshold. A negative amount (fingers moving right) goes to the next
// Space, a positive one to the previous (ZenSpacesSwipe #handleSwipeEnd(),
// zen.workspaces.natural-scroll off), the strip moving the other way from the
// fingers.

// The gesture amount of `travel` points of finger movement to the left.
float SwipeGestureAmount(float travel_left);

// The state SwipeTranslate() carries between the events of a gesture
// (ZenSpacesSwipe's swipeState.lastDelta).
struct SwipeTranslateState {
  float last_delta = 0;
};

// Zen's translateX for a cumulative gesture amount: `amount` times
// zen.workspaces.swipe-actions.delta-multiplier added to the last delta, made
// to drag harder the closer it gets to `strip_width` * 4.5 (their "force
// multiplier"), and held where it was once that falls to a half. Updates
// `state`.
float SwipeTranslate(float amount,
                     float strip_width,
                     SwipeTranslateState& state);

// How far the Space is drawn from rest, in pixels (positive: to the right),
// for a translateX: Zen translates each Space by translateX / 2 percent of its
// own width.
float SwipeOffsetPixels(float translate, float strip_width);

// Whether a swipe ending at gesture `amount` with the fingers last moving at
// `velocity` (amounts per second) switches Space: Gecko's
// SwipeTracker::ComputeSwipeSuccess() with Zen's threshold (0.25,
// ZenSpacesSwipe.SUCCESS_THRESHOLD) and velocity contribution (0.5,
// SUCCESS_VELOCITY_CONTRIBUTION).
bool SwipeSucceeds(float amount, float velocity);

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
    // The gesture's amount (SwipeGestureAmount() of the travel so far) changed.
    virtual void UpdateSwipe(float amount) = 0;
    // The fingers lifted: the amount, and the velocity of the last movement in
    // amounts per second.
    virtual void EndSwipe(float amount, float velocity) = 0;

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
  // The last movement's velocity, in amounts per second (Gecko's
  // mCurrentVelocity: not smoothed, not reset while the fingers rest).
  float velocity_ = 0;
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
  void UpdateSwipe(float amount) override;
  void EndSwipe(float amount, float velocity) override;

 private:
  const raw_ptr<KoanWorkspaceController> controller_;
  KoanSwipeTracker tracker_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
