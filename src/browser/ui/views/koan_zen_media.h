// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenMediaController.mjs
//   zen-media-controls.css
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// The media cards' stack, timing and time format.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_MEDIA_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_MEDIA_H_

#include <stddef.h>

#include <string>

#include "base/time/time.h"

namespace koan {

// At most three cards stack; two peek out above the front one, each 4%
// narrower and 30% fainter than the one in front.
inline constexpr size_t kMediaMaxStackedCards = 3;
inline constexpr int kMediaMaxPeekLevels = 2;
inline constexpr float kMediaPeekScaleStep = 0.04f;
inline constexpr float kMediaPeekOpacityStep = 0.3f;

// The stack expands and a new card fades in over 300 ms.
inline constexpr base::TimeDelta kMediaExpandDuration = base::Milliseconds(300);
inline constexpr base::TimeDelta kMediaFadeInDuration = base::Milliseconds(300);

// A card shows once its media has played for a second; switching away from
// its tab brings it back after half a second; the time ticks every second.
inline constexpr base::TimeDelta kMediaStillPlayingDelay = base::Seconds(1);
inline constexpr base::TimeDelta kMediaTabSwitchDelay = base::Milliseconds(500);
inline constexpr base::TimeDelta kMediaPositionInterval = base::Seconds(1);

// Media this long is a live stream: no progress.
inline constexpr base::TimeDelta kMediaLiveDuration = base::Seconds(900000);

// Zen's formatSecondsToTime: m:ss, or h:mm:ss.
std::u16string FormatMediaTime(base::TimeDelta time);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_MEDIA_H_
