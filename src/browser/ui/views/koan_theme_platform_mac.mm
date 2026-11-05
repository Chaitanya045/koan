// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Koan's changes, Copyright 2026 The Koan Authors. See
// koan_theme_platform.h for the material it ports.

#include "koan/browser/ui/views/koan_theme_platform.h"

#import <AppKit/AppKit.h>

#include "skia/ext/skia_utils_mac.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"

// The material behind a panel: it takes no clicks.
@interface KoanPopoverMaterialView : NSVisualEffectView
@end

@implementation KoanPopoverMaterialView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace koan {

namespace {

NSAppearance* AppearanceFor(bool dark) {
  return [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}

// A stretchable rounded rectangle, the material's mask.
NSImage* RoundedMask(CGFloat radius) {
  const CGFloat size = 2 * radius + 1;
  NSImage* image =
      [NSImage imageWithSize:NSMakeSize(size, size)
                     flipped:NO
              drawingHandler:^BOOL(NSRect rect) {
                [NSColor.blackColor setFill];
                [[NSBezierPath bezierPathWithRoundedRect:rect
                                                 xRadius:radius
                                                 yRadius:radius] fill];
                return YES;
              }];
  image.capInsets = NSEdgeInsetsMake(radius, radius, radius, radius);
  image.resizingMode = NSImageResizingModeStretch;
  return image;
}

}  // namespace

std::optional<SkColor> GetInactiveCaptionColor(bool dark) {
  __block SkColor color = SK_ColorTRANSPARENT;
  [AppearanceFor(dark) performAsCurrentDrawingAppearance:^{
    NSColor* device = [NSColor.windowBackgroundColor
        colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
    if (device) {
      color = skia::NSDeviceColorToSkColor(device);
    }
  }];
  if (SkColorGetA(color) == SK_AlphaTRANSPARENT) {
    return std::nullopt;
  }
  return SkColorSetA(color, SK_AlphaOPAQUE);
}

bool UsesNativePopoverMaterial() {
  return true;
}

void UpdatePopoverMaterial(views::Widget* widget,
                           const gfx::Rect& panel_in_widget,
                           int radius,
                           bool dark) {
  NSView* content = widget ? widget->GetNativeView().GetNativeNSView() : nil;
  if (!content) {
    return;
  }
  KoanPopoverMaterialView* material = nil;
  for (NSView* subview in content.subviews) {
    if ([subview isKindOfClass:[KoanPopoverMaterialView class]]) {
      material = static_cast<KoanPopoverMaterialView*>(subview);
      break;
    }
  }
  if (!material) {
    // As NSPopover's own background: the popover material, blending with
    // what's behind the window, always active.
    material = [[KoanPopoverMaterialView alloc] initWithFrame:NSZeroRect];
    material.material = NSVisualEffectMaterialPopover;
    material.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    material.state = NSVisualEffectStateActive;
    // (Views sorts visual effect views below the compositor's view.)
    [content addSubview:material positioned:NSWindowBelow relativeTo:nil];
  }
  material.appearance = AppearanceFor(dark);
  NSRect frame = NSMakeRect(panel_in_widget.x(), panel_in_widget.y(),
                            panel_in_widget.width(), panel_in_widget.height());
  if (!content.isFlipped) {
    frame.origin.y = NSHeight(content.bounds) - panel_in_widget.bottom();
  }
  material.frame = frame;
  material.maskImage = RoundedMask(radius);
}

}  // namespace koan
