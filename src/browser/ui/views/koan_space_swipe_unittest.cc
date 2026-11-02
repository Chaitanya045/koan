// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Unit tests for the swipe gesture's state machine, to run in koan_unittests
// (#87) once it exists. The sequences are the ones macOS sends: see
// SwipePhase in koan_space_swipe.h.

#include "koan/browser/ui/views/koan_space_swipe.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

class RecordingDelegate : public KoanSwipeTracker::Delegate {
 public:
  void UpdateSwipe(float offset) override { offsets.push_back(offset); }
  void EndSwipe(float velocity) override {
    ++ends;
    end_velocity = velocity;
  }

  std::vector<float> offsets;
  int ends = 0;
  float end_velocity = 0;
};

// Fingers going down, moving left by `steps` events of `dx` every 16 ms.
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
  ASSERT_FALSE(delegate.offsets.empty());
  EXPECT_FLOAT_EQ(-30, delegate.offsets.back());

  now += 0.016;
  EXPECT_TRUE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now));
  EXPECT_EQ(1, delegate.ends);
  EXPECT_LT(delegate.end_velocity, 0);
}

TEST(KoanSwipeTrackerTest, TheFirstEventOfAGestureKeepsItsMovement) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  // A gesture that begins with a movement past the lock distance.
  EXPECT_TRUE(tracker.OnScroll(-12, 1, SwipePhase::kBegin, 1));
  ASSERT_EQ(1u, delegate.offsets.size());
  EXPECT_FLOAT_EQ(-12, delegate.offsets[0]);
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
  EXPECT_TRUE(delegate.offsets.empty());
  EXPECT_EQ(0, delegate.ends);
}

TEST(KoanSwipeTrackerTest, FingersThatRestBeforeLiftingEndAtRest) {
  RecordingDelegate delegate;
  KoanSwipeTracker tracker(&delegate);
  double now = 1;
  Drag(tracker, -10, 4, now);
  // They stop; macOS sends nothing, for as long as they stay; then they lift.
  // (A pause mid-drag does not end the swipe.)
  EXPECT_TRUE(tracker.OnScroll(0, 0, SwipePhase::kEnd, now + 2));
  EXPECT_EQ(1, delegate.ends);
  EXPECT_FLOAT_EQ(0, delegate.end_velocity);
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
  EXPECT_EQ(1u, delegate.offsets.size());
}

TEST(KoanSwipeTest, ResistanceGrowsWithDistanceAndStopsAtTheEdge) {
  EXPECT_FLOAT_EQ(0, ResistedSwipeOffset(0, 200));
  EXPECT_NEAR(ResistedSwipeOffset(50, 200), 50 * (1 - 50 / 900.f), 0.001);
  EXPECT_LT(ResistedSwipeOffset(100, 200), 100);
  EXPECT_FLOAT_EQ(200, ResistedSwipeOffset(5000, 200));
  EXPECT_FLOAT_EQ(-200, ResistedSwipeOffset(-5000, 200));
}

TEST(KoanSwipeTest, AQuarterOfTheWidthSwitches) {
  EXPECT_FALSE(SwipeSucceeds(49, 200));
  EXPECT_TRUE(SwipeSucceeds(50, 200));
  EXPECT_TRUE(SwipeSucceeds(-60, 200));
  // Speed carries it on, in either direction.
  EXPECT_TRUE(SwipeSucceeds(ProjectedSwipeOffset(-20, -600), 200));
  EXPECT_FALSE(SwipeSucceeds(ProjectedSwipeOffset(-40, 400), 200));
}

}  // namespace koan
