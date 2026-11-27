// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_corners_geometry.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

// A restored window's visible area in its browser view, 968 by 598, with
// corners of 8.
constexpr gfx::RectF kWindow(0, 0, 968, 598);
constexpr gfx::RoundedCornersF kWindowCorners(8);

}  // namespace

TEST(KoanWindowCornersTest, KoanWindowsRoundAllFourCorners) {
  EXPECT_EQ(gfx::RoundedCornersF(8), RestoredCorners(8, true));
  // Upstream's Linux frame: the top only.
  EXPECT_EQ(gfx::RoundedCornersF(8, 8, 0, 0), RestoredCorners(8, false));
  // No radius (tiled, or no compositing): square either way.
  EXPECT_EQ(gfx::RoundedCornersF(), RestoredCorners(0, true));
}

TEST(KoanWindowCornersTest, ThePageBesideALeftSidebarRoundsItsRightCorners) {
  // The gutter round the card, right of a 230 wide sidebar, full height
  // (#492: the window's top-right and bottom-right corners were square).
  EXPECT_EQ(gfx::RoundedCornersF(0, 8, 8, 0),
            CornersAtWindowCorners(gfx::RectF(230, 0, 738, 598), kWindow,
                                   kWindowCorners));
}

TEST(KoanWindowCornersTest, ThePageBesideARightSidebarRoundsItsLeftCorners) {
  EXPECT_EQ(gfx::RoundedCornersF(8, 0, 0, 8),
            CornersAtWindowCorners(gfx::RectF(0, 0, 738, 598), kWindow,
                                   kWindowCorners));
}

TEST(KoanWindowCornersTest, ThePageAloneRoundsAllFour) {
  // Compact mode with the sidebar hidden.
  EXPECT_EQ(kWindowCorners,
            CornersAtWindowCorners(kWindow, kWindow, kWindowCorners));
}

TEST(KoanWindowCornersTest, APageBelowTheTopReachesOnlyTheBottomCorners) {
  // Under a toolbar or a docked bookmarks bar.
  EXPECT_EQ(gfx::RoundedCornersF(0, 0, 8, 8),
            CornersAtWindowCorners(gfx::RectF(0, 46, 968, 552), kWindow,
                                   kWindowCorners));
  // Away from every edge (a floating panel): none.
  EXPECT_EQ(gfx::RoundedCornersF(),
            CornersAtWindowCorners(gfx::RectF(8, 8, 952, 582), kWindow,
                                   kWindowCorners));
}

TEST(KoanWindowCornersTest, HalfADipOfRoundingStillReachesTheCorner) {
  // The frame's clip is a float rect; view bounds are whole DIPs.
  EXPECT_EQ(gfx::RoundedCornersF(0, 8, 8, 0),
            CornersAtWindowCorners(gfx::RectF(230, 0.4f, 737.6f, 597.8f),
                                   kWindow, kWindowCorners));
  EXPECT_EQ(gfx::RoundedCornersF(),
            CornersAtWindowCorners(gfx::RectF(230, 1, 737, 596), kWindow,
                                   kWindowCorners));
}

TEST(KoanWindowCornersTest, EachCornerTakesTheWindowsRadiusOnlyWhereItCan) {
  // Maximized, fullscreen or tiled: the window has no rounded corners.
  EXPECT_EQ(gfx::RoundedCornersF(),
            CornersAtWindowCorners(kWindow, kWindow, gfx::RoundedCornersF()));
  // Upstream's Linux frame (top only).
  EXPECT_EQ(gfx::RoundedCornersF(0, 8, 0, 0),
            CornersAtWindowCorners(gfx::RectF(230, 0, 738, 598), kWindow,
                                   gfx::RoundedCornersF(8, 8, 0, 0)));
}

TEST(KoanWindowCornersTest, UpstreamsGlassRadiiAreKept) {
  // In glass mode upstream rounds the page background's lower-left beside the
  // sidebar's bottom corner; the window's corners are added, not swapped in.
  EXPECT_EQ(gfx::RoundedCornersF(0, 8, 8, 12),
            LargerCorners(gfx::RoundedCornersF(0, 0, 0, 12),
                          gfx::RoundedCornersF(0, 8, 8, 0)));
  EXPECT_EQ(gfx::RoundedCornersF(8),
            LargerCorners(gfx::RoundedCornersF(8), gfx::RoundedCornersF(4)));
}

}  // namespace koan
