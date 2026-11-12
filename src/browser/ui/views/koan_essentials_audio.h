// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_AUDIO_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_AUDIO_H_

#include <string_view>

#include "ui/gfx/geometry/rect.h"

namespace koan {

// Zen's audio badge on an Essentials tile (Firefox's .tab-icon-overlay, which
// Zen shows on `[zen-essential]` tabs, tabs-css.patch): the tile keeps its
// favicon, and a round badge with the audio glyph sits over the favicon's
// top-right corner. Plain logic, for the unit tests; the view is in
// koan_essentials_audio_badge.cc.

// The badge: 16px across (--icon-size), the glyph in its 12px content box
// (padding 2px), moved 7px up and 7px towards the inline end from the
// favicon's own place (top: -7px; inset-inline-end: -7px).
inline constexpr int kEssentialsAudioBadgeSize = 16;
inline constexpr int kEssentialsAudioBadgeGlyphSize = 12;
inline constexpr int kEssentialsAudioBadgeOffset = 7;

// What the badge shows. Firefox's `[muted]` glyph wins over
// `[soundplaying]` (a muted tab that plays is muted). Zen's third state,
// `[activemedia-blocked]` (media held back until the tab is first shown),
// has no Chromium counterpart.
enum class EssentialsAudioState { kNone, kPlaying, kMuted };

// The state for a tab whose audio is `playing` (Chromium's kAudioPlaying
// alert: audible now or a moment ago) and `muting` (kAudioMuting: muted
// while it was audible).
EssentialsAudioState GetEssentialsAudioState(bool playing, bool muting);

// The badge's bounds for a favicon drawn in `favicon` (16x16, in the tab
// view's coordinates; Views mirrors them in RTL, as `inset-inline-end`
// does).
gfx::Rect GetEssentialsAudioBadgeBounds(const gfx::Rect& favicon);

// The badge's tooltip and accessible name (Firefox's
// tabbrowser-{mute,unmute}-tab-audio-background-tooltip): what a click does.
std::u16string_view GetEssentialsAudioBadgeTooltip(EssentialsAudioState state);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_AUDIO_H_
