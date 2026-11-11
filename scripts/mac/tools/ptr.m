// ptr pos | move x y [ms] | click x y | rclick x y   -- global pointer control (CG coordinates, points). Only used while the user is away.
#import <Cocoa/Cocoa.h>
static void post(CGEventType t, CGPoint p, CGMouseButton b){ CGEventRef e=CGEventCreateMouseEvent(NULL,t,p,b); CGEventSetIntegerValueField(e,kCGMouseEventClickState,1); CGEventPost(kCGHIDEventTap,e); CFRelease(e); }
int main(int c,char**v){@autoreleasepool{
 { char path[1024]; const char*r=getenv("KOAN_MAC_ROOT"); const char*h=getenv("HOME"); if(r&&*r) snprintf(path,sizeof path,"%s/tmp/own-input.ts",r); else snprintf(path,sizeof path,"%s/work/koan-mac/tmp/own-input.ts",h?h:"."); FILE*o=fopen(path,"w"); if(o){fprintf(o,"%ld\n",(long)time(NULL)); fclose(o);} }
 if(c<2)return 2; NSString*cmd=@(v[1]);
 CGEventRef cur=CGEventCreate(NULL); CGPoint p=CGEventGetLocation(cur); CFRelease(cur);
 if([cmd isEqual:@"pos"]){ printf("%d %d\n",(int)p.x,(int)p.y); return 0; }
 if(c<4)return 2; CGPoint to=CGPointMake(atof(v[2]),atof(v[3]));
 if([cmd isEqual:@"move"]){ int ms=c>4?atoi(v[4]):300; int steps=MAX(1,ms/8);
   for(int i=1;i<=steps;i++){ double f=(double)i/steps; CGPoint q=CGPointMake(p.x+(to.x-p.x)*f,p.y+(to.y-p.y)*f); post(kCGEventMouseMoved,q,kCGMouseButtonLeft); usleep(8000);} return 0; }
 if([cmd isEqual:@"click"]){ post(kCGEventMouseMoved,to,kCGMouseButtonLeft); usleep(80000); post(kCGEventLeftMouseDown,to,kCGMouseButtonLeft); usleep(60000); post(kCGEventLeftMouseUp,to,kCGMouseButtonLeft); return 0; }
 if([cmd isEqual:@"altclick"]){ post(kCGEventMouseMoved,to,kCGMouseButtonLeft); usleep(80000); CGEventRef d=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDown,to,kCGMouseButtonLeft); CGEventSetFlags(d,kCGEventFlagMaskAlternate); CGEventSetIntegerValueField(d,kCGMouseEventClickState,1); CGEventPost(kCGHIDEventTap,d); CFRelease(d); usleep(60000); CGEventRef u=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseUp,to,kCGMouseButtonLeft); CGEventSetFlags(u,kCGEventFlagMaskAlternate); CGEventPost(kCGHIDEventTap,u); CFRelease(u); return 0; }
 if([cmd isEqual:@"rclick"]){ post(kCGEventMouseMoved,to,kCGMouseButtonRight); usleep(80000); post(kCGEventRightMouseDown,to,kCGMouseButtonRight); usleep(60000); post(kCGEventRightMouseUp,to,kCGMouseButtonRight); return 0; }
 return 2; }}
