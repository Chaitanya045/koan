// cover <x> <y> <width> <height> [seconds]: an opaque window of this tool's
// own over that rectangle (points, CG global coordinates, top-left origin, as
// winid and CDP's window bounds give them), brought to the front, so that the
// window under it is covered (macOS occlusion) by another app's window. For
// the agent_lease e2e script's "covered window" case: it covers Koan's test
// window, never the user's apps. Prints "ready" once the window is on
// screen, and quits when its stdin closes (the test ended or died) or after
// `seconds` (default 120), whichever comes first, so a window can never be
// left over the user's screen.
// Build: clang -fobjc-arc -framework AppKit -o cover cover.m
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 5) {
      fprintf(stderr, "usage: cover <x> <y> <width> <height> [seconds]\n");
      return 2;
    }
    const double x = atof(argv[1]), y = atof(argv[2]);
    const double w = atof(argv[3]), h = atof(argv[4]);
    const double seconds = argc > 5 ? atof(argv[5]) : 120;
    if (w <= 0 || h <= 0 || seconds <= 0) {
      fprintf(stderr, "cover: bad size or time\n");
      return 2;
    }
    NSApplication* app = [NSApplication sharedApplication];
    // No Dock icon, but a window that can come to the front.
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    // CG global coordinates have their origin at the primary screen's top
    // left; Cocoa's at its bottom left.
    const double primary_height = NSScreen.screens.firstObject.frame.size.height;
    const double margin = 24;
    NSRect frame = NSMakeRect(x - margin, primary_height - (y + h) - margin,
                              w + 2 * margin, h + 2 * margin);
    NSWindow* window =
        [[NSWindow alloc] initWithContentRect:frame
                                    styleMask:NSWindowStyleMaskBorderless
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    window.releasedWhenClosed = NO;
    window.opaque = YES;
    window.hasShadow = NO;
    window.backgroundColor = [NSColor colorWithCalibratedWhite:0.5 alpha:1];
    window.title = @"koan-e2e-cover";
    [window orderFrontRegardless];
    [app activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];

    // Quit when the test closes our stdin, or at the deadline.
    dispatch_source_t input = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_READ, STDIN_FILENO, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(input, ^{
      char buffer[64];
      if (read(STDIN_FILENO, buffer, sizeof(buffer)) <= 0) {
        exit(0);
      }
    });
    dispatch_resume(input);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     exit(0);
                   });
    dispatch_async(dispatch_get_main_queue(), ^{
      printf("ready\n");
      fflush(stdout);
    });
    [app run];
  }
  return 0;
}
