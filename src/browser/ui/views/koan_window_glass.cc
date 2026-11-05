// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/widget/cocoa/nsCocoaWindow-mm.patch (the window material view)
//   prefs/zen/macos.yaml (window vibrancy and material)
//   src/zen/spaces/ZenGradientGenerator.mjs (the default theme's tint)
//   src/zen/common/styles/zen-theme.css, zen-toolbar.css (the transparent
//   window and toolbar)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_window_glass.h"

#include "build/build_config.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_glass_tint.h"
#include "ui/base/ui_base_features.h"

namespace koan {

bool IsWindowGlassSupported() {
  return ::features::IsGlassFrameEnabled();
}

void OnGlassLayout(const views::View* browser_view,
                   bool in_glass_mode,
                   CustomCornersBackground* toolbar_background,
                   CustomCornersBackground* top_container_background) {
  const CustomCorners::ColorChoiceWithAlpha transparent{
      CustomCorners::ToolbarTheme(), 0.0f};
  if (ToolbarIsClear(in_glass_mode, IsCompactModeActive(browser_view))) {
    toolbar_background->SetPrimaryColor(transparent);
    top_container_background->SetPrimaryColor(transparent);
    return;
  }
  // Out of glass mode (or floating over the page in compact mode) only undo
  // what was done above; upstream sets the top container's colour on every
  // layout, and its own rules (the frame colour in immersive mode) are not
  // ours to override.
  if (toolbar_background->primary_color() == transparent) {
    toolbar_background->SetPrimaryColor(
        CustomCorners::ColorChoiceWithAlpha{CustomCorners::ToolbarTheme()});
  }
}

bool KeepsTopContainerClear(bool in_glass_mode,
                            const views::View* browser_view) {
  return ToolbarIsClear(in_glass_mode, IsCompactModeActive(browser_view));
}

float SidebarGlassOpacity(const views::View* browser_view) {
  return SidebarBackingOpacity(IsCompactModeActive(browser_view));
}

#if !BUILDFLAG(IS_MAC)
bool IsWindowGlassAllowed() {
  return true;
}

void StartWindowGlass() {}
#endif

}  // namespace koan
