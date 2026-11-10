// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_loading_bar.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

base::TimeTicks At(int ms) {
  return base::TimeTicks() + base::Milliseconds(ms);
}

}  // namespace

TEST(KoanLoadingBarTest, NothingBeforeShow) {
  KoanLoadingBar bar;
  EXPECT_TRUE(bar.IsDone(At(0)));
  EXPECT_TRUE(bar.FrameAt(At(0)).bar.IsEmpty());
}

TEST(KoanLoadingBarTest, FadesInThenPulses) {
  KoanLoadingBar bar;
  bar.Show(At(0));
  EXPECT_FLOAT_EQ(0, bar.FrameAt(At(0)).opacity);
  EXPECT_FLOAT_EQ(0.4f, bar.FrameAt(At(200)).opacity);
  // The pulse starts small and centred, 5rem at 85%.
  const KoanLoadingBar::Frame start = bar.FrameAt(At(0));
  EXPECT_FLOAT_EQ(KoanLoadingBar::kWidth * 0.85f, start.bar.width());
  EXPECT_FLOAT_EQ(0, start.bar.CenterPoint().x());
  EXPECT_FLOAT_EQ(0, start.bar.CenterPoint().y());
  // At its largest after a second, fully opaque, and back after two.
  const KoanLoadingBar::Frame peak = bar.FrameAt(At(1000));
  EXPECT_FLOAT_EQ(KoanLoadingBar::kWidth * 0.95f, peak.bar.width());
  EXPECT_FLOAT_EQ(1, peak.opacity);
  EXPECT_FLOAT_EQ(KoanLoadingBar::kWidth * 0.85f,
                  bar.FrameAt(At(2000)).bar.width());
  EXPECT_FLOAT_EQ(0.6f, bar.FrameAt(At(2000)).opacity);
  EXPECT_FALSE(bar.FrameAt(At(1000)).long_load);
}

TEST(KoanLoadingBarTest, LongLoadSettlesIntoATrackWithASweep) {
  KoanLoadingBar bar;
  bar.Show(At(0));
  bar.StartLongLoad(At(3000));
  EXPECT_TRUE(bar.IsLongLoad());
  // Grows from nothing.
  EXPECT_TRUE(bar.FrameAt(At(3000)).bar.IsEmpty());
  const KoanLoadingBar::Frame settled = bar.FrameAt(At(3300));
  EXPECT_TRUE(settled.long_load);
  EXPECT_FLOAT_EQ(KoanLoadingBar::kLongLoadWidth, settled.bar.width());
  EXPECT_FLOAT_EQ(KoanLoadingBar::kHeight, settled.bar.height());
  EXPECT_FLOAT_EQ(0, settled.bar.CenterPoint().x());
  EXPECT_FLOAT_EQ(1, settled.opacity);
  // The sweep is 75% of the track, starting fully left of it and ending
  // fully right of it.
  EXPECT_FLOAT_EQ(120, settled.sweep.width());
  EXPECT_LE(settled.sweep.right(), settled.bar.x());
  const KoanLoadingBar::Frame end = bar.FrameAt(At(4299));
  EXPECT_GE(end.sweep.x(), end.bar.right() - 1);
}

TEST(KoanLoadingBarTest, HidesShrinkingAndFading) {
  KoanLoadingBar bar;
  bar.Show(At(0));
  bar.Hide(At(1000));
  EXPECT_FALSE(bar.IsShowing());
  EXPECT_FALSE(bar.IsDone(At(1150)));
  const KoanLoadingBar::Frame half = bar.FrameAt(At(1150));
  EXPECT_FLOAT_EQ(0.5f, half.opacity);
  EXPECT_FLOAT_EQ((KoanLoadingBar::kWidth * 0.95f + 64) / 2, half.bar.width());
  EXPECT_TRUE(bar.IsDone(At(1300)));
  EXPECT_TRUE(bar.FrameAt(At(1300)).bar.IsEmpty());
}

TEST(KoanLoadingBarTest, ShowWhileShowingKeepsTheBar) {
  KoanLoadingBar bar;
  bar.Show(At(0));
  bar.StartLongLoad(At(3000));
  bar.Show(At(3500));
  EXPECT_TRUE(bar.IsLongLoad());
}

TEST(KoanLoadingBarTest, ShowWhileHidingStartsOver) {
  KoanLoadingBar bar;
  bar.Show(At(0));
  bar.StartLongLoad(At(3000));
  bar.Hide(At(4000));
  bar.Show(At(4100));
  EXPECT_TRUE(bar.IsShowing());
  EXPECT_FALSE(bar.IsLongLoad());
  EXPECT_FLOAT_EQ(0, bar.FrameAt(At(4100)).opacity);
}

TEST(KoanLoadingBarTest, ReducedMotionIsStillAndGoesAtOnce) {
  KoanLoadingBar bar;
  bar.set_reduced_motion(true);
  bar.Show(At(0));
  EXPECT_FLOAT_EQ(1, bar.FrameAt(At(0)).opacity);
  EXPECT_EQ(bar.FrameAt(At(0)).bar, bar.FrameAt(At(700)).bar);
  bar.StartLongLoad(At(3000));
  EXPECT_FLOAT_EQ(KoanLoadingBar::kLongLoadWidth,
                  bar.FrameAt(At(3000)).bar.width());
  EXPECT_TRUE(bar.FrameAt(At(3500)).sweep.IsEmpty());
  bar.Hide(At(4000));
  EXPECT_TRUE(bar.IsDone(At(4000)));
}

}  // namespace koan
