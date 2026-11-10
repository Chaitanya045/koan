// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Tests of material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenMediaController.mjs, ZenDownloadAnimation.mjs,
//   ZenWindowDragChild.sys.mjs
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "koan/browser/ui/views/koan_zen_download_animation.h"
#include "koan/browser/ui/views/koan_zen_media.h"
#include "koan/browser/ui/views/koan_zen_window_drag.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

TEST(KoanZenMediaTest, FormatsMinutesAndHours) {
  EXPECT_EQ(FormatMediaTime(base::Seconds(0)), u"0:00");
  EXPECT_EQ(FormatMediaTime(base::Seconds(9)), u"0:09");
  EXPECT_EQ(FormatMediaTime(base::Seconds(61)), u"1:01");
  EXPECT_EQ(FormatMediaTime(base::Seconds(3599)), u"59:59");
  EXPECT_EQ(FormatMediaTime(base::Seconds(3600)), u"1:00:00");
  EXPECT_EQ(FormatMediaTime(base::Seconds(36005)), u"10:00:05");
}

TEST(KoanZenMediaTest, RoundsUpAndClampsAtZero) {
  // Zen's Math.ceil.
  EXPECT_EQ(FormatMediaTime(base::Milliseconds(1200)), u"0:02");
  EXPECT_EQ(FormatMediaTime(base::Seconds(-5)), u"0:00");
}

TEST(KoanZenDownloadAnimationTest, ArcBendsTowardsTheRoomierSide) {
  const DownloadArc up = ComputeDownloadArc(30, 40, /*room_above=*/500,
                                            /*room_below=*/100);
  EXPECT_FALSE(up.downward);
  // Twice the distance (50).
  EXPECT_FLOAT_EQ(up.height, 100);
  const DownloadArc down = ComputeDownloadArc(30, 40, 100, 500);
  EXPECT_TRUE(down.downward);
}

TEST(KoanZenDownloadAnimationTest, ArcHeightIsCapped) {
  // At 80% of the room.
  EXPECT_FLOAT_EQ(ComputeDownloadArc(300, 400, 200, 100).height, 160);
  // At 1200.
  EXPECT_FLOAT_EQ(ComputeDownloadArc(3000, 4000, 1e6, 0).height, 1200);
}

TEST(KoanZenDownloadAnimationTest, FramesRunFromStartToTarget) {
  const DownloadArc arc = ComputeDownloadArc(100, 50, 1000, 0);
  const DownloadArcFrame start = GetDownloadArcFrame(arc, 0);
  EXPECT_FLOAT_EQ(start.x, 0);
  EXPECT_FLOAT_EQ(start.y, 0);
  EXPECT_FLOAT_EQ(start.scale, 0.5f);
  EXPECT_FLOAT_EQ(start.opacity, 0.3f);
  const DownloadArcFrame middle = GetDownloadArcFrame(arc, 0.5f);
  // At the top of the arc (it bends up), at its biggest.
  EXPECT_FLOAT_EQ(middle.x, 50);
  EXPECT_FLOAT_EQ(middle.y, 25 - arc.height);
  EXPECT_FLOAT_EQ(middle.scale, 1.8f);
  EXPECT_FLOAT_EQ(middle.opacity, 0.9f + 0.2f / 0.6f * 0.1f);
  const DownloadArcFrame end = GetDownloadArcFrame(arc, 1);
  EXPECT_FLOAT_EQ(end.x, 100);
  EXPECT_FLOAT_EQ(end.y, 50);
  EXPECT_FLOAT_EQ(end.scale, 0.45f);
  EXPECT_FLOAT_EQ(end.opacity, 0);
}

TEST(KoanZenWindowDragTest, ScriptIsCalledWithThePoint) {
  const std::string script = base::UTF16ToUTF8(IsEmptySpaceScript(12.5, 7));
  ASSERT_FALSE(script.empty());
  EXPECT_EQ(script.front(), '(');
  EXPECT_TRUE(script.ends_with(")(12.5, 7)")) << script;
}

}  // namespace
}  // namespace koan
