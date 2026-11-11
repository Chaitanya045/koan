// keypress2 <pid> <keycode> [flags: c=cmd s=shift k=ctrl a=alt]: key down+up posted to that pid only
#import <Cocoa/Cocoa.h>
int main(int c,char**v){@autoreleasepool{
 { char path[1024]; const char*r=getenv("KOAN_MAC_ROOT"); const char*h=getenv("HOME"); if(r&&*r) snprintf(path,sizeof path,"%s/tmp/own-input.ts",r); else snprintf(path,sizeof path,"%s/work/koan-mac/tmp/own-input.ts",h?h:"."); FILE*o=fopen(path,"w"); if(o){fprintf(o,"%ld\n",(long)time(NULL)); fclose(o);} }
 if(c<3){fprintf(stderr,"usage: keypress2 pid keycode [csk a]\n");return 2;}
 pid_t pid=atoi(v[1]); CGKeyCode code=atoi(v[2]); CGEventFlags f=0;
 if(c>3){for(char*p=v[3];*p;p++){ if(*p=='c')f|=kCGEventFlagMaskCommand; if(*p=='s')f|=kCGEventFlagMaskShift; if(*p=='k')f|=kCGEventFlagMaskControl; if(*p=='a')f|=kCGEventFlagMaskAlternate; }}
 for(int d=1;d>=0;d--){ CGEventRef e=CGEventCreateKeyboardEvent(NULL,code,d); CGEventSetFlags(e,f); CGEventPostToPid(pid,e); CFRelease(e); usleep(60000);} }return 0;}
