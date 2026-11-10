// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_STATE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_STATE_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"

class BrowserView;

namespace views {
class View;
}

// What paints a window's background (KoanThemeController) needs to know of
// the translucent window ("glass", upstream's GlassFrameService, which Koan's
// glass makes work for every window): whether the blur is behind this window,
// and when that changes. Without the glass frame, no window is glass.
namespace koan {

// Whether `browser_view`'s window is glass (GlassFrameService eligibility:
// the setting, macOS 26, not fullscreen or Reduce Transparency), whether or
// not it is active at the moment. Then Zen's maths for a transparent window
// apply, and an inactive one is flat. Always true with
// --koan-test-window-glass (tests off macOS).
bool IsWindowGlassEnabled(const BrowserView* browser_view);

// Runs `callback` when IsWindowGlassEnabled() of `browser_view` may have
// changed, or the window went active or inactive. It runs after upstream's
// own frame view has switched its layer (BrowserFrameViewMac registers
// first). Needs the window's widget; empty without it.
base::CallbackListSubscription AddWindowGlassObserver(
    BrowserView* browser_view,
    base::RepeatingClosure callback);

// The rule for every layer Koan paints translucently: it never fills its
// bounds opaquely. CustomCornersBackground CHECKs it for its views (the
// vertical tab strip region, the top container), and an opaque layer would
// hide the blur. Call it where the layer is created or re-created, not in a
// posted fix-up; it never makes a layer opaque. Repaints `view` if it
// changed the layer.
void MakeTranslucentLayer(views::View* view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLASS_STATE_H_
