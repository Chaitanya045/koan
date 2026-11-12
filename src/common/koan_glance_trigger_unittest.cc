// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/common/koan_glance_trigger.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

TEST(GlanceTriggerTest, ParsesTheBrowsersValues) {
  EXPECT_EQ(ParseGlanceTrigger("off"), GlanceTrigger::kOff);
  EXPECT_EQ(ParseGlanceTrigger("alt"), GlanceTrigger::kAlt);
  EXPECT_EQ(ParseGlanceTrigger("ctrl"), GlanceTrigger::kCtrl);
  EXPECT_EQ(ParseGlanceTrigger("shift"), GlanceTrigger::kShift);
  EXPECT_EQ(ParseGlanceTrigger("meta"), GlanceTrigger::kMeta);
  // Zen's default for anything else, e.g. no switch.
  EXPECT_EQ(ParseGlanceTrigger(""), GlanceTrigger::kAlt);
  EXPECT_EQ(ParseGlanceTrigger("Shift"), GlanceTrigger::kAlt);
}

TEST(GlanceTriggerTest, OnlyTheTriggerModifierAlone) {
  // ctrl, shift, alt, meta.
  EXPECT_TRUE(
      IsGlanceTriggerClick(GlanceTrigger::kAlt, false, false, true, false));
  EXPECT_TRUE(
      IsGlanceTriggerClick(GlanceTrigger::kCtrl, true, false, false, false));
  EXPECT_TRUE(
      IsGlanceTriggerClick(GlanceTrigger::kShift, false, true, false, false));
  EXPECT_TRUE(
      IsGlanceTriggerClick(GlanceTrigger::kMeta, false, false, false, true));
  // Another modifier, none, or one more.
  EXPECT_FALSE(
      IsGlanceTriggerClick(GlanceTrigger::kAlt, false, true, false, false));
  EXPECT_FALSE(
      IsGlanceTriggerClick(GlanceTrigger::kAlt, false, false, false, false));
  EXPECT_FALSE(
      IsGlanceTriggerClick(GlanceTrigger::kAlt, false, true, true, false));
  // Glance off: nothing.
  for (int i = 0; i < 4; ++i) {
    EXPECT_FALSE(IsGlanceTriggerClick(GlanceTrigger::kOff, i == 0, i == 1,
                                      i == 2, i == 3));
  }
}

// #326: a trigger the browser pushes replaces the one the process started
// with, and the next push replaces it again.
TEST(GlanceTriggerTest, ThePushedTriggerIsTheProcesss) {
  SetGlanceTrigger(GlanceTrigger::kShift);
  EXPECT_EQ(GetGlanceTrigger(), GlanceTrigger::kShift);
  SetGlanceTrigger(GlanceTrigger::kOff);
  EXPECT_EQ(GetGlanceTrigger(), GlanceTrigger::kOff);
  SetGlanceTrigger(GlanceTrigger::kAlt);
  EXPECT_EQ(GetGlanceTrigger(), GlanceTrigger::kAlt);
}

}  // namespace
}  // namespace koan
