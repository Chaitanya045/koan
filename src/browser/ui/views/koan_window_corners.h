// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CORNERS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CORNERS_H_

#include "ui/gfx/geometry/rounded_corners_f.h"

class BrowserView;

namespace views {
class View;
}

namespace koan {

// Hooks that keep a restored window's four corners rounded alike on Linux
// (#492). On the Mac the system clips the whole window to its rounded shape;
// on Linux nothing does: the client-side frame paints the window's shape and
// shadow, upstream with square bottom corners, and every view in a corner
// has to leave it clear.

// From BrowserFrameViewLinux::GetRestoredClipRegion() and
// GetWindowRoundedCorners(): the radii of `browser_view`'s restored window,
// whose top corners upstream rounds by `radius`. Koan's browser windows round
// the bottom corners too (the frame's clip, its border and shadow, the opaque
// region, and the views that follow GetWindowRoundedCorners()); other windows
// (popups, apps) keep upstream's.
gfx::RoundedCornersF RestoredWindowCorners(const BrowserView* browser_view,
                                           float radius);

// From BrowserViewTabbedLayoutImpl::DoPostLayoutVisualAdjustments(), after
// upstream set its own corners: rounds the page's background (the gutter
// round the content card, MultiContentsView's background layer, which nothing
// clips) at the window's corners it reaches, keeping the radii upstream gives
// it in glass mode. Linux only.
void RoundContentsToWindowCorners(views::View* browser_view,
                                  bool in_glass_mode);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_CORNERS_H_
