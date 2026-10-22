// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenDownloadAnimation.mjs
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_zen_download_animation.h"

#include <algorithm>
#include <cmath>

namespace koan {

namespace {

constexpr float kArcHeightRatio = 2;
constexpr float kMaxArcHeight = 1200;
constexpr float kArcRoomFraction = 0.8f;
constexpr float kStartScale = 0.5f;
constexpr float kPeakScale = 1.8f;
constexpr float kEndScale = 0.45f;

float EaseInOutQuad(float t) {
  return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
}

}  // namespace

DownloadArc ComputeDownloadArc(float dx,
                               float dy,
                               float room_above,
                               float room_below) {
  DownloadArc arc;
  arc.dx = dx;
  arc.dy = dy;
  arc.downward = room_below > room_above;
  arc.height =
      std::min({std::hypot(dx, dy) * kArcHeightRatio, kMaxArcHeight,
                (arc.downward ? room_below : room_above) * kArcRoomFraction});
  return arc;
}

DownloadArcFrame GetDownloadArcFrame(const DownloadArc& arc, float progress) {
  DownloadArcFrame frame;
  const float eased = EaseInOutQuad(progress);
  frame.x = arc.dx * eased;
  frame.y = arc.dy * eased + (arc.downward ? 1 : -1) * arc.height *
                                 (1 - std::pow(2 * eased - 1, 2.0f));
  frame.scale =
      progress < 0.5f
          ? kStartScale + progress / 0.5f * (kPeakScale - kStartScale)
          : kPeakScale - (progress - 0.5f) / 0.5f * (kPeakScale - kEndScale);
  if (progress < 0.3f) {
    frame.opacity = 0.3f + progress / 0.3f * 0.6f;
  } else if (progress < 0.98f) {
    frame.opacity = std::min(1.0f, 0.9f + (progress - 0.3f) / 0.6f * 0.1f);
  } else {
    frame.opacity = std::max(0.0f, 1 - (progress - 0.9f) / 0.1f);
  }
  return frame;
}

}  // namespace koan
