// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_OKLCH_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_OKLCH_H_

#include "third_party/skia/include/core/SkColor.h"

namespace koan::look {

// A colour in OKLCH, the space shadcn/ui v4 writes its tokens in: lightness
// `l` (0..1), chroma `c`, hue `h` in degrees and opacity `a` (0..1).
struct OklchColor {
  float l;
  float c;
  float h;
  float a;
};

// Converts to sRGB with Björn Ottosson's OKLab matrices and the sRGB transfer
// function, so a token comes out as the colour a browser draws for the same
// CSS. A colour outside the sRGB gamut is mapped into it with CSS Color 4's
// algorithm (it keeps its lightness and hue and loses chroma until the
// clipped colour is within a just-noticeable difference).
SkColor OklchToSkColor(const OklchColor& color);

// The same for CIE-style HSL (hue in degrees, saturation and lightness 0..1),
// for the pre-OKLCH themes (hsl(222.2 84% 4.9%)) people still paste.
SkColor HslToSkColor(float h, float s, float l, float a);

}  // namespace koan::look

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_OKLCH_H_
