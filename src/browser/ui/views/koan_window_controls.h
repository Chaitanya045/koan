// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CONTROLS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CONTROLS_H_

#include "ui/gfx/geometry/size.h"

class BrowserView;

namespace koan {

// Size of the caption-button areas the frame reserves at the top corners
// (client-side decorations; leading and trailing, as the desktop's button
// layout puts them): their total width and the larger height, or an empty
// size if it draws none (system title bar). The frame paints these buttons
// above the client view, so when Koan hides the toolbar it keeps this strip
// free for them instead of drawing its own (which would ignore the desktop's
// button layout).
gfx::Size GetCaptionButtonAreaSize(const BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CONTROLS_H_
