// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibrary.mjs (see the header)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_library_motion.h"

#include <algorithm>
#include <cmath>

namespace koan {

namespace {

// ZenLibrary.animateProgress().
constexpr double kStiffness = 720;
constexpr double kDamping = 47;
constexpr double kMass = 1.2;

// The openProgress setter: the sidebar is gone by a third of the way
// (toolboxProgress = min(1, value * 3)), shrunk by 4% if it fades.
constexpr float kSidebarSpeed = 3;
constexpr float kSidebarShrink = 0.04f;

}  // namespace

double LibrarySpring(double seconds) {
  if (seconds <= 0) {
    return 0;
  }
  const double omega = std::sqrt(kStiffness / kMass);
  const double zeta = kDamping / (2 * std::sqrt(kStiffness * kMass));
  const double omega_d = omega * std::sqrt(1 - zeta * zeta);
  const double decay = std::exp(-zeta * omega * seconds);
  return 1 - decay * (std::cos(omega_d * seconds) +
                      zeta * omega / omega_d * std::sin(omega_d * seconds));
}

LibraryMotion GetLibraryMotion(float progress,
                               float panel_width,
                               float sidebar_extent,
                               bool compact) {
  LibraryMotion motion;
  // (A rubber band past closed doesn't bring the sidebar back past rest.)
  const float sidebar = std::clamp(progress * kSidebarSpeed, 0.f, 1.f);
  const float extent = std::clamp(sidebar_extent, 0.f, panel_width);
  if (compact || extent == 0) {
    motion.panel_offset = (1 - progress) * panel_width;
    motion.sidebar_offset = compact ? sidebar : 0;
    return motion;
  }
  motion.panel_offset = (1 - progress) * (panel_width - extent);
  motion.panel_opacity = sidebar;
  motion.sidebar_scale = 1 - sidebar * kSidebarShrink;
  motion.sidebar_opacity = 1 - sidebar;
  return motion;
}

}  // namespace koan
