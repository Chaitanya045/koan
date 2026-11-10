// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/zen/compact-mode/sidebar.inc.css (the values the test checks)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_sidebar_curve.h"

#include <algorithm>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanSidebarCurveTest, SpringEndpoints) {
  EXPECT_DOUBLE_EQ(0.0, SidebarSpringAt(0.0));
  EXPECT_NEAR(1.003423, SidebarSpringAt(1.0), 1e-6);
  // The CSS table's points: 0.497 at 20%, 1.010904 at 73% (the peak).
  EXPECT_NEAR(0.497, SidebarSpringAt(0.2), 1e-6);
  EXPECT_NEAR(1.010904, SidebarSpringAt(0.73), 1e-6);
}

TEST(KoanSidebarCurveTest, SpringIsClampedAndInterpolated) {
  EXPECT_DOUBLE_EQ(SidebarSpringAt(0.0), SidebarSpringAt(-1.0));
  EXPECT_DOUBLE_EQ(SidebarSpringAt(1.0), SidebarSpringAt(2.0));
  // Between two table points the CSS interpolates linearly.
  EXPECT_NEAR((0.497 + 0.525718) / 2, SidebarSpringAt(0.205), 1e-6);
}

TEST(KoanSidebarCurveTest, SpringOvershootsOnceAndSettles) {
  double peak = 0.0;
  for (int i = 0; i <= 100; ++i) {
    peak = std::max(peak, SidebarSpringAt(i / 100.0));
  }
  EXPECT_GT(peak, 1.0);
  EXPECT_LT(peak, 1.02);
  // It rises monotonically until the peak.
  double previous = -1.0;
  for (int i = 0; i <= 73; ++i) {
    const double value = SidebarSpringAt(i / 100.0);
    EXPECT_GE(value, previous);
    previous = value;
  }
}

TEST(KoanSidebarCurveTest, EaseEndpointsAndMonotonic) {
  EXPECT_NEAR(0.0, SidebarEaseAt(0.0), 1e-9);
  EXPECT_NEAR(1.0, SidebarEaseAt(1.0), 1e-9);
  EXPECT_DOUBLE_EQ(SidebarEaseAt(0.0), SidebarEaseAt(-1.0));
  EXPECT_DOUBLE_EQ(SidebarEaseAt(1.0), SidebarEaseAt(2.0));
  double previous = -1.0;
  for (int i = 0; i <= 100; ++i) {
    const double value = SidebarEaseAt(i / 100.0);
    EXPECT_GE(value, previous);
    previous = value;
  }
}

TEST(KoanSidebarCurveTest, SlideValueGoesTheRightWay) {
  // Out: the spring, from where it is to 1.
  EXPECT_DOUBLE_EQ(0.0, SidebarSlideValue(0.0, 1.0, 0.0));
  EXPECT_NEAR(0.497, SidebarSlideValue(0.0, 1.0, 0.2), 1e-6);
  EXPECT_NEAR(0.5 + 0.5 * 0.497, SidebarSlideValue(0.5, 1.0, 0.2), 1e-6);
  // In: the ease, from where it is to 0.
  EXPECT_DOUBLE_EQ(1.0, SidebarSlideValue(1.0, 0.0, 0.0));
  EXPECT_NEAR(0.0, SidebarSlideValue(1.0, 0.0, 1.0), 1e-9);
  EXPECT_NEAR(1.0 - 0.3 * SidebarEaseAt(0.4), SidebarSlideValue(1.0, 0.7, 0.4),
              1e-9);
}

TEST(KoanSidebarCurveTest, Durations) {
  EXPECT_EQ(250, kSidebarRevealDuration.InMilliseconds());
  EXPECT_EQ(150, kSidebarHideDuration.InMilliseconds());
  EXPECT_EQ(150, kSidebarKeepHoverDuration.InMilliseconds());
}

}  // namespace koan
