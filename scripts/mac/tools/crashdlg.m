// crashdlg list | dismiss : the macOS "quit unexpectedly" dialogs (owner UserNotificationCenter) whose text names Koan.
// list prints their texts and buttons and presses nothing; dismiss presses "Ignore"/"OK"/"Close" on those dialogs only.
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
static NSString* S(AXUIElementRef e, CFStringRef a){ CFTypeRef v=NULL; AXUIElementCopyAttributeValue(e,a,&v); return v&&CFGetTypeID(v)==CFStringGetTypeID()?(__bridge_transfer NSString*)v:nil; }
static void walk(AXUIElementRef e, NSMutableArray*texts, NSMutableArray*buttons){
 NSString*role=S(e,kAXRoleAttribute); NSString*t=S(e,kAXValueAttribute)?:S(e,kAXTitleAttribute);
 if([role isEqual:@"AXStaticText"]&&t.length)[texts addObject:t];
 if([role isEqual:@"AXButton"]&&t.length)[buttons addObject:@[t,(__bridge id)e]];
 CFArrayRef kids=NULL; AXUIElementCopyAttributeValue(e,kAXChildrenAttribute,(CFTypeRef*)&kids); if(!kids)return;
 for(CFIndex i=0;i<CFArrayGetCount(kids);i++) walk((AXUIElementRef)CFArrayGetValueAtIndex(kids,i),texts,buttons); CFRelease(kids); }
int main(int c,char**v){@autoreleasepool{
 BOOL dismiss=c>1&&!strcmp(v[1],"dismiss"); int found=0,pressed=0;
 for(NSRunningApplication*app in NSWorkspace.sharedWorkspace.runningApplications){
  if(![app.localizedName isEqual:@"UserNotificationCenter"])continue;
  AXUIElementRef ax=AXUIElementCreateApplication(app.processIdentifier); CFArrayRef ws=NULL; AXUIElementCopyAttributeValue(ax,kAXWindowsAttribute,(CFTypeRef*)&ws); if(!ws)continue;
  for(CFIndex i=0;i<CFArrayGetCount(ws);i++){ NSMutableArray*texts=[NSMutableArray array],*buttons=[NSMutableArray array]; walk((AXUIElementRef)CFArrayGetValueAtIndex(ws,i),texts,buttons);
   NSString*all=[texts componentsJoinedByString:@" | "]; if([all rangeOfString:@"Koan"].location==NSNotFound)continue; found++;
   NSMutableArray*names=[NSMutableArray array]; for(NSArray*b in buttons)[names addObject:b[0]];
   printf("dialog %d: %s  buttons: %s\n",found,all.UTF8String,[names componentsJoinedByString:@", "].UTF8String);
   if(dismiss){ for(NSString*want in @[@"Ignore",@"OK",@"Close"]) for(NSArray*b in buttons) if([b[0] isEqual:want]){ AXUIElementPerformAction((__bridge AXUIElementRef)b[1],kAXPressAction); pressed++; goto next; } next:; } } }
 printf("%d Koan dialog(s), pressed %d\n",found,pressed); return 0; }}
