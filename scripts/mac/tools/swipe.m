// swipe <x> <y> <dx> [steps] [ms] [--info]
//     -- a two-finger horizontal trackpad gesture over (x, y) (CG coordinates, points): the pointer moves there,
//        then continuous (pixel) scroll events with the trackpad's phases: Began, `steps` x Changed, Ended, `ms`
//        apart (16), adding up to `dx` points of scrollingDeltaX. Used by scripts/mac/video/flows/uf_swipe.mjs for
//        the swipe between Spaces over the sidebar (KoanSpaceSwipe). Only used while the user is away (the gates).
//        Prints how AppKit reads the first event: "nsevent dx=<scrollingDeltaX> precise=<0|1> phase=<NSEventPhase>
//        inverted=<isDirectionInvertedFromDevice>", which says which way the fingers "moved" for Koan: with
//        inverted=1 (natural scrolling) the fingers moved the way dx's sign says, with inverted=0 the other way.
//        --info prints that line only and posts nothing.
// Build: clang -fobjc-arc -framework Cocoa -o "$KOAN_MAC_TOOLS/swipe" scripts/mac/tools/swipe.m
#import <Cocoa/Cocoa.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static CGEventRef make(CGPoint p, double d, int64_t phase) {
  int32_t whole = (int32_t)lround(d);
  CGEventRef e = CGEventCreateScrollWheelEvent2(NULL, kCGScrollEventUnitPixel, 2, 0, whole, 0);
  CGEventSetLocation(e, p);
  CGEventSetFlags(e, 0);
  CGEventSetIntegerValueField(e, kCGScrollWheelEventIsContinuous, 1);
  CGEventSetIntegerValueField(e, kCGScrollWheelEventScrollPhase, phase);
  CGEventSetIntegerValueField(e, kCGScrollWheelEventMomentumPhase, kCGMomentumScrollPhaseNone);
  CGEventSetIntegerValueField(e, kCGScrollWheelEventPointDeltaAxis1, 0);
  CGEventSetIntegerValueField(e, kCGScrollWheelEventPointDeltaAxis2, whole);
  CGEventSetDoubleValueField(e, kCGScrollWheelEventFixedPtDeltaAxis2, d);
  return e;
}

int main(int c, char** v) {
  @autoreleasepool {
    if (c < 4) {
      fprintf(stderr, "usage: swipe <x> <y> <dx> [steps] [ms] [--info]\n");
      return 2;
    }
    bool info_only = c > 4 && !strcmp(v[c - 1], "--info");
    int argc = info_only ? c - 1 : c;
    CGPoint p = CGPointMake(atof(v[1]), atof(v[2]));
    double dx = atof(v[3]);
    int steps = argc > 4 ? MAX(1, atoi(v[4])) : 12;
    int ms = argc > 5 ? MAX(1, atoi(v[5])) : 16;
    double per = dx / steps;

    CGEventRef first = make(p, per, kCGScrollPhaseBegan);
    NSEvent* ns = [NSEvent eventWithCGEvent:first];
    printf("nsevent dx=%.1f precise=%d phase=%lu inverted=%d\n", ns.scrollingDeltaX,
           ns.hasPreciseScrollingDeltas ? 1 : 0, (unsigned long)ns.phase,
           ns.isDirectionInvertedFromDevice ? 1 : 0);
    fflush(stdout);
    if (info_only) {
      CFRelease(first);
      return 0;
    }
    // Ours, for the gates (as ptr does): input after this stamp is the user's.
    {
      char path[1024];
      const char* r = getenv("KOAN_MAC_ROOT");
      const char* h = getenv("HOME");
      if (r && *r) {
        snprintf(path, sizeof path, "%s/tmp/own-input.ts", r);
      } else {
        snprintf(path, sizeof path, "%s/work/koan-mac/tmp/own-input.ts", h ? h : ".");
      }
      FILE* o = fopen(path, "w");
      if (o) {
        fprintf(o, "%ld\n", (long)time(NULL));
        fclose(o);
      }
    }
    CGEventRef move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p, kCGMouseButtonLeft);
    CGEventSetFlags(move, 0);
    CGEventPost(kCGHIDEventTap, move);
    CFRelease(move);
    usleep(120000);

    CGEventPost(kCGHIDEventTap, first);
    CFRelease(first);
    usleep(ms * 1000);
    for (int i = 1; i < steps; i++) {
      CGEventRef e = make(p, per, kCGScrollPhaseChanged);
      CGEventPost(kCGHIDEventTap, e);
      CFRelease(e);
      usleep(ms * 1000);
    }
    CGEventRef end = make(p, 0, kCGScrollPhaseEnded);
    CGEventPost(kCGHIDEventTap, end);
    CFRelease(end);
    usleep(50000);
    return 0;
  }
}
