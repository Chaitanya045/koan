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
  constexpr double kEpsilon = 1e-4;
  return std::ranges::all_of(
      rgb, [](double v) { return v >= -kEpsilon && v <= 1.0 + kEpsilon; });
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
  const double lightness = std::clamp<double>(color.l, 0.0, 1.0);
  double chroma = std::max<double>(color.c, 0.0);
  Rgb rgb = OklchToLinearSrgb(lightness, chroma, color.h);
  if (!InGamut(rgb)) {
    // Bisect the chroma that still fits.
    double low = 0.0;
    double high = chroma;
    for (int i = 0; i < 24; ++i) {
      const double mid = (low + high) / 2;
      if (InGamut(OklchToLinearSrgb(lightness, mid, color.h))) {
        low = mid;
      } else {
        high = mid;
      }
    }
    chroma = low;
    rgb = OklchToLinearSrgb(lightness, chroma, color.h);
  }
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
