// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/color/koan_oklch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace koan::look {

namespace {

using Rgb = std::array<double, 3>;

// OKLab -> linear sRGB (Ottosson, https://bottosson.github.io/posts/oklab/).
Rgb OklabToLinearSrgb(double l, double a, double b) {
  const double l_ = l + 0.3963377774 * a + 0.2158037573 * b;
  const double m_ = l - 0.1055613458 * a - 0.0638541728 * b;
  const double s_ = l - 0.0894841775 * a - 1.2914855480 * b;
  const double lc = l_ * l_ * l_;
  const double mc = m_ * m_ * m_;
  const double sc = s_ * s_ * s_;
  return {+4.0767416621 * lc - 3.3077115913 * mc + 0.2309699292 * sc,
          -1.2684380046 * lc + 2.6097574011 * mc - 0.3413193965 * sc,
          -0.0041960863 * lc - 0.7034186147 * mc + 1.7076147010 * sc};
}

Rgb OklchToLinearSrgb(double l, double c, double h_degrees) {
  const double h = h_degrees * std::numbers::pi / 180.0;
  return OklabToLinearSrgb(l, c * std::cos(h), c * std::sin(h));
}

bool InGamut(const Rgb& rgb) {
  constexpr double kEpsilon = 1e-6;
  return std::ranges::all_of(
      rgb, [](double v) { return v >= -kEpsilon && v <= 1.0 + kEpsilon; });
}

Rgb Clip(const Rgb& rgb) {
  return {std::clamp(rgb[0], 0.0, 1.0), std::clamp(rgb[1], 0.0, 1.0),
          std::clamp(rgb[2], 0.0, 1.0)};
}

// Linear sRGB -> OKLab.
std::array<double, 3> LinearSrgbToOklab(const Rgb& rgb) {
  const double l = std::cbrt(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] +
                             0.0514459929 * rgb[2]);
  const double m = std::cbrt(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] +
                             0.1073969566 * rgb[2]);
  const double s = std::cbrt(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] +
                             0.6299787005 * rgb[2]);
  return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
          1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

// The distance between two colours in OKLab (CSS Color 4's deltaEOK).
double DeltaEOk(const Rgb& a, const Rgb& b) {
  const auto lab_a = LinearSrgbToOklab(a);
  const auto lab_b = LinearSrgbToOklab(b);
  return std::hypot(lab_a[0] - lab_b[0], lab_a[1] - lab_b[1],
                    lab_a[2] - lab_b[2]);
}

// CSS Color 4's gamut mapping to sRGB (the algorithm browsers use for
// out-of-gamut oklch()): reduce the chroma until the clipped colour is
// within a just-noticeable difference of the unclipped one.
Rgb MapToSrgbGamut(double l, double c, double h) {
  constexpr double kJnd = 0.02;
  constexpr double kEpsilon = 0.0001;
  if (l >= 1.0) {
    return {1, 1, 1};
  }
  if (l <= 0.0) {
    return {0, 0, 0};
  }
  Rgb current = OklchToLinearSrgb(l, c, h);
  if (InGamut(current)) {
    return Clip(current);
  }
  Rgb clipped = Clip(current);
  if (DeltaEOk(clipped, current) < kJnd) {
    return clipped;
  }
  double min = 0.0;
  double max = c;
  bool min_in_gamut = true;
  while (max - min > kEpsilon) {
    const double chroma = (min + max) / 2;
    current = OklchToLinearSrgb(l, chroma, h);
    if (min_in_gamut && InGamut(current)) {
      min = chroma;
      continue;
    }
    clipped = Clip(current);
    const double e = DeltaEOk(clipped, current);
    if (e < kJnd) {
      if (kJnd - e < kEpsilon) {
        return clipped;
      }
      min_in_gamut = false;
      min = chroma;
    } else {
      max = chroma;
    }
  }
  return clipped;
}

double LinearToGamma(double v) {
  v = std::clamp(v, 0.0, 1.0);
  return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

uint8_t ToByte(double v) {
  return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

}  // namespace

SkColor OklchToSkColor(const OklchColor& color) {
  const Rgb rgb = MapToSrgbGamut(std::clamp<double>(color.l, 0.0, 1.0),
                                 std::max<double>(color.c, 0.0), color.h);
  return SkColorSetARGB(ToByte(color.a), ToByte(LinearToGamma(rgb[0])),
                        ToByte(LinearToGamma(rgb[1])),
                        ToByte(LinearToGamma(rgb[2])));
}

SkColor HslToSkColor(float h, float s, float l, float a) {
  const double hue = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
  const double sat = std::clamp(s, 0.0f, 1.0f);
  const double light = std::clamp(l, 0.0f, 1.0f);
  auto channel = [&](double n) {
    const double k = std::fmod(n + hue / 30.0, 12.0);
    return light - sat * std::min(light, 1.0 - light) *
                       std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0}));
  };
  return SkColorSetARGB(ToByte(a), ToByte(channel(0)), ToByte(channel(8)),
                        ToByte(channel(4)));
}

}  // namespace koan::look
