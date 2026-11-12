// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_essentials_audio.h"

namespace koan {

EssentialsAudioState GetEssentialsAudioState(bool playing, bool muting) {
  if (muting) {
    return EssentialsAudioState::kMuted;
  }
  return playing ? EssentialsAudioState::kPlaying : EssentialsAudioState::kNone;
}

gfx::Rect GetEssentialsAudioBadgeBounds(const gfx::Rect& favicon) {
  return gfx::Rect(favicon.x() + kEssentialsAudioBadgeOffset,
                   favicon.y() - kEssentialsAudioBadgeOffset,
                   kEssentialsAudioBadgeSize, kEssentialsAudioBadgeSize);
}

std::u16string_view GetEssentialsAudioBadgeTooltip(EssentialsAudioState state) {
  switch (state) {
    case EssentialsAudioState::kNone:
      return std::u16string_view();
    case EssentialsAudioState::kPlaying:
      return u"Mute tab";
    case EssentialsAudioState::kMuted:
      return u"Unmute tab";
  }
}

}  // namespace koan
