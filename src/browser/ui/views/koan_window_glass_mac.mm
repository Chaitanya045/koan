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

#import "koan/browser/ui/views/koan_window_glass_mac.h"

#include "chrome/browser/ui/views/frame/glass_frame_service.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_glass_tint.h"
#include "koan/browser/ui/views/koan_window_glass.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"

// Clicks go to the views above, not to the blur.
@interface KoanGlassBackgroundView : NSVisualEffectView
@end

@implementation KoanGlassBackgroundView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

// Tells the glass frame service when Reduce Transparency changes, so it
// re-decides which windows are glass.
@interface KoanReduceTransparencyObserver : NSObject
@end

@implementation KoanReduceTransparencyObserver
- (void)displayOptionsChanged:(NSNotification*)notification {
  if (GlassFrameService* service = GlassFrameService::GetInstance()) {
    service->OnThemeChanged();
  }
}
@end

namespace koan {

NSView* CreateGlassBackgroundView(NSRect frame) {
  NSVisualEffectView* view =
      [[KoanGlassBackgroundView alloc] initWithFrame:frame];
  // ZenWindowMaterialView (nsCocoaWindow-mm.patch): the default pref value
  // zen.widget.macos.window-material = 1 is the HUD window material.
  view.material = NSVisualEffectMaterialHUDWindow;
  view.appearance = nil;
  view.emphasized = NO;
  view.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  // Zen greys out inactive windows (zen.view.grey-out-inactive-windows,
  // default on); the material does it natively: flat, no blur, while the
  // window isn't active.
  view.state = NSVisualEffectStateFollowsWindowActiveState;
  view.wantsLayer = YES;
  return view;
}

bool IsWindowGlassAllowed() {
  return ![[NSWorkspace sharedWorkspace]
      accessibilityDisplayShouldReduceTransparency];
}

namespace {

// Zen's InactiveCaption on macOS (Gecko's nsLookAndFeel): the system's window
// background, as it looks in the UI's colour scheme.
SkColor InactiveWash(bool dark) {
  NSAppearance* appearance = [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
  __block SkColor wash = SK_ColorBLACK;
  [appearance performAsCurrentDrawingAppearance:^{
    NSColor* color = [NSColor.windowBackgroundColor
        colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    wash = SkColorSetA(skia::CGColorRefToSkColor(color.CGColor), 0xff);
  }];
  return wash;
}

}  // namespace

SkColor GetGlassTint(const ui::ColorProvider& colors, bool window_active) {
  return ResolveGlassTint(colors.GetColor(kColorKoanGlassTint), window_active,
                          InactiveWash(color_utils::IsDark(
                              colors.GetColor(ui::kColorFrameActive))));
}

void StartWindowGlass() {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;

  static KoanReduceTransparencyObserver* observer =
      [[KoanReduceTransparencyObserver alloc] init];
  [[[NSWorkspace sharedWorkspace] notificationCenter]
      addObserver:observer
         selector:@selector(displayOptionsChanged:)
             name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
           object:nil];
}

}  // namespace koan
