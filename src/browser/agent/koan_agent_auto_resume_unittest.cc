// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_auto_resume.h"

#include <vector>

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

using Phase = AutoResumePolicy::Phase;

constexpr base::TimeDelta kDelay = base::Seconds(5);

class KoanAgentAutoResumeTest : public testing::Test {
 protected:
  base::TimeTicks At(double seconds) const {
    return start_ + base::Seconds(seconds);
  }

  const base::TimeTicks start_ = base::TimeTicks() + base::Seconds(100);
  AutoResumePolicy policy_;
};

// T-resume-1: only the user's input in the page resumes by itself.
TEST_F(KoanAgentAutoResumeTest, OnlyUserInputIsEligible) {
  policy_.Pause("user-input", /*user_on_tab=*/false, At(0));
  EXPECT_EQ(policy_.hold(), PauseHold::kNone);
  EXPECT_EQ(policy_.phase(), Phase::kWaiting);

  AutoResumePolicy take_over;
  take_over.Pause("user", /*user_on_tab=*/true, At(0));
  EXPECT_EQ(take_over.hold(), PauseHold::kTakeOver);
  EXPECT_EQ(take_over.phase(), Phase::kHeld);
  EXPECT_FALSE(take_over.NextCheck());
  EXPECT_FALSE(take_over.ResumesAt());
  for (double t : {5.0, 10.0, 60.0, 3600.0}) {
    take_over.SetUserOnTab(false, At(t));
    EXPECT_FALSE(take_over.Advance(At(t)));
  }
}

TEST_F(KoanAgentAutoResumeTest, EveryHoldIsStickyUntilEnd) {
  for (PauseHold hold :
       {PauseHold::kSecretField, PauseHold::kUserNeeded,
        PauseHold::kInterstitial, PauseHold::kDialog, PauseHold::kKeepPaused}) {
    AutoResumePolicy policy;
    policy.Pause("user-input", /*user_on_tab=*/false, At(0));
    policy.Hold(hold);
    EXPECT_EQ(policy.hold(), hold);
    EXPECT_FALSE(PauseHoldName(hold).empty());
    // Nothing that happens later lets it go.
    policy.OnInput(At(1));
    policy.SetUserOnTab(true, At(2));
    policy.SetUserOnTab(false, At(3));
    EXPECT_FALSE(policy.Advance(At(600)));
    EXPECT_EQ(policy.phase(), Phase::kHeld);
    // The first hold is the one named.
    policy.Hold(PauseHold::kDialog == hold ? PauseHold::kKeepPaused
                                           : PauseHold::kDialog);
    EXPECT_EQ(policy.hold(), hold);
    // Hand back, Stop or the tab closing end it; the next pause starts clean.
    policy.End();
    EXPECT_EQ(policy.phase(), Phase::kNotPaused);
    policy.Pause("user-input", false, At(700));
    EXPECT_EQ(policy.hold(), PauseHold::kNone);
  }
}

TEST_F(KoanAgentAutoResumeTest, HoldNeedsAPause) {
  policy_.Hold(PauseHold::kKeepPaused);
  EXPECT_EQ(policy_.phase(), Phase::kNotPaused);
  policy_.Pause("user-input", false, At(0));
  EXPECT_EQ(policy_.hold(), PauseHold::kNone);
  // kNone and kSetting are not latched holds.
  policy_.Hold(PauseHold::kNone);
  policy_.Hold(PauseHold::kSetting);
  EXPECT_EQ(policy_.hold(), PauseHold::kNone);
}

// T-resume-2: the user left the tab.
TEST_F(KoanAgentAutoResumeTest, LeftTabResumesAfterTheDelay) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  policy_.SetUserOnTab(false, At(1));
  // Leaving is not activity: the wait runs from the input.
  EXPECT_EQ(policy_.NextCheck(), At(5));
  EXPECT_EQ(policy_.ResumesAt(), At(5));
  EXPECT_FALSE(policy_.Advance(At(4.9)));
  EXPECT_TRUE(policy_.Advance(At(5)));
  // No countdown when the user is not there to see it.
  EXPECT_FALSE(policy_.CountdownEnds());
}

TEST_F(KoanAgentAutoResumeTest, InputInTheLeftTabRestartsTheWait) {
  policy_.Pause("user-input", /*user_on_tab=*/false, At(0));
  policy_.OnInput(At(3));
  EXPECT_FALSE(policy_.Advance(At(5)));
  EXPECT_EQ(policy_.NextCheck(), At(8));
  EXPECT_TRUE(policy_.Advance(At(8)));
}

TEST_F(KoanAgentAutoResumeTest, ComingBackHandsOverToTheCountdown) {
  policy_.Pause("user-input", /*user_on_tab=*/false, At(0));
  // Back on the tab before the delay: that is activity, and now the user is
  // there, so the pill counts down first.
  policy_.SetUserOnTab(true, At(4));
  EXPECT_FALSE(policy_.Advance(At(5)));
  EXPECT_EQ(policy_.NextCheck(), At(9));
  EXPECT_EQ(policy_.ResumesAt(), At(14));
  EXPECT_FALSE(policy_.Advance(At(9)));
  EXPECT_EQ(policy_.phase(), Phase::kCountdown);
  EXPECT_EQ(policy_.CountdownEnds(), At(14));
  EXPECT_TRUE(policy_.Advance(At(14)));
}

// T-resume-3: the user stays on the tab.
TEST_F(KoanAgentAutoResumeTest, StayedOnTabCountsDownThenResumes) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  EXPECT_EQ(policy_.ResumesAt(), At(10));
  EXPECT_FALSE(policy_.Advance(At(4)));
  EXPECT_EQ(policy_.phase(), Phase::kWaiting);
  EXPECT_FALSE(policy_.Advance(At(5)));
  EXPECT_EQ(policy_.phase(), Phase::kCountdown);
  EXPECT_EQ(policy_.CountdownEnds(), At(5) + kAutoResumeCountdown);
  EXPECT_EQ(policy_.NextCheck(), At(10));
  EXPECT_FALSE(policy_.Advance(At(9.5)));
  EXPECT_TRUE(policy_.Advance(At(10)));
}

TEST_F(KoanAgentAutoResumeTest, LateCheckStillShowsTheWholeCountdown) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  EXPECT_FALSE(policy_.Advance(At(7)));
  EXPECT_EQ(policy_.CountdownEnds(), At(12));
}

TEST_F(KoanAgentAutoResumeTest, MouseMoveResetsButNeverResumesEarly) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  EXPECT_FALSE(policy_.Advance(At(5)));
  ASSERT_EQ(policy_.phase(), Phase::kCountdown);
  // Moving the mouse over the page cancels the countdown and restarts the
  // wait.
  policy_.OnMouseMove(At(6));
  EXPECT_EQ(policy_.phase(), Phase::kWaiting);
  EXPECT_FALSE(policy_.CountdownEnds());
  EXPECT_FALSE(policy_.Advance(At(10)));
  EXPECT_EQ(policy_.NextCheck(), At(11));
  EXPECT_FALSE(policy_.Advance(At(11)));
  EXPECT_TRUE(policy_.Advance(At(16)));
}

TEST_F(KoanAgentAutoResumeTest, MouseMoveWithoutAPauseDoesNothing) {
  policy_.OnMouseMove(At(1));
  policy_.OnInput(At(1));
  EXPECT_EQ(policy_.phase(), Phase::kNotPaused);
  EXPECT_FALSE(policy_.Advance(At(100)));
}

TEST_F(KoanAgentAutoResumeTest, InputDuringTheCountdownRestartsIt) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  EXPECT_FALSE(policy_.Advance(At(5)));
  EXPECT_FALSE(policy_.Advance(At(8)));
  policy_.OnInput(At(8));
  EXPECT_EQ(policy_.phase(), Phase::kWaiting);
  EXPECT_FALSE(policy_.Advance(At(10)));
  EXPECT_FALSE(policy_.Advance(At(13)));
  EXPECT_EQ(policy_.CountdownEnds(), At(18));
  EXPECT_TRUE(policy_.Advance(At(18)));
}

TEST_F(KoanAgentAutoResumeTest, LeavingDuringTheCountdownResumesNow) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  EXPECT_FALSE(policy_.Advance(At(6)));
  policy_.SetUserOnTab(false, At(7));
  EXPECT_FALSE(policy_.CountdownEnds());
  EXPECT_TRUE(policy_.Advance(At(7)));
}

TEST_F(KoanAgentAutoResumeTest, KeepPausedDuringTheCountdown) {
  policy_.Pause("user-input", /*user_on_tab=*/true, At(0));
  EXPECT_FALSE(policy_.Advance(At(5)));
  policy_.Hold(PauseHold::kKeepPaused);
  EXPECT_EQ(policy_.phase(), Phase::kHeld);
  EXPECT_FALSE(policy_.CountdownEnds());
  EXPECT_FALSE(policy_.Advance(At(10)));
  policy_.SetUserOnTab(false, At(11));
  EXPECT_FALSE(policy_.Advance(At(100)));
}

TEST_F(KoanAgentAutoResumeTest, WaitForHandBackSetting) {
  policy_.SetSettings({.enabled = false, .delay = kDelay});
  policy_.Pause("user-input", false, At(0));
  EXPECT_EQ(policy_.hold(), PauseHold::kSetting);
  EXPECT_EQ(PauseHoldName(PauseHold::kSetting), "setting");
  EXPECT_FALSE(policy_.Advance(At(60)));
  // Turned back on mid-pause, the pause may resume again (not latched).
  policy_.SetSettings({.enabled = true, .delay = base::Seconds(2)});
  EXPECT_EQ(policy_.hold(), PauseHold::kNone);
  EXPECT_TRUE(policy_.Advance(At(60)));
}

TEST_F(KoanAgentAutoResumeTest, LongerDelay) {
  policy_.SetSettings({.enabled = true, .delay = base::Seconds(60)});
  policy_.Pause("user-input", false, At(0));
  EXPECT_FALSE(policy_.Advance(At(59)));
  EXPECT_TRUE(policy_.Advance(At(60)));
}

// Stop, revocation and the tab closing end the pause without a resume.
TEST_F(KoanAgentAutoResumeTest, EndNeverResumes) {
  policy_.Pause("user-input", false, At(0));
  policy_.End();
  EXPECT_FALSE(policy_.paused());
  EXPECT_FALSE(policy_.NextCheck());
  EXPECT_FALSE(policy_.ResumesAt());
  EXPECT_FALSE(policy_.Advance(At(600)));
}

TEST(KoanAgentAutoResumeSettingsTest, PrefRange) {
  EXPECT_EQ(kAutoResumeSecondsLimit.default_value, 5);
  EXPECT_EQ(kAutoResumeSecondsLimit.min, 2);
  EXPECT_EQ(kAutoResumeSecondsLimit.max, 60);
  // Missing prefs: on, 5 s.
  AutoResumeSettings settings = ReadAutoResumeSettings(nullptr, nullptr);
  EXPECT_TRUE(settings.enabled);
  EXPECT_EQ(settings.delay, base::Seconds(5));
  for (int seconds = 2; seconds <= 60; ++seconds) {
    const base::Value stored(seconds);
    EXPECT_EQ(ReadAutoResumeSettings(nullptr, &stored).delay,
              base::Seconds(seconds));
  }
  // No 0 or unlimited value: anything else is the default.
  std::vector<base::Value> bad;
  bad.emplace_back(0);
  bad.emplace_back(1);
  bad.emplace_back(61);
  bad.emplace_back(-1);
  bad.emplace_back(2.5);
  bad.emplace_back("10");
  for (const base::Value& stored : bad) {
    EXPECT_EQ(ReadAutoResumeSettings(nullptr, &stored).delay, base::Seconds(5))
        << stored.DebugString();
  }
  const base::Value off(false);
  EXPECT_FALSE(ReadAutoResumeSettings(&off, nullptr).enabled);
  const base::Value not_bool(0);
  EXPECT_TRUE(ReadAutoResumeSettings(&not_bool, nullptr).enabled);
}

}  // namespace

}  // namespace koan::agent
