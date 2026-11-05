// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/widget/cocoa/nsCocoaWindow-mm.patch (the window material view)
//   prefs/zen/macos.yaml (window vibrancy and material)
//   src/zen/spaces/ZenGradientGenerator.mjs (the default theme's tint)
//   src/zen/common/styles/zen-theme.css, zen-toolbar.css (the transparent
//   window and toolbar)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_glass_tint.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {
// Zen's dark default theme: rgba(0, 0, 0, 0.4).
constexpr SkColor kDarkTint = SkColorSetARGB(0x66, 0, 0, 0);
}  // namespace

TEST(KoanGlassTintTest, ActiveWindowKeepsTheTint) {
  EXPECT_EQ(kDarkTint, ResolveGlassTint(kDarkTint, true));
  // Light mode's default theme has none.
  EXPECT_EQ(SK_ColorTRANSPARENT, ResolveGlassTint(SK_ColorTRANSPARENT, true));
}

TEST(KoanGlassTintTest, InactiveWindowIsFlat) {
  EXPECT_EQ(SK_ColorTRANSPARENT, ResolveGlassTint(kDarkTint, false));
}

TEST(KoanGlassTintTest, ToolbarIsClearOnlyInGlassModeAndNotInCompactMode) {
  EXPECT_TRUE(ToolbarIsClear(/*in_glass_mode=*/true, /*compact_mode=*/false));
  // Floating over the page in compact mode: it keeps its colour as a backing.
  EXPECT_FALSE(ToolbarIsClear(true, /*compact_mode=*/true));
  // Without glass, upstream's colours stay.
  EXPECT_FALSE(ToolbarIsClear(false, false));
  EXPECT_FALSE(ToolbarIsClear(false, true));
}

TEST(KoanGlassTintTest, SidebarHasABackingOnlyInCompactMode) {
  EXPECT_EQ(0.0f, SidebarBackingOpacity(false));
  EXPECT_EQ(1.0f, SidebarBackingOpacity(true));
}

}  // namespace koan
