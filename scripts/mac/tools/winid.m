// winid <pid>: the on-screen windows of that exact pid, one per line, biggest first:
//   <CGWindowID> <x> <y> <width> <height> <layer>
// (points, CG global coordinates). Only reads the window list; needs no screen-recording
// grant (window names are left out). For `screencapture -l<CGWindowID>` in scripts/mac/visual/.
// Build: clang -framework CoreGraphics -framework Foundation -o winid winid.m
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 2) {
      fprintf(stderr, "usage: winid <pid>\n");
      return 2;
    }
    const pid_t pid = (pid_t)atoi(argv[1]);
    NSArray* list = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    NSMutableArray* mine = [NSMutableArray array];
    for (NSDictionary* w in list) {
      if ([w[(id)kCGWindowOwnerPID] intValue] != pid) continue;
      CGRect r;
      if (!CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)w[(id)kCGWindowBounds], &r)) continue;
      [mine addObject:@{@"id" : w[(id)kCGWindowNumber], @"layer" : w[(id)kCGWindowLayer],
                        @"r" : [NSValue valueWithBytes:&r objCType:@encode(CGRect)]}];
    }
    [mine sortUsingComparator:^NSComparisonResult(NSDictionary* a, NSDictionary* b) {
      CGRect ra, rb;
      [a[@"r"] getValue:&ra];
      [b[@"r"] getValue:&rb];
      const double aa = ra.size.width * ra.size.height, ab = rb.size.width * rb.size.height;
      return aa > ab ? NSOrderedAscending : aa < ab ? NSOrderedDescending : NSOrderedSame;
    }];
    for (NSDictionary* w in mine) {
      CGRect r;
      [w[@"r"] getValue:&r];
      printf("%u %d %d %d %d %d\n", [w[@"id"] unsignedIntValue], (int)r.origin.x, (int)r.origin.y,
             (int)r.size.width, (int)r.size.height, [w[@"layer"] intValue]);
    }
    return mine.count ? 0 : 1;
  }
}
