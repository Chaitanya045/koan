// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_navigation.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

TEST(KoanSpaceNavigationTest, WrapsAroundAtTheEnds) {
  EXPECT_EQ(NextSpaceIndex(2, 3, /*forward=*/true, /*wrap_around=*/true), 0u);
  EXPECT_EQ(NextSpaceIndex(0, 3, /*forward=*/false, /*wrap_around=*/true), 2u);
  EXPECT_EQ(NextSpaceIndex(1, 3, /*forward=*/true, /*wrap_around=*/true), 2u);
}

TEST(KoanSpaceNavigationTest, StopsAtTheEndsWithoutWrapAround) {
  EXPECT_EQ(NextSpaceIndex(2, 3, /*forward=*/true, /*wrap_around=*/false),
            std::nullopt);
  EXPECT_EQ(NextSpaceIndex(0, 3, /*forward=*/false, /*wrap_around=*/false),
            std::nullopt);
  EXPECT_EQ(NextSpaceIndex(1, 3, /*forward=*/false, /*wrap_around=*/false), 0u);
}

TEST(KoanSpaceNavigationTest, NoNextSpaceWithOneSpace) {
  EXPECT_EQ(NextSpaceIndex(0, 1, /*forward=*/true, /*wrap_around=*/true),
            std::nullopt);
  EXPECT_EQ(NextSpaceIndex(3, 3, /*forward=*/true, /*wrap_around=*/true),
            std::nullopt);
}

TEST(KoanSpaceNavigationTest, SlidesTheShorterWayRoundOnlyWhenWrapping) {
  EXPECT_EQ(SpaceSwitchSteps(3, 0, 4, /*wrap_around=*/true), 1);
  EXPECT_EQ(SpaceSwitchSteps(0, 3, 4, /*wrap_around=*/true), -1);
  EXPECT_EQ(SpaceSwitchSteps(3, 0, 4, /*wrap_around=*/false), -3);
  EXPECT_EQ(SpaceSwitchSteps(1, 2, 4, /*wrap_around=*/true), 1);
}

TEST(KoanSpaceNavigationTest, ScrollModifier) {
  EXPECT_TRUE(IsSpaceScrollModifierDown("ctrl", true, false, false, false));
  EXPECT_FALSE(IsSpaceScrollModifierDown("ctrl", false, true, true, true));
  EXPECT_TRUE(IsSpaceScrollModifierDown("alt", false, true, false, false));
  EXPECT_TRUE(IsSpaceScrollModifierDown("shift", false, false, true, false));
  EXPECT_TRUE(IsSpaceScrollModifierDown("meta", false, false, false, true));
  EXPECT_FALSE(IsSpaceScrollModifierDown("meta", true, false, false, false));
  // An unknown value is Zen's default.
  EXPECT_TRUE(IsSpaceScrollModifierDown("hyper", true, false, false, false));
}

}  // namespace
}  // namespace koan
