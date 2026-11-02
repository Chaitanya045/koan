// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenDownloadAnimation.mjs
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// The download icon's arc, scale and fade, and the downloads tile's
// timing.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_DOWNLOAD_ANIMATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_DOWNLOAD_ANIMATION_H_

#include "base/time/time.h"

namespace koan {

// 60 keyframes over zen.downloads.download-animation-duration (1000 ms).
inline constexpr int kDownloadArcSteps = 60;
inline constexpr base::TimeDelta kDownloadArcDuration = base::Seconds(1);

// The tile slides in (350 ms) past its place, growing to 110%, settles
// (200 ms), stays for the arc's duration + 200 ms, then shrinks to 90%
// (150 ms) and slides out fading (300 ms). It starts and ends at 80%.
inline constexpr base::TimeDelta kDownloadTileIn = base::Milliseconds(350);
inline constexpr base::TimeDelta kDownloadTileSettle = base::Milliseconds(200);
inline constexpr base::TimeDelta kDownloadTileHold =
    kDownloadArcDuration + base::Milliseconds(200);
inline constexpr base::TimeDelta kDownloadTilePress = base::Milliseconds(150);
inline constexpr base::TimeDelta kDownloadTileOut = base::Milliseconds(300);
inline constexpr float kDownloadTileOutsideScale = 0.8f;
inline constexpr float kDownloadTileOvershootScale = 1.1f;
inline constexpr float kDownloadTilePressScale = 0.9f;

// The arc from (0, 0) to (dx, dy): its height and whether it bends down.
struct DownloadArc {
  float dx = 0;
  float dy = 0;
  float height = 0;
  bool downward = false;
};

// Zen's #calculateOptimalArc: towards the side with more room (the room
// above and below both ends), twice as high as the distance, at most 1200
// and at most 80% of that room.
DownloadArc ComputeDownloadArc(float dx,
                               float dy,
                               float room_above,
                               float room_below);

// Where the icon is along `arc`, its scale and opacity, at `progress` (0 to
// 1) of Zen's keyframes.
struct DownloadArcFrame {
  float x = 0;
  float y = 0;
  float scale = 1;
  float opacity = 1;
};
DownloadArcFrame GetDownloadArcFrame(const DownloadArc& arc, float progress);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_DOWNLOAD_ANIMATION_H_
