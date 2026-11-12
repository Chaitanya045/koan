// hold down x y | to x y [ms] | up x y   -- a held-button drag, one step per call (CG coordinates, points). Only used
// while the user is away, by scripts/mac/video/flows/essentials_drag.mjs through flows/common.mjs's gates.
// down moves to (x, y) and presses the left button there; to moves from where the pointer is to (x, y) in 12 ms steps
// over `ms` (default 400) with the button held (kCGEventLeftMouseDragged); up releases at (x, y). Unlike ptr's
// down/dragto/up, the press settles for 150 ms and the steps are slower, as a person's drag: the flow can look at
// the sidebar between calls while the tab is still held.
#import <Cocoa/Cocoa.h>
static void post(CGEventType t, CGPoint p){ CGEventRef e=CGEventCreateMouseEvent(NULL,t,p,kCGMouseButtonLeft); CGEventSetIntegerValueField(e,kCGMouseEventClickState,1); CGEventPost(kCGHIDEventTap,e); CFRelease(e); }
int main(int c,char**v){@autoreleasepool{
 { char path[1024]; const char*r=getenv("KOAN_MAC_ROOT"); const char*h=getenv("HOME"); if(r&&*r) snprintf(path,sizeof path,"%s/tmp/own-input.ts",r); else snprintf(path,sizeof path,"%s/work/koan-mac/tmp/own-input.ts",h?h:"."); FILE*o=fopen(path,"w"); if(o){fprintf(o,"%ld\n",(long)time(NULL)); fclose(o);} }
 if(c<4)return 2; NSString*cmd=@(v[1]); CGPoint to=CGPointMake(atof(v[2]),atof(v[3]));
 CGEventRef cur=CGEventCreate(NULL); CGPoint p=CGEventGetLocation(cur); CFRelease(cur);
 if([cmd isEqual:@"down"]){ post(kCGEventMouseMoved,to); usleep(150000); post(kCGEventLeftMouseDown,to); return 0; }
 if([cmd isEqual:@"to"]){ int ms=c>4?atoi(v[4]):400; int steps=MAX(2,ms/12);
   for(int i=1;i<=steps;i++){ double f=(double)i/steps; post(kCGEventLeftMouseDragged,CGPointMake(p.x+(to.x-p.x)*f,p.y+(to.y-p.y)*f)); usleep(12000);} return 0; }
 if([cmd isEqual:@"up"]){ post(kCGEventLeftMouseUp,to); return 0; }
 return 2; }}
