// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpacesSwipe.mjs (the values the tests check)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// Unit tests for the swipe gesture's state machine and Zen's formulas, to run
// in koan_unittests (#87) once it exists. The sequences are the ones macOS
// sends: see SwipePhase in koan_swipe_tracker.h.

#include "koan/browser/ui/views/koan_swipe_tracker.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

class RecordingDelegate : public KoanSwipeTracker::Delegate {
 public:
  void UpdateSwipe(float amount) override { amounts.push_back(amount); }
  void EndSwipe(float amount, float velocity) override {
    ++ends;
    end_amount = amount;
    end_velocity = velocity;
  }

  std::vector<float> amounts;
  int ends = 0;
  float end_amount = 0;
  float end_velocity = 0;
};

// Fingers going down, then moving by `dx` points (negative: left) every 16 ms.
void Drag(KoanSwipeTracker& tracker, float dx, int steps, double& now) {
  EXPECT_FALSE(tracker.OnScroll(0, 0, SwipePhase::kBegin, now));
  for (int i = 0; i < steps; ++i) {
    now += 0.016;
    tracker.OnScroll(dx, 0, SwipePhase::kUpdate, now);
  }
}

}  // namespace

TEST(KoanSwipeTrackerTest, HorizontalSwipeFollowsTheFingersAndEnds) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  Drag(tracker, -6, 5, now);
  // The first events are still short of the lock distance: not taken.
  ASSERT_FALSE(delegate.amounts.empty());
  // 30 points to the left of 550 per amount: positive.
  EXPECT_FLOAT_EQ(30 / 550.f, delegate.amounts.back());

  now += 0.016;
  EXPECT_TRUE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now));
  EXPECT_EQ(1, delegate.ends);
  EXPECT_FLOAT_EQ(30 / 550.f, delegate.end_amount);
  // The last movement: 6 points in 16 ms, to the left.
  EXPECT_FLOAT_EQ(6 / 550.f / 0.016f, delegate.end_velocity);
}

TEST(KoanSwipeTrackerTest, TheVelocityHasAnEightMillisecondFloor) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  EXPECT_FALSE(tracker.OnScroll(0, 0, SwipePhase::kBegin, now));
  tracker.OnScroll(-12, 0, SwipePhase::kUpdate, now + 0.002);
  tracker.OnScroll(-11, 0, SwipePhase::kUpdate, now + 0.004);
  tracker.OnScroll(0, 0, SwipePhase::kEnd, now + 0.2);
  // Gecko: max(0.008, elapsed); and fingers resting before the lift don't
  // zero it (its end events don't update the velocity).
  EXPECT_FLOAT_EQ(11 / 550.f / 0.008f, delegate.end_velocity);
}

TEST(KoanSwipeTrackerTest, TheFirstEventOfAGestureKeepsItsMovement) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  EXPECT_TRUE(tracker.OnScroll(-12, 1, SwipePhase::kBegin, 1));
  ASSERT_EQ(1u, delegate.amounts.size());
  EXPECT_FLOAT_EQ(12 / 550.f, delegate.amounts[0]);
}

TEST(KoanSwipeTrackerTest, MomentumAfterTheLiftIsTakenUntilItsEnd) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  Drag(tracker, -8, 4, now);
  EXPECT_TRUE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now + 0.016));
  EXPECT_TRUE(tracker.OnScroll(-3, 0, SwipePhase::kMomentum, now + 0.032));
  EXPECT_TRUE(tracker.OnScroll(-1, 0, SwipePhase::kMomentum, now + 0.048));
  // The end of the momentum is the swipe's too, once.
  EXPECT_TRUE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now + 0.064));
  EXPECT_EQ(1, delegate.ends);
  // And what follows isn't.
  EXPECT_FALSE(tracker.OnScroll(-3, 0, SwipePhase::kMomentum, now + 0.5));
}

TEST(KoanSwipeTrackerTest, VerticalScrollingIsLeftAlone) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  EXPECT_FALSE(tracker.OnScroll(1, -4, SwipePhase::kBegin, now));
  for (int i = 0; i < 6; ++i) {
    now += 0.016;
    EXPECT_FALSE(tracker.OnScroll(2, -6, SwipePhase::kUpdate, now));
  }
  EXPECT_FALSE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now + 0.016));
  EXPECT_FALSE(tracker.OnScroll(0, -3, SwipePhase::kMomentum, now + 0.032));
  EXPECT_TRUE(delegate.amounts.empty());
  EXPECT_EQ(0, delegate.ends);
}

TEST(KoanSwipeTrackerTest, ANewGestureAfterAMissedEndLetsGoOfTheOldOne) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  Drag(tracker, -10, 4, now);
  Drag(tracker, 10, 4, now);
  EXPECT_EQ(1, delegate.ends);
}

TEST(KoanSwipeTrackerTest, AnUpdateWithoutABeginStartsAGesture) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  EXPECT_TRUE(tracker.OnScroll(-14, 0, SwipePhase::kUpdate, 1));
  EXPECT_EQ(1u, delegate.amounts.size());
}

TEST(KoanSwipeTest, TheAmountIsTravelOverGeckosPixelSize) {
  EXPECT_FLOAT_EQ(1.f, SwipeGestureAmount(550));
  EXPECT_FLOAT_EQ(-0.5f, SwipeGestureAmount(-275));
}

TEST(KoanSwipeTest, TheSwipeSucceedsAtAQuarterOfAnAmount) {
  // 0.25 of 550 points = 137.5 points of travel, at rest.
  EXPECT_FALSE(SwipeSucceeds(SwipeGestureAmount(137), 0));
  EXPECT_TRUE(SwipeSucceeds(SwipeGestureAmount(138), 0));
  // The same to the other side (fingers moving right).
  EXPECT_FALSE(SwipeSucceeds(SwipeGestureAmount(-137), 0));
  EXPECT_TRUE(SwipeSucceeds(SwipeGestureAmount(-138), 0));
}

TEST(KoanSwipeTest, TheLastVelocityCountsHalfInTheDirectionOfTheSwipe) {
  // 100 points of travel plus a speed of 0.3 amounts per second: 0.1818 +
  // 0.3 * 0.5 >= 0.25.
  EXPECT_TRUE(SwipeSucceeds(SwipeGestureAmount(100), 0.3f));
  EXPECT_TRUE(SwipeSucceeds(SwipeGestureAmount(-100), -0.3f));
  // Moving back against it takes away.
  EXPECT_FALSE(SwipeSucceeds(SwipeGestureAmount(138), -0.1f));
  EXPECT_FALSE(SwipeSucceeds(SwipeGestureAmount(-138), 0.1f));
}

TEST(KoanSwipeTest, TranslateIsZensForceMultiplierRecurrence) {
  SwipeTranslateState state;
  // 230 wide: amount 0.1 -> delta 10; translate = 10 * (1 - 10/1035).
  const float first = SwipeTranslate(0.1f, 230, state);
  EXPECT_NEAR(10 * (1 - 10 / 1035.f), first, 1e-4);
  // lastDelta = delta + (translate - delta) / 2.
  EXPECT_NEAR(10 + (first - 10) / 2, state.last_delta, 1e-4);
}

TEST(KoanSwipeTest, TranslateHoldsWhereItWasPastTheForceFloor) {
  SwipeTranslateState state;
  state.last_delta = 40;
  // A translate of 1000 on a 230 wide strip: force = 1 - 1000 / 1035 < 0.5.
  EXPECT_FLOAT_EQ(40, SwipeTranslate(10.f, 230, state));
  EXPECT_FLOAT_EQ(40, state.last_delta);
}

TEST(KoanSwipeTest, TheOffsetIsHalfAPercentOfTheWidthPerUnit) {
  // translateX(offset / 2 %): a translate of 50 is 25% of the width.
  EXPECT_FLOAT_EQ(57.5f, SwipeOffsetPixels(50, 230));
  EXPECT_FLOAT_EQ(-57.5f, SwipeOffsetPixels(-50, 230));
}

// An abandoned swipe: the end of the gesture never reached the sidebar.

TEST(KoanSwipeTrackerTest, CancelForgetsTheGestureWithoutTellingTheDelegate) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  Drag(tracker, -10, 4, now);
  tracker.Cancel();
  EXPECT_EQ(0, delegate.ends);
  // The rest of the gesture is nobody's: no momentum is taken...
  EXPECT_FALSE(tracker.OnScroll(-3, 0, SwipePhase::kMomentum, now + 0.1));
  // ...and fingers that go on start a new gesture from nothing.
  delegate.amounts.clear();
  EXPECT_TRUE(tracker.OnScroll(-14, 0, SwipePhase::kUpdate, now + 0.2));
  ASSERT_EQ(1u, delegate.amounts.size());
  EXPECT_FLOAT_EQ(14 / 550.f, delegate.amounts[0]);
}

TEST(KoanSwipeTrackerTest, ALateEndAfterCancelIsNoSwipe) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  Drag(tracker, -10, 4, now);
  tracker.Cancel();
  EXPECT_FALSE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now + 5));
  EXPECT_EQ(0, delegate.ends);
}

TEST(SwipeDragTest, FollowsTheAmountAndSlidesOnFromWhereItWas) {
  SwipeDrag drag;
  EXPECT_FALSE(drag.live());
  const float offset = drag.Update(-0.1f, 230);
  EXPECT_TRUE(drag.live());
  EXPECT_LT(offset, 0);
  EXPECT_FLOAT_EQ(offset, drag.offset());
  // Fingers up: the drag is over, and the slide starts from where it was, once.
  EXPECT_FLOAT_EQ(offset, drag.Release());
  EXPECT_FALSE(drag.live());
  EXPECT_FLOAT_EQ(offset, drag.TakeSlideOffset());
  EXPECT_FLOAT_EQ(0, drag.TakeSlideOffset());
}

TEST(SwipeDragTest, ASwitchWhileTheDragGoesOnSlidesFromTheDrag) {
  SwipeDrag drag;
  const float offset = drag.Update(0.08f, 230);
  // (A shortcut or a click switches Space under the fingers.)
  EXPECT_FLOAT_EQ(offset, drag.TakeSlideOffset());
  EXPECT_FALSE(drag.live());
}

TEST(SwipeDragTest, AnAbandonedDragLeavesNothingForTheNextSwitch) {
  SwipeDrag drag;
  drag.Update(-0.2f, 230);
  drag.Reset();
  EXPECT_FALSE(drag.live());
  EXPECT_FLOAT_EQ(0, drag.offset());
  // The next switch of Space, whatever starts it, doesn't slide in from a
  // stale offset.
  EXPECT_FLOAT_EQ(0, drag.TakeSlideOffset());
}

TEST(SwipeDragTest, AReleasedSwipeThatWasNotUsedIsForgottenWithReset) {
  SwipeDrag drag;
  drag.Update(-0.2f, 230);
  drag.Release();
  // The swipe didn't switch Space and the controller let go of it.
  drag.Reset();
  EXPECT_FLOAT_EQ(0, drag.TakeSlideOffset());
}

TEST(SwipeDragTest, ANewDragStartsFromRest) {
  SwipeDrag drag;
  drag.Update(-0.2f, 230);
  drag.Release();
  drag.Reset();
  const float first = drag.Update(-0.1f, 230);
  SwipeDrag fresh;
  EXPECT_FLOAT_EQ(fresh.Update(-0.1f, 230), first);
}

}  // namespace koan
