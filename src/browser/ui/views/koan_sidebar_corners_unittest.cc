// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_corners.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanSidebarCornersTest, UpstreamsRuleOutsideCompactMode) {
  // A docked sidebar keeps upstream's corners: shown while the value is
  // positive, hidden at 0 and while it is concave (negative).
  EXPECT_TRUE(SidebarFloatingCornerVisible(1.0, /*compact_mode=*/false));
  EXPECT_TRUE(SidebarFloatingCornerVisible(0.25, false));
  EXPECT_FALSE(SidebarFloatingCornerVisible(0.0, false));
  EXPECT_FALSE(SidebarFloatingCornerVisible(-1.0, false));
}

TEST(KoanSidebarCornersTest, NeverInCompactMode) {
  // The values upstream's flyout passes through as the hidden sidebar is
  // peeked in fullscreen (1 to -1 and back) and as it collapses (from 1):
  // the slid strip is elsewhere, so a corner would stand alone at the top
  // edge.
  for (double value : {1.0, 0.75, 0.5, 0.01, 0.0, -0.5, -1.0}) {
    EXPECT_FALSE(SidebarFloatingCornerVisible(value, /*compact_mode=*/true))
        << value;
  }
}

}  // namespace koan
