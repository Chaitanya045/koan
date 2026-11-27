// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_lights_geometry.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

// Where the macOS traffic lights are in a window: the corner, 74 by 38 (their
// box with the room Chromium keeps around it).
gfx::Rect Lights() {
  return gfx::Rect(0, 0, 74, 38);
}

}  // namespace

TEST(KoanLightsGeometryTest, AButtonAwayFromTheLightsStays) {
  const gfx::Rect button(80, 3, 32, 32);
  EXPECT_EQ(button, MoveClearOf(button, Lights(), 216));
  const gfx::Rect below(0, 38, 32, 32);
  EXPECT_EQ(below, MoveClearOf(below, Lights(), 216));
}

TEST(KoanLightsGeometryTest, AButtonOverTheLightsMovesToTheirRight) {
  EXPECT_EQ(gfx::Rect(74, 3, 32, 32),
            MoveClearOf(gfx::Rect(0, 3, 32, 32), Lights(), 216));
  // A button partly over them: only the part that needs to move.
  EXPECT_EQ(gfx::Rect(74, 10, 32, 32),
            MoveClearOf(gfx::Rect(60, 10, 32, 32), Lights(), 216));
}

TEST(KoanLightsGeometryTest, WithNoRoomBesideThemItMovesBelow) {
  // A collapsed sidebar, 48 wide: the button goes under the lights.
  EXPECT_EQ(gfx::Rect(0, 38, 32, 32),
            MoveClearOf(gfx::Rect(0, 3, 32, 32), Lights(), 48));
  // The room is exactly enough: it goes beside them.
  EXPECT_EQ(gfx::Rect(74, 3, 32, 32),
            MoveClearOf(gfx::Rect(0, 3, 32, 32), Lights(), 74 + 32));
  EXPECT_EQ(gfx::Rect(0, 38, 32, 32),
            MoveClearOf(gfx::Rect(0, 3, 32, 32), Lights(), 74 + 31));
}

TEST(KoanLightsGeometryTest, ItNeverMovesALowerButtonUp) {
  // Already below the lights' top and clear of them to the side: stays; one
  // that is lower than the lights' bottom stays even if there's no room.
  const gfx::Rect low(0, 50, 32, 32);
  EXPECT_EQ(low, MoveClearOf(low, Lights(), 48));
}

TEST(KoanLightsGeometryTest, MonoDotsAreTheToolboxTextFaintly) {
  // Zen's --zen-toolbar-element-bg: 8% of dark text, 15% of light text.
  EXPECT_EQ(SkColorSetARGB(20, 0x20, 0x20, 0x20),
            MonoTrafficLightColor(SkColorSetRGB(0x20, 0x20, 0x20)));
  EXPECT_EQ(SkColorSetARGB(38, 0xf0, 0xf0, 0xf0),
            MonoTrafficLightColor(SkColorSetRGB(0xf0, 0xf0, 0xf0)));
}

TEST(KoanLightsGeometryTest, MonoLightsShowTheButtonsOnlyWhileHovered) {
  // Pointer away: the dots, the buttons see-through.
  EXPECT_EQ((MonoLightsState{.buttons_opaque = false, .dots_drawn = true}),
            GetMonoLightsState(false, false, false));
  // Pointer over them: AppKit's buttons, no dots under them.
  EXPECT_EQ((MonoLightsState{.buttons_opaque = true, .dots_drawn = false}),
            GetMonoLightsState(true, false, false));
  // A sheet: the buttons whatever the pointer does.
  EXPECT_EQ((MonoLightsState{.buttons_opaque = true, .dots_drawn = false}),
            GetMonoLightsState(false, true, false));
  // Buttons hidden (compact mode): no dots either.
  EXPECT_EQ((MonoLightsState{.buttons_opaque = false, .dots_drawn = false}),
            GetMonoLightsState(false, false, true));
}

// A 1280-wide window's caption buttons as the Linux frame lays them out, 32
// by 46 in the top corners (issue #487).
constexpr int kFrameWidth = 1280;
const gfx::Rect kRightClose(1244, 0, 32, 46);
const gfx::Rect kRightMaximize(1212, 0, 32, 46);
const gfx::Rect kRightMinimize(1180, 0, 32, 46);
const gfx::Rect kLeftClose(4, 0, 32, 46);

TEST(KoanLightsGeometryTest, CaptionButtonSideFollowsTheButtonLayout) {
  EXPECT_FALSE(IsCaptionButtonOnSidebarSide(kRightClose, kFrameWidth,
                                            /*sidebar_on_right=*/false));
  EXPECT_TRUE(IsCaptionButtonOnSidebarSide(kRightClose, kFrameWidth,
                                           /*sidebar_on_right=*/true));
  EXPECT_TRUE(IsCaptionButtonOnSidebarSide(kLeftClose, kFrameWidth,
                                           /*sidebar_on_right=*/false));
  EXPECT_FALSE(IsCaptionButtonOnSidebarSide(kLeftClose, kFrameWidth,
                                            /*sidebar_on_right=*/true));
}

TEST(KoanLightsGeometryTest, PageSideCaptionButtonsKeepTheirStripOpen) {
  // GNOME's default ("appmenu:close") with the sidebar on the left: the
  // close button is over the page, so its strip stays (the bug was that it
  // only showed with the pointer at the top edge).
  EXPECT_TRUE(KeepsCaptionStripOpen({kRightClose}, kFrameWidth,
                                    /*sidebar_on_right=*/false,
                                    /*sidebar_hidden=*/false));
  // KDE's default (minimise, maximise, close on the right), likewise.
  EXPECT_TRUE(KeepsCaptionStripOpen(
      {kRightMinimize, kRightMaximize, kRightClose}, kFrameWidth,
      /*sidebar_on_right=*/false, /*sidebar_hidden=*/false));
  // Buttons on the left with tabs on the right: over the page too.
  EXPECT_TRUE(KeepsCaptionStripOpen({kLeftClose}, kFrameWidth,
                                    /*sidebar_on_right=*/true,
                                    /*sidebar_hidden=*/false));
  // Buttons on both sides: the page side's keep it.
  EXPECT_TRUE(KeepsCaptionStripOpen({kLeftClose, kRightClose}, kFrameWidth,
                                    /*sidebar_on_right=*/false,
                                    /*sidebar_hidden=*/false));
}

TEST(KoanLightsGeometryTest, SidebarSideOrHiddenCaptionButtonsNeedNoStrip) {
  // On the sidebar's side they are in its top row.
  EXPECT_FALSE(KeepsCaptionStripOpen({kRightClose}, kFrameWidth,
                                     /*sidebar_on_right=*/true,
                                     /*sidebar_hidden=*/false));
  EXPECT_FALSE(KeepsCaptionStripOpen({kLeftClose}, kFrameWidth,
                                     /*sidebar_on_right=*/false,
                                     /*sidebar_hidden=*/false));
  // Compact mode hides the sidebar: the top edge reveals them.
  EXPECT_FALSE(KeepsCaptionStripOpen({kRightClose}, kFrameWidth,
                                     /*sidebar_on_right=*/false,
                                     /*sidebar_hidden=*/true));
  // No buttons (the system title bar, fullscreen), or not laid out yet.
  EXPECT_FALSE(KeepsCaptionStripOpen({}, kFrameWidth,
                                     /*sidebar_on_right=*/false,
                                     /*sidebar_hidden=*/false));
  EXPECT_FALSE(KeepsCaptionStripOpen({gfx::Rect()}, kFrameWidth,
                                     /*sidebar_on_right=*/true,
                                     /*sidebar_hidden=*/false));
}

}  // namespace koan
