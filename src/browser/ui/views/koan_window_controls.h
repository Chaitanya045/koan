// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CONTROLS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CONTROLS_H_

#include <memory>

#include "ui/gfx/geometry/size.h"

class BrowserView;

namespace views {
class View;
}

namespace koan {

// Size of the caption-button area the frame reserves on the trailing side
// (client-side decorations), or an empty size if it draws none there (system
// title bar, or caption buttons on the leading side).
gfx::Size GetCaptionButtonAreaSize(BrowserView* browser_view);

// A row of minimise / maximise-restore / close buttons for `browser_view`'s
// window, each `button_size`. Used where Koan hides the frame-drawn caption
// buttons under its own UI (compact mode, single toolbar).
std::unique_ptr<views::View> CreateWindowControls(BrowserView* browser_view,
                                                  const gfx::Size& button_size);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CONTROLS_H_
