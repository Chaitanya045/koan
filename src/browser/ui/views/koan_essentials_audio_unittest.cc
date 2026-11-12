// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials_audio.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanEssentialsAudioTest, NoBadgeWithoutAudio) {
  EXPECT_EQ(EssentialsAudioState::kNone, GetEssentialsAudioState(false, false));
  EXPECT_TRUE(
      GetEssentialsAudioBadgeTooltip(EssentialsAudioState::kNone).empty());
}

TEST(KoanEssentialsAudioTest, PlayingShowsTheSpeakerAndMutes) {
  EXPECT_EQ(EssentialsAudioState::kPlaying,
            GetEssentialsAudioState(true, false));
  EXPECT_EQ(u"Mute tab",
            GetEssentialsAudioBadgeTooltip(EssentialsAudioState::kPlaying));
}

// Chromium keeps kAudioPlaying while a muted tab plays: muted wins, as
// Firefox's [muted] rule comes after [soundplaying].
TEST(KoanEssentialsAudioTest, MutedWinsOverPlaying) {
  EXPECT_EQ(EssentialsAudioState::kMuted, GetEssentialsAudioState(true, true));
  EXPECT_EQ(EssentialsAudioState::kMuted, GetEssentialsAudioState(false, true));
  EXPECT_EQ(u"Unmute tab",
            GetEssentialsAudioBadgeTooltip(EssentialsAudioState::kMuted));
}

// .tab-icon-overlay: 16px, top -7px and inset-inline-end -7px from the
// favicon it is stacked on.
TEST(KoanEssentialsAudioTest, BadgeSitsOverTheFaviconsTopRightCorner) {
  const gfx::Rect favicon(30, 15, 16, 16);
  const gfx::Rect badge = GetEssentialsAudioBadgeBounds(favicon);
  EXPECT_EQ(gfx::Rect(37, 8, 16, 16), badge);
  // It overlaps the favicon's top-right quarter and sticks out past it.
  EXPECT_TRUE(badge.Intersects(favicon));
  EXPECT_GT(badge.right(), favicon.right());
  EXPECT_LT(badge.y(), favicon.y());
}

}  // namespace koan
