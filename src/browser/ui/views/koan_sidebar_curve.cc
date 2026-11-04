// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/zen/compact-mode/sidebar.inc.css
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_sidebar_curve.h"

#include <algorithm>
#include <array>

#include "ui/gfx/geometry/cubic_bezier.h"

namespace koan {

namespace {

// Its value at 0%, 1%, ... 100% of the time, from the CSS.
constexpr std::array<float, 101> kZenCompactModeFunc = {
    0.0f,      0.002748f, 0.010544f, 0.022757f, 0.038804f, 0.058151f, 0.080308f,
    0.104828f, 0.131301f, 0.159358f, 0.188662f, 0.21891f,  0.249828f, 0.281172f,
    0.312724f, 0.344288f, 0.375693f, 0.40679f,  0.437447f, 0.467549f, 0.497f,
    0.525718f, 0.553633f, 0.580688f, 0.60684f,  0.632052f, 0.656298f, 0.679562f,
    0.701831f, 0.723104f, 0.743381f, 0.76267f,  0.780983f, 0.798335f, 0.814744f,
    0.830233f, 0.844826f, 0.858549f, 0.87143f,  0.883498f, 0.894782f, 0.905314f,
    0.915125f, 0.924247f, 0.93271f,  0.940547f, 0.947787f, 0.954463f, 0.960603f,
    0.966239f, 0.971397f, 0.976106f, 0.980394f, 0.984286f, 0.987808f, 0.990984f,
    0.993837f, 0.99639f,  0.998664f, 1.000679f, 1.002456f, 1.004011f, 1.005363f,
    1.006528f, 1.007522f, 1.008359f, 1.009054f, 1.009618f, 1.010065f, 1.010405f,
    1.010649f, 1.010808f, 1.01089f,  1.010904f, 1.010857f, 1.010757f, 1.010611f,
    1.010425f, 1.010205f, 1.009955f, 1.009681f, 1.009387f, 1.009077f, 1.008754f,
    1.008422f, 1.008083f, 1.00774f,  1.007396f, 1.007052f, 1.00671f,  1.006372f,
    1.00604f,  1.005713f, 1.005394f, 1.005083f, 1.004782f, 1.004489f, 1.004207f,
    1.003935f, 1.003674f, 1.003423f,
};

}  // namespace

double SidebarSpringAt(double t) {
  const double position = std::clamp(t, 0.0, 1.0) * 100.0;
  const size_t index = std::min<size_t>(static_cast<size_t>(position), 99);
  const double fraction = position - static_cast<double>(index);
  return kZenCompactModeFunc[index] +
         fraction *
             (kZenCompactModeFunc[index + 1] - kZenCompactModeFunc[index]);
}

double SidebarEaseAt(double t) {
  static const gfx::CubicBezier ease(0.25, 0.1, 0.25, 1.0);
  return ease.Solve(std::clamp(t, 0.0, 1.0));
}

double SidebarSlideValue(double from, double target, double t) {
  return from + (target - from) *
                    (target > from ? SidebarSpringAt(t) : SidebarEaseAt(t));
}

}  // namespace koan
