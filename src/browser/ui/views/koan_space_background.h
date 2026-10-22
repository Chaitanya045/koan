// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BACKGROUND_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BACKGROUND_H_

#include "koan/browser/ui/views/koan_space_theme.h"

namespace gfx {
class Canvas;
class ImageSkia;
class RectF;
}  // namespace gfx

namespace koan::theme {

// Paints a background's layers the way CSS paints Zen's (bottom to top, as
// one group at `opacity`), sized to `box`: the window's background and the
// picker's preset swatches.
void PaintLayers(gfx::Canvas* canvas,
                 const gfx::RectF& box,
                 const std::vector<GradientLayer>& layers,
                 float opacity = 1);

// Fills `box` with the window's own colour, below the layers.
void PaintBase(gfx::Canvas* canvas, const gfx::RectF& box, SkColor color);

// Tiles Zen's grain texture over `box` (anchored at its origin) at
// `opacity`.
void PaintGrain(gfx::Canvas* canvas, const gfx::RectF& box, float opacity);

// Zen's grain texture (grain-bg.png).
const gfx::ImageSkia& GrainImage();

}  // namespace koan::theme

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_BACKGROUND_H_
