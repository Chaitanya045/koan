// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_ICONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_ICONS_H_

#include "koan/browser/ui/views/koan_library_ui.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace koan::library {

// Paints a section's tab icon, the resting frame of Zen's animated sprite
// (a download circle, a history box, two space cards), into the square
// `bounds`. `fill` is transparent for tabs that aren't selected, like Zen's.
// With a `progress` in [0, 1], the downloads icon carries a progress ring.
void PaintSectionIcon(gfx::Canvas* canvas,
                      const gfx::RectF& bounds,
                      LibrarySection section,
                      SkColor stroke,
                      SkColor fill,
                      float progress = -1.f);

// Zen's tab icon colours: the accent (stroke) and, for the selected tab, the
// accent washed out towards the background (fill).
SkColor SectionIconStroke(const ui::ColorProvider* provider);
SkColor SectionIconFill(const ui::ColorProvider* provider);

// The Library glyph of the sidebar button (a box with sparkles over it).
ui::ImageModel LibraryGlyph(int size);
// Zen's "drag to reorder" handle: arrows in four directions.
ui::ImageModel MoveGlyph(int size);
// A round swatch of `color`, for colour menus.
ui::ImageModel ColorSwatch(ui::ColorId color, int size);

}  // namespace koan::library

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_ICONS_H_
