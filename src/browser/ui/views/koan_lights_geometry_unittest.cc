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

}  // namespace koan
