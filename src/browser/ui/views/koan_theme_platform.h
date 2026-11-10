// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0, and from
// Firefox, Copyright the Mozilla Foundation, MPL-2.0:
//   zen-browser-ui.css (InactiveCaption), widget/cocoa/nsLookAndFeel.mm
//   (InactiveCaption on macOS), toolkit/themes/shared/global-shared.css and
//   prefs/zen/macos.yaml (native popovers)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PLATFORM_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PLATFORM_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Rect;
}

namespace views {
class Widget;
}

namespace koan {

// The colour Zen paints an inactive window with
// (zen.view.grey-out-inactive-windows: `background: InactiveCaption`). On
// macOS Firefox resolves InactiveCaption to NSColor.windowBackgroundColor,
// in the window's appearance. Nullopt elsewhere: the caller falls back to
// its colour ID.
std::optional<SkColor> GetInactiveCaptionColor(bool dark);

// Whether Zen's panels are native popovers here: on macOS
// (widget.macos.native-popovers) a panel's CSS background is transparent
// and the popover's own material shows.
bool UsesNativePopoverMaterial();

// Puts the popover material (NSVisualEffectMaterialPopover) behind
// `panel_in_widget` (the panel's bounds in the widget, rounded by `radius`),
// in the light or dark appearance. Call again when either changes. Does
// nothing off macOS.
void UpdatePopoverMaterial(views::Widget* widget,
                           const gfx::Rect& panel_in_widget,
                           int radius,
                           bool dark);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_THEME_PLATFORM_H_
