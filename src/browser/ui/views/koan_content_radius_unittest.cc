// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_content_radius.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanContentRadiusTest, FollowsTheMacWindowCorners) {
  // The 8pt gap around the card.
  EXPECT_FLOAT_EQ(6.f, ZenContentCardRadius(ZenOsWindowRadius(true, 15), 8));
  // Tahoe: measured 7 on the Mac (zen-mac-spec.md, the card and Glance).
  EXPECT_FLOAT_EQ(7.f, ZenContentCardRadius(ZenOsWindowRadius(true, 26), 8));
}

TEST(KoanContentRadiusTest, NeverBelowZensMinimum) {
  // Linux's 8px: 4 x 1.3 = 5.2 inner, just above Zen's 5px floor.
  EXPECT_FLOAT_EQ(4.f, ZenContentCardRadius(ZenOsWindowRadius(false, 0), 8));
  EXPECT_FLOAT_EQ(5.f / kZenSquircle, ZenContentCardRadius(4, 8));
}

}  // namespace koan
