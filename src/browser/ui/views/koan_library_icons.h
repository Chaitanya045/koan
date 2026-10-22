// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_ICONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_ICONS_H_

#include "koan/browser/ui/views/koan_library_ui.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

namespace koan::library {

// Paints a section's tab icon, the resting frame of Zen's animated sprite
// (a download circle, a history box, two space cards), into the square
// `bounds`, filled like Zen's selected tab if `filled`. With a `progress` in
// [0, 1], the downloads icon carries a progress ring.
void PaintSectionIcon(gfx::Canvas* canvas,
                      const gfx::RectF& bounds,
                      LibrarySection section,
                      const ui::ColorProvider* provider,
                      bool filled,
                      float progress = -1.f);

// A round swatch of `color`, for colour menus.
ui::ImageModel ColorSwatch(ui::ColorId color, int size);

}  // namespace koan::library

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_ICONS_H_
