// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_traffic_lights.h"

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
#include "base/logging.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/remote_cocoa/app_shim/immersive_mode_controller_cocoa.h"
#include "koan/browser/ui/views/koan_lights_geometry.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/views/widget/widget.h"

// Zen's mono window controls: three dots behind AppKit's buttons, which are
// see-through (alpha 0, not hidden, so that AppKit's own hover and clicks
// still work) until the pointer is over them. The view itself is never hidden:
// it owns the tracking area that says where the pointer is, and a hidden
// view's area tracks nothing (its visible rect is empty), so hiding it while
// the pointer is over it ends the hover at once and AppKit sends an exit, then
// an enter once it shows again, as fast as the run loop turns. It stops
// drawing instead.
@interface KoanMonoTrafficLightsView : NSView
@property(nonatomic, copy) NSColor* dotColor;
@property(nonatomic) BOOL pointerInside;
// Lays the dots out over `buttons` (in the superview's coordinates) and shows
// either them or the buttons.
- (void)updateWithButtons:(NSArray<NSButton*>*)buttons;
@end

@implementation KoanMonoTrafficLightsView {
  NSMutableArray<NSValue*>* _centres;
  BOOL _dotsDrawn;
}

@synthesize dotColor = _dotColor;
@synthesize pointerInside = _pointerInside;

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _centres = [NSMutableArray array];
    [self addTrackingArea:[[NSTrackingArea alloc]
                              initWithRect:NSZeroRect
                                   options:NSTrackingMouseEnteredAndExited |
                                           NSTrackingActiveAlways |
                                           NSTrackingInVisibleRect
                                     owner:self
                                  userInfo:nil]];
  }
  return self;
}

- (NSView*)hitTest:(NSPoint)point {
  // Never in the way of the buttons' clicks.
  return nil;
}

- (void)mouseEntered:(NSEvent*)event {
  self.pointerInside = YES;
  [self updateWithButtons:nil];
}

- (void)mouseExited:(NSEvent*)event {
  self.pointerInside = NO;
  [self updateWithButtons:nil];
}

- (NSArray<NSButton*>*)buttonsOfWindow {
  NSWindow* window = self.window;
  NSMutableArray<NSButton*>* buttons = [NSMutableArray array];
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    if (NSButton* button = [window standardWindowButton:type]) {
      [buttons addObject:button];
    }
  }
  return buttons;
}

- (void)updateWithButtons:(NSArray<NSButton*>*)buttons {
  if (!buttons) {
    buttons = [self buttonsOfWindow];
  }
  NSRect frame = NSZeroRect;
  [_centres removeAllObjects];
  for (NSButton* button in buttons) {
    frame = NSUnionRect(frame, button.frame);
  }
  if (!NSEqualRects(self.frame, frame)) {
    self.frame = frame;
    [self setNeedsDisplay:YES];
  }
  for (NSButton* button in buttons) {
    const NSRect b = button.frame;
    [_centres
        addObject:[NSValue
                      valueWithPoint:NSMakePoint(NSMidX(b) - NSMinX(frame),
                                                 NSMidY(b) - NSMinY(frame))]];
  }
  const koan::MonoLightsState state = koan::GetMonoLightsState(
      self.pointerInside, self.window.attachedSheet != nil,
      buttons.count && buttons.firstObject.hidden);
  // (Only what changed: this runs on every layout.)
  const CGFloat alpha = state.buttons_opaque ? 1.0 : 0.0;
  for (NSButton* button in buttons) {
    if (button.alphaValue != alpha) {
      button.alphaValue = alpha;
    }
  }
  if (state.dots_drawn != _dotsDrawn) {
    _dotsDrawn = state.dots_drawn;
    // For the Mac check of a steady hover (lights_hover_steady.mjs).
    VLOG(1) << "KoanMonoTrafficLights: " << (_dotsDrawn ? "dots" : "no dots");
    [self setNeedsDisplay:YES];
  }
}

- (void)drawRect:(NSRect)dirtyRect {
  if (!_dotsDrawn) {
    return;
  }
  // Zen's --zen-traffic-light-size: 6px, 7px with macOS 26's larger buttons.
  CGFloat radius = 6;
  if (@available(macOS 26, *)) {
    radius = 7;
  }
  [self.dotColor setFill];
  for (NSValue* value in _centres) {
    const NSPoint c = value.pointValue;
    [[NSBezierPath
        bezierPathWithOvalInRect:NSMakeRect(c.x - radius, c.y - radius,
                                            2 * radius, 2 * radius)] fill];
  }
}

@end

namespace koan {

namespace {

NSString* const kMonoLightsIdentifier = @"KoanMonoTrafficLights";

KoanMonoTrafficLightsView* FindMonoLights(NSView* parent) {
  for (NSView* view in parent.subviews) {
    if ([view.identifier isEqualToString:kMonoLightsIdentifier]) {
      return base::apple::ObjCCast<KoanMonoTrafficLightsView>(view);
    }
  }
  return nil;
}

}  // namespace

void SetMonoTrafficLights(const BrowserView* browser_view,
                          std::optional<SkColor> dot_color) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window = widget && !widget->IsClosed()
                         ? widget->GetNativeWindow().GetNativeNSWindow()
                         : nil;
  NSButton* close = [window standardWindowButton:NSWindowCloseButton];
  NSView* parent = close.superview;
  if (!parent) {
    return;
  }
  KoanMonoTrafficLightsView* dots = FindMonoLights(parent);
  if (!dot_color) {
    if (dots) {
      [dots removeFromSuperview];
      for (NSWindowButton type :
           {NSWindowCloseButton, NSWindowMiniaturizeButton,
            NSWindowZoomButton}) {
        [window standardWindowButton:type].alphaValue = 1.0;
      }
    }
    return;
  }
  if (!dots) {
    dots = [[KoanMonoTrafficLightsView alloc] initWithFrame:NSZeroRect];
    dots.identifier = kMonoLightsIdentifier;
    [parent addSubview:dots positioned:NSWindowBelow relativeTo:close];
  }
  NSColor* color = skia::SkColorToSRGBNSColor(*dot_color);
  if (![dots.dotColor isEqual:color]) {
    dots.dotColor = color;
    [dots setNeedsDisplay:YES];
  }
  [dots updateWithButtons:nil];
}

void SetTrafficLightsShown(const BrowserView* browser_view, bool shown) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    [[window standardWindowButton:type] setHidden:!shown];
  }
  // The mono dots go and come with them.
  if (KoanMonoTrafficLightsView* dots = FindMonoLights(
          [window standardWindowButton:NSWindowCloseButton].superview)) {
    [dots updateWithButtons:nil];
  }
}

bool IsPointerOverWindow(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window || !window.isOnActiveSpace) {
    return false;
  }
  // The topmost window of the screen at the pointer (windows that are not
  // on screen are not listed), this one or one that hangs from it. In native
  // fullscreen, AppKit's title strip with the traffic lights (below the menu
  // bar, over the top of the window) is a window of its own: the pointer on it
  // is still on this window, as far as Koan's reveals are concerned (it is
  // also over this screen's one fullscreen window).
  const NSInteger number = [NSWindow windowNumberAtPoint:NSEvent.mouseLocation
                             belowWindowWithWindowNumber:0];
  for (NSWindow* under = [NSApp windowWithWindowNumber:number]; under;
       under = under.parentWindow) {
    if (under == window) {
      return true;
    }
    if (window.styleMask & NSWindowStyleMaskFullScreen &&
        remote_cocoa::IsNSToolbarFullScreenWindow(under) &&
        under.screen == window.screen) {
      return true;
    }
  }
  return false;
}

void ClearTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window =
      widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  if (!window) {
    return;
  }
  window.titlebarAppearsTransparent = YES;
  window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
  window.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

void ShowFullscreenTitlebar(const BrowserView* browser_view) {
  const views::Widget* widget = browser_view->GetWidget();
  NSWindow* window = widget && !widget->IsClosed()
                         ? widget->GetNativeWindow().GetNativeNSWindow()
                         : nil;
  if (!window) {
    return;
  }
  // (Only where AppKit's buttons are hidden: this runs on every layout.)
  if ([window standardWindowButton:NSWindowCloseButton].hidden) {
    SetTrafficLightsShown(browser_view, true);
  }
}

}  // namespace koan
