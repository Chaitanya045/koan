// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains values from Gecko, Copyright the Mozilla contributors, MPL-2.0:
//   modules/libpref/init/StaticPrefList.yaml (the values the tests check)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// How each platform's touchpad scroll events feed the swipe tracker. The Linux
// sequences are the ones Chromium's Wayland and X11 event sources produce: see
// LinuxSwipePhase() in koan_swipe_input.h.

#include "koan/browser/ui/views/koan_swipe_input.h"

#include <cmath>
#include <vector>

#include "base/time/time.h"
#include "koan/browser/ui/views/koan_swipe_tracker.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/types/event_type.h"

namespace koan {

namespace {

using ui::EventType;

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

// A Linux browser's side of a touchpad: the events, as KoanSpaceSwipe hands
// them to the tracker.
class LinuxTouchpad {
 public:
  explicit LinuxTouchpad(LinuxScrollBackend backend)
      : tracker_(&delegate_, LinuxSwipePixelSize(backend)) {}

  // One event of `type` with offsets `dx`, `dy`, `ms` milliseconds after the
  // last; returns whether the swipe took it.
  bool Send(EventType type, float dx, float dy, double ms) {
    now_ += ms / 1000;
    return tracker_.OnScroll(dx, dy, LinuxSwipePhase(type, lifted_), now_);
  }

  // `steps` scroll events of `dx`, `dy`, `ms` apart; returns how many the swipe
  // took.
  int Move(float dx, float dy, int steps, double ms) {
    int taken = 0;
    for (int i = 0; i < steps; ++i) {
      taken += Send(EventType::kScroll, dx, dy, ms);
    }
    return taken;
  }

  // The lift: Wayland's fling start carries the fling's velocity, X11's none;
  // neither counts for the swipe.
  bool Lift(float vx = 0) {
    return Send(EventType::kScrollFlingStart, vx, 0, 16);
  }

  RecordingDelegate& delegate() { return delegate_; }

 private:
  RecordingDelegate delegate_;
  KoanSwipeTracker tracker_;
  bool lifted_ = true;
  double now_ = 1;
};

// Upstream's wheel switch on Linux (VerticalTabStripFocusSwipeController::
// ProcessSwipeOffset(), Chromium 158): wheel offsets add up until one axis
// passes 15; a horizontal gesture switches Space once at 75; 300 ms without a
// wheel event starts over.
class UpstreamWheelSwitch {
 public:
  void OnWheel(float dx, float dy, double seconds) {
    if (seconds - last_ > 0.3) {
      state_ = State::kNone;
      x_ = y_ = 0;
      triggered_ = false;
    }
    last_ = seconds;
    if (state_ == State::kVertical) {
      return;
    }
    x_ += dx;
    y_ += dy;
    if (state_ == State::kNone) {
      if (std::abs(y_) >= 15 && std::abs(y_) > std::abs(x_)) {
        state_ = State::kVertical;
        return;
      }
      if (std::abs(x_) >= 15 && std::abs(x_) > std::abs(y_)) {
        state_ = State::kHorizontal;
      }
    }
    if (state_ == State::kHorizontal && !triggered_ && std::abs(x_) >= 75) {
      triggered_ = true;
      ++switches;
    }
  }

  int switches = 0;

 private:
  enum class State { kNone, kHorizontal, kVertical };
  State state_ = State::kNone;
  float x_ = 0;
  float y_ = 0;
  bool triggered_ = false;
  double last_ = -1;
};

// A Linux browser window's sidebar on Wayland, as the events go through it:
// each touchpad scroll goes to Koan's handler (the swipe), then Views makes
// wheel events of it (one, and a second one if nobody took it: see
// TouchpadWheels), which Koan's handler sees before upstream's wheel switch
// and the tab list. Counts the Space switches either makes.
class LinuxSidebar {
 public:
  // Chromium's offsets per compositor pixel on Wayland (WaylandPointer::OnAxis
  // scales 10 to 120).
  static constexpr float kOffsetsPerPixel = 12;

  LinuxSidebar()
      : tracker_(&delegate_,
                 LinuxSwipePixelSize(LinuxScrollBackend::kWayland)) {}

  // The tester's slow swipe: 1 compositor pixel every 16 ms, `pixels` of them
  // (negative: fingers to the left, towards the next Space), then the lift.
  void SlowSwipe(int pixels) {
    const float step = pixels < 0 ? -kOffsetsPerPixel : kOffsetsPerPixel;
    for (int i = 0; i < std::abs(pixels); ++i) {
      Scroll(EventType::kScroll, step, 0);
    }
    Scroll(EventType::kScrollFlingStart, 0, 0);
  }

  // Vertical finger scrolling of `pixels`, then the lift.
  void VerticalScroll(int pixels) {
    for (int i = 0; i < pixels; ++i) {
      Scroll(EventType::kScroll, 0, kOffsetsPerPixel);
    }
    Scroll(EventType::kScrollFlingStart, 0, 0);
  }

  // A burst of `clicks` mouse wheel clicks to the side, 16 ms apart: Wayland
  // makes each a wheel event of 120.
  void WheelBurst(int clicks) {
    for (int i = 0; i < clicks; ++i) {
      now_ += 0.016;
      Wheel(-120, 0);
    }
  }

  // Space switches: upstream's wheel switch, and swipes that succeeded.
  int switches() const { return upstream_.switches + swipe_switches_; }
  // The wheel events that reached the tab list.
  int tab_list_wheels() const { return tab_list_wheels_; }
  RecordingDelegate& delegate() { return delegate_; }

 private:
  void Scroll(EventType type, float dx, float dy) {
    now_ += 0.016;
    const int ends = delegate_.ends;
    const bool taken =
        tracker_.OnScroll(dx, dy, LinuxSwipePhase(type, lifted_), now_);
    if (delegate_.ends > ends &&
        SwipeSucceeds(delegate_.end_amount, delegate_.end_velocity)) {
      ++swipe_switches_;
    }
    if (type != EventType::kScroll) {
      return;
    }
    wheels_.OnScroll(Time(), taken);
    // DesktopNativeWidgetAura's wheel event, and views::Widget's when the
    // scroll event's copy wasn't handled.
    Wheel(dx, dy);
    if (!taken) {
      Wheel(dx, dy);
    }
  }

  // A wheel event now, through Koan's handler (SpaceWheelHandler), then
  // upstream's wheel switch and the tab list unless Koan stopped it.
  void Wheel(float dx, float dy) {
    if (wheels_.Classify(Time()) == TouchpadWheels::Kind::kTakenScroll) {
      return;
    }
    upstream_.OnWheel(dx, dy, now_);
    ++tab_list_wheels_;
  }

  base::TimeTicks Time() const {
    return base::TimeTicks() + base::Seconds(now_);
  }

  RecordingDelegate delegate_;
  KoanSwipeTracker tracker_;
  TouchpadWheels wheels_;
  UpstreamWheelSwitch upstream_;
  bool lifted_ = true;
  double now_ = 1;
  int swipe_switches_ = 0;
  int tab_list_wheels_ = 0;
};

}  // namespace

TEST(KoanSwipeInputTest, MacPhasesComeFromTheMomentumPhase) {
  using ui::EventMomentumPhase;
  EXPECT_EQ(SwipePhase::kBegin, MacSwipePhase(EventMomentumPhase::MAY_BEGIN));
  EXPECT_EQ(SwipePhase::kBegin, MacSwipePhase(EventMomentumPhase::BEGAN));
  EXPECT_EQ(SwipePhase::kUpdate, MacSwipePhase(EventMomentumPhase::NONE));
  EXPECT_EQ(SwipePhase::kEnd, MacSwipePhase(EventMomentumPhase::END));
  EXPECT_EQ(SwipePhase::kEnd, MacSwipePhase(EventMomentumPhase::BLOCKED));
  EXPECT_EQ(SwipePhase::kMomentum,
            MacSwipePhase(EventMomentumPhase::INERTIAL_UPDATE));
}

TEST(KoanSwipeInputTest, LinuxPhasesFromTheEventTypes) {
  bool lifted = true;
  // Wayland's or X11's first gesture: no fling to cancel, so the first scroll
  // begins it.
  EXPECT_EQ(SwipePhase::kBegin, LinuxSwipePhase(EventType::kScroll, lifted));
  EXPECT_FALSE(lifted);
  EXPECT_EQ(SwipePhase::kUpdate, LinuxSwipePhase(EventType::kScroll, lifted));
  EXPECT_EQ(SwipePhase::kEnd,
            LinuxSwipePhase(EventType::kScrollFlingStart, lifted));
  EXPECT_TRUE(lifted);

  // Wayland's next gesture: the fling the lift started is cancelled first.
  EXPECT_EQ(SwipePhase::kBegin,
            LinuxSwipePhase(EventType::kScrollFlingCancel, lifted));
  EXPECT_FALSE(lifted);
  EXPECT_EQ(SwipePhase::kUpdate, LinuxSwipePhase(EventType::kScroll, lifted));
  EXPECT_EQ(SwipePhase::kEnd,
            LinuxSwipePhase(EventType::kScrollFlingStart, lifted));

  // X11's next gesture: nothing marks it but the lift before it.
  EXPECT_EQ(SwipePhase::kBegin, LinuxSwipePhase(EventType::kScroll, lifted));
  EXPECT_EQ(SwipePhase::kUpdate, LinuxSwipePhase(EventType::kScroll, lifted));
}

TEST(KoanSwipeInputTest, AFingerRestingOnTheTouchpadBeginsOnce) {
  bool lifted = true;
  LinuxSwipePhase(EventType::kScroll, lifted);
  LinuxSwipePhase(EventType::kScrollFlingStart, lifted);
  // Wayland's hold gesture: a finger touching the touchpad stops the fling,
  // and the scroll that follows (with no fling left to cancel) carries on
  // from there, not as another beginning.
  EXPECT_EQ(SwipePhase::kBegin,
            LinuxSwipePhase(EventType::kScrollFlingCancel, lifted));
  EXPECT_EQ(SwipePhase::kUpdate, LinuxSwipePhase(EventType::kScroll, lifted));
}

TEST(KoanSwipeInputTest, LinuxPixelSizeIsGeckosForGtk) {
  // 1100 / 40 = 27.5 GTK scroll units, of 120 offsets on Wayland and 53 on X11.
  EXPECT_FLOAT_EQ(3300.f, LinuxSwipePixelSize(LinuxScrollBackend::kWayland));
  EXPECT_FLOAT_EQ(1457.5f, LinuxSwipePixelSize(LinuxScrollBackend::kX11));
}

TEST(KoanSwipeInputTest, WaylandSlowSwipeFollowsTheFingersAndSwitches) {
  LinuxTouchpad touchpad(LinuxScrollBackend::kWayland);
  // 4 compositor pixels to the left a frame, 48 offsets in Chromium's scale.
  EXPECT_EQ(30, touchpad.Move(-48, 2, 30, 16));
  RecordingDelegate& delegate = touchpad.delegate();
  ASSERT_EQ(30u, delegate.amounts.size());
  // The strip follows from the first event: its 48 offsets are past the lock
  // distance (10, under upstream's 15).
  EXPECT_FLOAT_EQ(-48 / 3300.f, delegate.amounts.front());
  EXPECT_FLOAT_EQ(-1440 / 3300.f, delegate.amounts.back());

  // The lift ends it, and is the swipe's (the tab list gets no fling).
  EXPECT_TRUE(touchpad.Lift(/*vx=*/-3000));
  EXPECT_EQ(1, delegate.ends);
  EXPECT_FLOAT_EQ(-1440 / 3300.f, delegate.end_amount);
  // The last movement's velocity, not the fling's.
  EXPECT_FLOAT_EQ(-48 / 3300.f / 0.016f, delegate.end_velocity);
  EXPECT_TRUE(SwipeSucceeds(delegate.end_amount, delegate.end_velocity));
}

TEST(KoanSwipeInputTest, WaylandShortSlowSwipeSpringsBack) {
  LinuxTouchpad touchpad(LinuxScrollBackend::kWayland);
  touchpad.Move(-10, 0, 30, 16);
  touchpad.Lift();
  RecordingDelegate& delegate = touchpad.delegate();
  ASSERT_EQ(1, delegate.ends);
  EXPECT_FLOAT_EQ(-300 / 3300.f, delegate.end_amount);
  EXPECT_FALSE(SwipeSucceeds(delegate.end_amount, delegate.end_velocity));
}

TEST(KoanSwipeInputTest, WaylandFlickSwitches) {
  LinuxTouchpad touchpad(LinuxScrollBackend::kWayland);
  touchpad.Move(240, 0, 3, 8);
  touchpad.Lift(/*vx=*/20000);
  RecordingDelegate& delegate = touchpad.delegate();
  ASSERT_EQ(1, delegate.ends);
  // A short travel to the right (the previous Space), carried by its speed.
  EXPECT_FLOAT_EQ(720 / 3300.f, delegate.end_amount);
  EXPECT_LT(delegate.end_amount, 0.25f);
  EXPECT_TRUE(SwipeSucceeds(delegate.end_amount, delegate.end_velocity));
}

TEST(KoanSwipeInputTest, WaylandNextGestureBeginsAtTheFlingCancel) {
  LinuxTouchpad touchpad(LinuxScrollBackend::kWayland);
  touchpad.Move(-48, 0, 10, 16);
  touchpad.Lift();
  // Fingers down again: Wayland cancels the fling the lift started.
  EXPECT_FALSE(touchpad.Send(EventType::kScrollFlingCancel, 0, 0, 200));
  EXPECT_EQ(3, touchpad.Move(36, 0, 3, 16));
  RecordingDelegate& delegate = touchpad.delegate();
  EXPECT_EQ(1, delegate.ends);
  // A new swipe, from rest.
  EXPECT_FLOAT_EQ(108 / 3300.f, delegate.amounts.back());
}

TEST(KoanSwipeInputTest, X11SwipesWithoutBeginnings) {
  LinuxTouchpad touchpad(LinuxScrollBackend::kX11);
  EXPECT_EQ(20, touchpad.Move(-25, 0, 20, 16));
  EXPECT_TRUE(touchpad.Lift());
  RecordingDelegate& delegate = touchpad.delegate();
  ASSERT_EQ(1, delegate.ends);
  EXPECT_FLOAT_EQ(-500 / 1457.5f, delegate.end_amount);
  EXPECT_TRUE(SwipeSucceeds(delegate.end_amount, delegate.end_velocity));

  // A vertical scroll of the tabs is left alone, its lift too (the tab list
  // takes the fling).
  EXPECT_EQ(0, touchpad.Move(1, -40, 10, 16));
  EXPECT_FALSE(touchpad.Lift());
  // And the swipe after it is a swipe again, though nothing on X11 says a
  // gesture begins.
  EXPECT_EQ(5, touchpad.Move(30, 0, 5, 16));
  EXPECT_FLOAT_EQ(150 / 1457.5f, delegate.amounts.back());
  EXPECT_TRUE(touchpad.Lift());
  EXPECT_EQ(2, delegate.ends);
}

TEST(KoanSwipeInputTest, AVerticalStartIsNeverASwipe) {
  LinuxTouchpad touchpad(LinuxScrollBackend::kWayland);
  // Scrolling the tabs, drifting sideways later: still the tabs'.
  EXPECT_EQ(0, touchpad.Move(0, 60, 3, 16));
  EXPECT_EQ(0, touchpad.Move(-60, 0, 5, 16));
  EXPECT_FALSE(touchpad.Lift());
  EXPECT_TRUE(touchpad.delegate().amounts.empty());
  EXPECT_EQ(0, touchpad.delegate().ends);
}

TEST(KoanSwipeInputTest, TouchpadWheelsAreToldFromAMouseWheel) {
  TouchpadWheels wheels;
  const base::TimeTicks t = base::TimeTicks() + base::Seconds(10);
  // Before any scroll, and at any other time, a wheel is a mouse wheel.
  EXPECT_EQ(TouchpadWheels::Kind::kMouseWheel, wheels.Classify(t));
  wheels.OnScroll(t, /*taken=*/true);
  EXPECT_EQ(TouchpadWheels::Kind::kTakenScroll, wheels.Classify(t));
  EXPECT_EQ(TouchpadWheels::Kind::kMouseWheel,
            wheels.Classify(t + base::Milliseconds(1)));
  wheels.OnScroll(t + base::Milliseconds(16), /*taken=*/false);
  EXPECT_EQ(TouchpadWheels::Kind::kLeftScroll,
            wheels.Classify(t + base::Milliseconds(16)));
  EXPECT_EQ(TouchpadWheels::Kind::kMouseWheel, wheels.Classify(t));
}

// The tester's sequences on PR #529's first Linux build, where a slow swipe
// also switched Space through upstream's wheel switch about 10 px in.
TEST(KoanSwipeInputTest, ASlowShortSwipeOverTheSidebarSwitchesNothing) {
  LinuxSidebar sidebar;
  sidebar.SlowSwipe(-30);
  ASSERT_EQ(1, sidebar.delegate().ends);
  EXPECT_EQ(0, sidebar.switches());
}

TEST(KoanSwipeInputTest, ASlowSwipeOverTheSidebarSwitchesOnce) {
  LinuxSidebar sidebar;
  sidebar.SlowSwipe(-120);
  EXPECT_EQ(1, sidebar.switches());
  sidebar.SlowSwipe(120);
  EXPECT_EQ(2, sidebar.switches());
}

TEST(KoanSwipeInputTest, ASwipePastTheLastSpaceIsNeverAWheelSwitch) {
  // On the last Space the swipe turns into the add-Space badge; upstream's
  // switch would have wrapped to the first Space first.
  LinuxSidebar sidebar;
  sidebar.SlowSwipe(-260);
  EXPECT_EQ(0, sidebar.tab_list_wheels());
  EXPECT_EQ(1, sidebar.switches());  // The swipe's own, at the lift.
}

TEST(KoanSwipeInputTest, VerticalFingerScrollingStillScrollsTheTabs) {
  LinuxSidebar sidebar;
  sidebar.VerticalScroll(30);
  // Both of Views' wheel events of each scroll reach the tab list.
  EXPECT_EQ(60, sidebar.tab_list_wheels());
  EXPECT_EQ(0, sidebar.switches());
}

TEST(KoanSwipeInputTest, AMouseWheelStillSwitchesOneSpacePerBurst) {
  LinuxSidebar sidebar;
  sidebar.WheelBurst(3);
  EXPECT_EQ(1, sidebar.switches());
}

}  // namespace koan
