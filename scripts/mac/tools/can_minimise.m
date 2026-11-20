// can_minimise: whether this Mac session can minimise a window now. It opens
// a plain titled, miniaturizable window of its own (off to the side, small),
// asks AppKit to minimise it, and prints "minimised=1" or "minimised=0" after
// 3 s, then quits. Exit 0 when it could, 1 when it couldn't.
// The bench found (cycle 19 triage 2) that no window minimises while the
// session is locked or the person is away: Koan's, an older Koan's, and this
// one's alike (Window > Minimize, the button, AXMinimized). The minimise
// checks (agent_lease.mjs, uf_render.mjs) skip then, saying so.
// Build: clang -fobjc-arc -framework Cocoa -o can_minimise can_minimise.m
#import <Cocoa/Cocoa.h>
#include <stdio.h>

int main(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(40, 40, 160, 100)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window setTitle:@"koan-bench-can-minimise"];
    [window orderFrontRegardless];
    __block int minimised = 0;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2),
                   dispatch_get_main_queue(), ^{
                     [window miniaturize:nil];
                     dispatch_after(
                         dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                         dispatch_get_main_queue(), ^{
                           minimised = [window isMiniaturized] ? 1 : 0;
                           printf("minimised=%d\n", minimised);
                           fflush(stdout);
                           [window close];
                           [NSApp stop:nil];
                           [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                               location:NSZeroPoint
                                                          modifierFlags:0
                                                              timestamp:0
                                                           windowNumber:0
                                                                context:nil
                                                                subtype:0
                                                                  data1:0
                                                                  data2:0]
                                    atStart:YES];
                         });
                   });
    [NSApp run];
    return minimised ? 0 : 1;
  }
}
