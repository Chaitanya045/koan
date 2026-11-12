// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenMenubar.mjs
//   zen-menubar.ftl
//   zen-workspaces.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/cocoa/koan_main_menu.h"

#import <Cocoa/Cocoa.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/strings/strcat.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/accelerators/platform_accelerator_cocoa.h"
#include "ui/base/base_window.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace {

using Action = koan::shortcuts::Action;
using Binding = koan::shortcuts::Binding;
using ColorScheme = ThemeService::BrowserColorScheme;

// The Spaces menu's items on the active Space (Zen's Theme…, Change Name…,
// Change Icon…); an item's tag.
enum SpaceCommand {
  kEditTheme,
  kChangeName,
  kChangeIcon,
};

// The window whose Spaces and shortcuts the menus act on: the one that was
// active last. Null without a window that has Koan's UI (e.g. a popup).
koan::KoanBrowserViewController* ActiveController() {
  BrowserWindowInterface* browser =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  BrowserView* view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  if (!view || !view->GetIsNormalType()) {
    return nullptr;
  }
  return koan::KoanBrowserViewController::From(view);
}

// The window File > Close Window acts on, as AppController finds it
// (-windowForPerformClose): the target of -performClose: (a popover's
// window), or its topmost parent if that is a child window (a bubble).
NSWindow* WindowForPerformClose() {
  id target = [NSApp targetForAction:@selector(performClose:)];
  NSWindow* window = nil;
  if (NSPopover* popover = base::apple::ObjCCast<NSPopover>(target)) {
    window = popover.contentViewController.view.window;
  } else {
    window = base::apple::ObjCCast<NSWindow>(target);
  }
  while (window.parentWindow) {
    window = window.parentWindow;
  }
  return window;
}

// The browser window of `window`, if it is one.
BrowserWindowInterface* BrowserOfWindow(NSWindow* window) {
  BrowserView* view = window ? BrowserView::GetBrowserViewForNativeWindow(
                                   gfx::NativeWindow(window))
                             : nullptr;
  return view ? view->browser() : nullptr;
}

const koan::shortcuts::Info* FindInfo(Action action) {
  for (const koan::shortcuts::Info& info : koan::shortcuts::Actions()) {
    if (info.action == action) {
      return &info;
    }
  }
  return nullptr;
}

// The key an NSMenuItem needs for `accelerator`, or nothing if AppKit can't
// show it (ui::GetKeyEquivalentAndModifierMaskFromAccelerator() would fail).
struct MenuKey {
  NSString* key_equivalent;
  NSUInteger modifier_mask;
};
std::optional<MenuKey> MenuKeyFor(const ui::Accelerator& accelerator) {
  unichar shifted = 0;
  unichar unshifted = 0;
  if (accelerator.key_code() == ui::VKEY_UNKNOWN ||
      ui::MacKeyCodeForWindowsKeyCode(accelerator.key_code(), 0, &shifted,
                                      &unshifted) == -1) {
    return std::nullopt;
  }
  KeyEquivalentAndModifierMask* equivalent =
      ui::GetKeyEquivalentAndModifierMaskFromAccelerator(accelerator);
  return MenuKey{equivalent.keyEquivalent, equivalent.modifierMask};
}

// Shows `accelerator` (or no key) on `item`.
void SetKey(NSMenuItem* item,
            const std::optional<ui::Accelerator>& accelerator) {
  std::optional<MenuKey> key;
  if (accelerator) {
    key = MenuKeyFor(*accelerator);
  }
  item.keyEquivalent = key ? key->key_equivalent : @"";
  item.keyEquivalentModifierMask = key ? key->modifier_mask : 0;
}

// Collects `menu`'s items and its submenus'.
void CollectItems(NSMenu* menu, NSMutableArray<NSMenuItem*>* items) {
  for (NSMenuItem* item in menu.itemArray) {
    [items addObject:item];
    if (item.submenu) {
      CollectItems(item.submenu, items);
    }
  }
}

// Chrome's menu items whose key is Koan's. First, those that do what a Koan
// shortcut does and take its key; Zen rebinds them, e.g. Save Page.
struct Relocated {
  Action action;
  int command;
};
constexpr Relocated kRelocated[] = {
    {Action::kSavePage, IDC_SAVE_PAGE},
    {Action::kNewPrivateWindow, IDC_NEW_INCOGNITO_WINDOW},
    {Action::kInspect, IDC_DEV_TOOLS_INSPECT},
    {Action::kMuteSite, IDC_WINDOW_MUTE_SITE},
    {Action::kDownloads, IDC_SHOW_DOWNLOADS},
};

// Keys of Firefox's that Zen keeps and Chrome's item has elsewhere or not at
// all: View Source (key_viewSource) and Show All Bookmarks (manBookmarkKb).
struct FixedKey {
  int command;
  ui::KeyboardCode key;
  int modifiers;
};
constexpr FixedKey kFixedKeys[] = {
    {IDC_VIEW_SOURCE, ui::VKEY_U, ui::EF_COMMAND_DOWN},
    {IDC_SHOW_BOOKMARK_MANAGER, ui::VKEY_O,
     ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN},
};

// What Chrome's items had before Koan changed their keys (an NSArray of the
// key equivalent and the mask), so that a shortcut the user moves or clears
// gives the key back.
NSMapTable<NSMenuItem*, NSArray*>* ChangedItems() {
  static NSMapTable<NSMenuItem*, NSArray*>* table =
      [NSMapTable weakToStrongObjectsMapTable];
  return table;
}

void RememberKey(NSMenuItem* item) {
  if (![ChangedItems() objectForKey:item]) {
    [ChangedItems()
        setObject:@[ item.keyEquivalent, @(item.keyEquivalentModifierMask) ]
           forKey:item];
  }
}

bool IsChromeCommand(NSMenuItem* item, int command) {
  return item.tag == command && item.action == @selector(commandDispatch:);
}

NSMenuItem* ActionItem(Action action, id target, NSString* title = nil) {
  const koan::shortcuts::Info* info = FindInfo(action);
  NSMenuItem* item = [[NSMenuItem alloc]
      initWithTitle:title ?: base::SysUTF8ToNSString(info->label)
             action:@selector(perform:)
      keyEquivalent:@""];
  item.target = target;
  item.tag = static_cast<NSInteger>(action);
  return item;
}

// Inserts `items` after the item with `tag` (at the end without one).
void InsertAfter(NSMenu* menu, NSInteger tag, NSArray<NSMenuItem*>* items) {
  NSInteger index = [menu indexOfItemWithTag:tag];
  index = index == -1 ? menu.numberOfItems : index + 1;
  for (NSMenuItem* item in items) {
    [menu insertItem:item atIndex:index++];
  }
}

// Hides the items of `menu` with one of `tags`.
void HideItems(NSMenu* menu, std::initializer_list<int> tags) {
  for (NSMenuItem* item in menu.itemArray) {
    for (int tag : tags) {
      if (item.tag == tag) {
        item.hidden = YES;
      }
    }
  }
}

// An item that runs Chrome's command like its own menus' items do
// (AppController / the window dispatch it), with the key Chrome has for it.
NSMenuItem* ChromeItem(NSString* title, int command) {
  NSMenuItem* item =
      [[NSMenuItem alloc] initWithTitle:title
                                 action:@selector(commandDispatch:)
                          keyEquivalent:@""];
  item.tag = command;
  if (const ui::Accelerator* accelerator =
          AcceleratorsCocoa::GetInstance()->GetAcceleratorForCommand(command)) {
    SetKey(item, *accelerator);
  }
  return item;
}

}  // namespace

// The target and validator of Koan's menu items, and the delegate of the
// Spaces menu, whose items follow the window's Spaces.
@interface KoanMenuTarget : NSObject <NSMenuDelegate, NSMenuItemValidation>
+ (KoanMenuTarget*)shared;
// File > Close Window (⇧⌘W): Chrome's command for a browser window, else
// AppKit's -performClose: on the window (see the File menu below).
- (void)performClose:(id)sender;
// Runs the Koan shortcut in the item's tag.
- (void)perform:(NSMenuItem*)item;
// Switches the active window to the Space at the item's tag (an index).
- (void)switchSpace:(NSMenuItem*)item;
// The same from the Dock, which first brings the window forward.
- (void)dockSwitchSpace:(NSMenuItem*)item;
// Theme…, Change Name… and Change Icon… for the active Space (the item's tag
// is a SpaceCommand).
- (void)spaceCommand:(NSMenuItem*)item;
// View > Appearance: the browser's colour scheme in the item's tag.
- (void)setAppearance:(NSMenuItem*)item;
// Sets the Spaces menu, whose items are enabled each time it opens.
- (void)setSpacesMenu:(NSMenu*)menu;
// Shows the keys of the active window's shortcuts in `mainMenu` (see the
// definition).
- (void)refreshKeysIn:(NSMenu*)mainMenu;
// Refreshes the keys whenever the menu bar opens.
- (void)observeMenuBar:(NSMenu*)mainMenu;
// Refreshes the keys of the menu bar Koan extended, if it is the main menu.
- (void)refreshMainMenu;
@end

namespace {

// The label of a Space in the menus: "<icon>  <name>", like Move To.
NSString* SpaceTitle(const koan::KoanWorkspaceController::SpaceInfo& space) {
  return base::SysUTF16ToNSString(
      space.icon.empty() ? space.name
                         : base::StrCat({space.icon, u"  ", space.name}));
}

}  // namespace

@implementation KoanMenuTarget {
  NSMenu* _spacesMenu;
  id _trackingObserver;
  id _windowObserver;
  NSMenu* _mainMenu;
}

+ (KoanMenuTarget*)shared {
  static KoanMenuTarget* shared = [[KoanMenuTarget alloc] init];
  return shared;
}

- (void)perform:(NSMenuItem*)item {
  if (koan::KoanBrowserViewController* controller = ActiveController()) {
    controller->shortcuts()->Run(static_cast<Action>(item.tag));
  }
}

- (void)performClose:(id)sender {
  NSWindow* window = WindowForPerformClose();
  if (BrowserWindowInterface* browser = BrowserOfWindow(window)) {
    chrome::ExecuteCommand(browser, IDC_CLOSE_WINDOW);
    return;
  }
  [window performClose:sender];
}

- (void)switchSpace:(NSMenuItem*)item {
  koan::KoanBrowserViewController* controller = ActiveController();
  if (!controller) {
    return;
  }
  const auto spaces = controller->workspaces()->GetSpaces();
  if (item.tag >= 0 && item.tag < static_cast<NSInteger>(spaces.size())) {
    controller->workspaces()->SwitchTo(spaces[item.tag].id);
  }
}

- (void)dockSwitchSpace:(NSMenuItem*)item {
  if (koan::KoanBrowserViewController* controller = ActiveController()) {
    [NSApp activateIgnoringOtherApps:YES];
    controller->browser_view()->browser()->GetWindow()->Activate();
  }
  [self switchSpace:item];
}

- (void)spaceCommand:(NSMenuItem*)item {
  koan::KoanBrowserViewController* controller = ActiveController();
  if (!controller) {
    return;
  }
  koan::KoanWorkspaceController* workspaces = controller->workspaces();
  switch (item.tag) {
    case kEditTheme:
      workspaces->EditActiveSpaceTheme();
      break;
    case kChangeName:
      workspaces->StartRename();
      break;
    case kChangeIcon:
      workspaces->ShowIconPickerForActiveSpace();
      break;
  }
}

- (void)setAppearance:(NSMenuItem*)item {
  if (koan::KoanBrowserViewController* controller = ActiveController()) {
    ThemeServiceFactory::GetForProfile(controller->browser_view()->GetProfile())
        ->SetBrowserColorScheme(static_cast<ColorScheme>(item.tag));
  }
}

// NSMenuItemValidation:
- (BOOL)validateMenuItem:(NSMenuItem*)item {
  if (item.action == @selector(performClose:)) {
    NSWindow* window = WindowForPerformClose();
    if (BrowserWindowInterface* browser = BrowserOfWindow(window)) {
      return chrome::IsCommandEnabled(browser, IDC_CLOSE_WINDOW);
    }
    return window && (window.styleMask & NSWindowStyleMaskClosable);
  }
  koan::KoanBrowserViewController* controller = ActiveController();
  if (!controller) {
    return NO;
  }
  if (item.action == @selector(perform:)) {
    return controller->shortcuts()->CanRun(static_cast<Action>(item.tag));
  }
  if (item.action == @selector(setAppearance:)) {
    const ColorScheme current = ThemeServiceFactory::GetForProfile(
                                    controller->browser_view()->GetProfile())
                                    ->GetBrowserColorScheme();
    item.state = current == static_cast<ColorScheme>(item.tag)
                     ? NSControlStateValueOn
                     : NSControlStateValueOff;
    return YES;
  }
  if (item.action == @selector(spaceCommand:)) {
    return controller->workspaces()->IsEnabled() &&
           controller->workspaces()->GetActiveSpace().has_value();
  }
  return controller->workspaces()->IsEnabled();
}

- (void)setSpacesMenu:(NSMenu*)menu {
  _spacesMenu = menu;
  menu.delegate = self;
  // The items' states are set in -menuNeedsUpdate:, with the Spaces.
  menu.autoenablesItems = NO;
}

// NSMenuDelegate: the Spaces menu enables its items for the active window.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  if (menu != _spacesMenu) {
    return;
  }
  koan::KoanBrowserViewController* controller = ActiveController();
  for (NSMenuItem* item in menu.itemArray) {
    if (item.action == @selector(perform:)) {
      item.enabled = controller && controller->shortcuts()->CanRun(
                                       static_cast<Action>(item.tag));
    } else if (item.action == @selector(spaceCommand:)) {
      item.enabled = controller && controller->workspaces()->IsEnabled() &&
                     controller->workspaces()->GetActiveSpace().has_value();
    }
  }
}

// Gives each Koan item the key of its shortcut as the active window has it,
// and Chrome's items the key they are left with: the Koan key for those that
// do what a Koan shortcut does (Save Page ...), and none for those that
// share a key with another Koan shortcut (Hide Others, Bookmark All Tabs).
// Koan's key wins over Chrome's menu in the window (the FocusManager runs
// first), so this only keeps the menus telling the truth.
- (void)refreshKeysIn:(NSMenu*)mainMenu {
  // Give back what an earlier refresh took.
  NSMapTable<NSMenuItem*, NSArray*>* changed = ChangedItems();
  for (NSMenuItem* item in changed.keyEnumerator.allObjects) {
    NSArray* original = [changed objectForKey:item];
    item.keyEquivalent = original[0];
    item.keyEquivalentModifierMask = [original[1] unsignedIntegerValue];
  }
  [changed removeAllObjects];

  koan::KoanBrowserViewController* controller = ActiveController();
  const std::vector<Binding> bindings =
      controller ? controller->shortcuts()->bindings()
                 : koan::shortcuts::DefaultBindings();
  const auto key_of = [&](Action action) -> std::optional<ui::Accelerator> {
    for (const Binding& binding : bindings) {
      if (binding.action == action) {
        return binding.accelerator;
      }
    }
    return std::nullopt;
  };

  NSMutableArray<NSMenuItem*>* items = [NSMutableArray array];
  CollectItems(mainMenu, items);
  NSMutableSet<NSMenuItem*>* claimed = [NSMutableSet set];
  for (NSMenuItem* item in items) {
    if (item.target == self && item.action == @selector(perform:)) {
      // Zen's Create Space has no key; Koan's still works from the window.
      SetKey(item, static_cast<Action>(item.tag) == Action::kNewWorkspace
                       ? std::optional<ui::Accelerator>()
                       : key_of(static_cast<Action>(item.tag)));
      [claimed addObject:item];
    }
  }
  // Without a window the defaults decide, so the menus are right before the
  // first window is active (the Mac bench found them stale otherwise).
  for (NSMenuItem* item in items) {
    for (const Relocated& relocated : kRelocated) {
      if (IsChromeCommand(item, relocated.command)) {
        RememberKey(item);
        SetKey(item, key_of(relocated.action));
        [claimed addObject:item];
      }
    }
  }
  std::vector<MenuKey> koan_keys;
  for (const Binding& binding : bindings) {
    // The Library's History keys (Cmd+Y ...) only apply with a Library:
    // elsewhere Chrome's items (Show Full History, Home) keep their keys and
    // run.
    if (binding.action != Action::kLibraryHistory) {
      if (std::optional<MenuKey> key = MenuKeyFor(binding.accelerator)) {
        koan_keys.push_back(*key);
      }
    }
  }
  for (NSMenuItem* item in items) {
    // Koan's own items (the Spaces list's Ctrl+1... too) keep theirs.
    if ([claimed containsObject:item] || item.target == self ||
        item.keyEquivalent.length == 0) {
      continue;
    }
    for (const MenuKey& key : koan_keys) {
      if ([item.keyEquivalent isEqualToString:key.key_equivalent] &&
          item.keyEquivalentModifierMask == key.modifier_mask) {
        RememberKey(item);
        item.keyEquivalent = @"";
        item.keyEquivalentModifierMask = 0;
        break;
      }
    }
  }
  // Firefox's keys that Chrome has on others: View Source is Cmd+U (Chrome's
  // Cmd+Option+U is Koan's Unsplit) and the bookmarks library Cmd+Shift+O.
  for (NSMenuItem* item in items) {
    for (const FixedKey& fixed : kFixedKeys) {
      if (IsChromeCommand(item, fixed.command)) {
        RememberKey(item);
        SetKey(item, ui::Accelerator(fixed.key, fixed.modifiers));
      }
    }
  }
}

- (void)refreshMainMenu {
  if (_mainMenu && NSApp.mainMenu == _mainMenu) {
    [self refreshKeysIn:_mainMenu];
  }
}

- (void)observeMenuBar:(NSMenu*)mainMenu {
  _mainMenu = mainMenu;
  if (_trackingObserver) {
    [[NSNotificationCenter defaultCenter] removeObserver:_trackingObserver];
  }
  __weak KoanMenuTarget* weak_self = self;
  _trackingObserver = [[NSNotificationCenter defaultCenter]
      addObserverForName:NSMenuDidBeginTrackingNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                // Only for the menu bar, whichever of its menus is tracked.
                NSMenu* root = notification.object;
                while (root.supermenu) {
                  root = root.supermenu;
                }
                if (root == mainMenu) {
                  [weak_self refreshKeysIn:mainMenu];
                }
              }];
  // And when another window becomes the main one (its profile may have other
  // shortcuts), in case the tracking notification doesn't reach the menu bar.
  if (_windowObserver) {
    [[NSNotificationCenter defaultCenter] removeObserver:_windowObserver];
  }
  _windowObserver = [[NSNotificationCenter defaultCenter]
      addObserverForName:NSWindowDidBecomeMainNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                // After the window became the last active browser.
                dispatch_async(dispatch_get_main_queue(), ^{
                  [weak_self refreshKeysIn:mainMenu];
                });
              }];
}

@end

namespace koan {

void ExtendMainMenu(NSMenu* main_menu, bool is_pwa) {
  if (is_pwa) {
    return;
  }
  KoanMenuTarget* target = [KoanMenuTarget shared];

  // Zen's menu bar (Zen 1.24b: File Edit View Spaces History Bookmarks Tools
  // Window Help). Chrome's items that Zen doesn't have are hidden, not
  // removed: AppController and the Tab, Profiles and History bridges find
  // their items by tag and CHECK that they exist.
  HideItems(main_menu, {IDC_PROFILE_MAIN_MENU, IDC_TAB_MENU});

  // App menu: Zen's Settings… (Cmd+,) opens Koan's settings, so the key moves
  // from Chrome's Preferences…, which opens chrome://settings and is hidden.
  if (NSMenu* app = [main_menu itemWithTag:IDC_CHROME_MENU].submenu) {
    HideItems(app, {IDC_OPTIONS});
    InsertAfter(app, IDC_OPTIONS,
                @[ ActionItem(Action::kOpenSettings, target, @"Settings…") ]);
  }

  // File: Zen's New Blank Window (Cmd+Shift+N, a window without Spaces or
  // window sync) after New Window, and New Private Window.
  if (NSMenu* file = [main_menu itemWithTag:IDC_FILE_MENU].submenu) {
    for (NSMenuItem* item in file.itemArray) {
      if (IsChromeCommand(item, IDC_NEW_INCOGNITO_WINDOW)) {
        item.title = @"New Private Window";
      }
    }
    HideItems(file, {IDC_NEW_ISOLATED_WINDOW});
    InsertAfter(file, IDC_NEW_WINDOW,
                @[ ActionItem(Action::kNewBlankWindow, target) ]);
    // Close Window (⇧⌘W) sends -performClose: to the key window, which AppKit
    // validates and runs against the window's own close button. Koan hides
    // and fades AppKit's traffic lights (koan_traffic_lights_mac.mm), and on
    // the Mac the item was disabled in Koan's windows, its key doing
    // nothing. So Koan's target takes it: Chrome's Close Window command for
    // a browser window (what the red button does), AppKit's path for any
    // other. Only Chrome's own Close Window item, the ⇧⌘W one:
    // AppController sets its title, tag and action again
    // (-updateMenuItemKeyEquivalents, which for a window without tabs hides
    // it and turns ⌘W's item into Close Window, with no target) but never
    // its target.
    for (NSMenuItem* item in file.itemArray) {
      if (item.tag == IDC_CLOSE_WINDOW &&
          item.action == @selector(performClose:)) {
        item.target = target;
      }
    }
  }

  // View: Zen's starts with Appearance > Automatic / Light / Dark. Chrome's
  // toolbar, URL, AI, Lens, search and vertical tab items, Cast and
  // Developer (Browser Tools in Tools) are not Zen's. Always Show Bookmarks
  // Bar (⇧⌘B, Firefox's View > Toolbars > Bookmarks Toolbar in Zen) stays:
  // it switches Koan's bookmarks bar setting (koan_command_policy.cc).
  if (NSMenu* view = [main_menu itemWithTag:IDC_VIEW_MENU].submenu) {
    HideItems(view,
              {IDC_TOGGLE_FULLSCREEN_TOOLBAR, IDC_SHOW_FULL_URLS,
               IDC_SHOW_GOOGLE_LENS_SHORTCUT, IDC_SHOW_AI_MODE_OMNIBOX_BUTTON,
               IDC_SHOW_SEARCH_TOOLS, IDC_TOGGLE_VERTICAL_TABS,
               IDC_CUSTOMIZE_TOUCH_BAR, IDC_ROUTE_MEDIA, IDC_DEVELOPER_MENU});
    // Chrome's "Collapse Vertical Tabs" stays: upstream shows it whenever the
    // tabs are vertical (a hidden item's key wouldn't fire either), and it is
    // Zen's "Toggle Sidebar's Width" (zen-sidebar-shortcut-toggle), which Zen
    // leaves without a key: Chrome's Shift+Cmd+L is Koan's inspector.
    for (NSMenuItem* item in view.itemArray) {
      if (IsChromeCommand(item, IDC_TOGGLE_VERTICAL_TABS_COLLAPSE)) {
        item.title = @"Toggle Sidebar's Width";
      }
    }
    NSMenu* appearance = [[NSMenu alloc] initWithTitle:@"Appearance"];
    // Zen's first, disabled item (zen-menubar-appearance-description).
    NSMenuItem* websites =
        [[NSMenuItem alloc] initWithTitle:@"Websites will use:"
                                   action:nil
                            keyEquivalent:@""];
    websites.enabled = NO;
    [appearance addItem:websites];
    const struct {
      NSString* title;
      ColorScheme scheme;
    } kSchemes[] = {
        {@"Automatic", ColorScheme::kSystem},
        {@"Light", ColorScheme::kLight},
        {@"Dark", ColorScheme::kDark},
    };
    for (const auto& entry : kSchemes) {
      NSMenuItem* item =
          [[NSMenuItem alloc] initWithTitle:entry.title
                                     action:@selector(setAppearance:)
                              keyEquivalent:@""];
      item.target = target;
      item.tag = static_cast<NSInteger>(entry.scheme);
      [appearance addItem:item];
    }
    NSMenuItem* appearance_item =
        [[NSMenuItem alloc] initWithTitle:@"Appearance"
                                   action:nil
                            keyEquivalent:@""];
    appearance_item.submenu = appearance;
    [view insertItem:appearance_item atIndex:0];
    [view insertItem:[NSMenuItem separatorItem] atIndex:1];
  }

  // Spaces, after View as in Zen 1.24b (ZenMenubar.mjs #initSpacesMenu):
  // Create Space, Edit Theme…, Change Name…, Change Icon…, Next Space,
  // Previous Space.
  NSMenu* spaces = [[NSMenu alloc] initWithTitle:@"Spaces"];
  [spaces addItem:ActionItem(Action::kNewWorkspace, target, @"Create Space")];
  const struct {
    NSString* title;
    SpaceCommand command;
  } kSpaceCommands[] = {
      {@"Edit Theme…", kEditTheme},
      {@"Change Name…", kChangeName},
      {@"Change Icon…", kChangeIcon},
  };
  for (const auto& entry : kSpaceCommands) {
    NSMenuItem* item =
        [[NSMenuItem alloc] initWithTitle:entry.title
                                   action:@selector(spaceCommand:)
                            keyEquivalent:@""];
    item.target = target;
    item.tag = entry.command;
    [spaces addItem:item];
  }
  [spaces addItem:[NSMenuItem separatorItem]];
  [spaces addItem:ActionItem(Action::kWorkspaceForward, target, @"Next Space")];
  [spaces addItem:ActionItem(Action::kWorkspaceBackward, target,
                             @"Previous Space")];
  [target setSpacesMenu:spaces];
  NSMenuItem* spaces_item = [[NSMenuItem alloc] initWithTitle:@"Spaces"
                                                       action:nil
                                                keyEquivalent:@""];
  spaces_item.submenu = spaces;
  const NSInteger view_index = [main_menu indexOfItemWithTag:IDC_VIEW_MENU];
  [main_menu
      insertItem:spaces_item
         atIndex:view_index == -1 ? main_menu.numberOfItems : view_index + 1];

  // Tools, before the hidden Profiles menu (after Bookmarks) as in Zen:
  // Downloads, Extensions, Browser Tools. Downloads moves here from Window.
  NSMenu* tools = [[NSMenu alloc] initWithTitle:@"Tools"];
  [tools addItem:ChromeItem(@"Downloads", IDC_SHOW_DOWNLOADS)];
  [tools addItem:ChromeItem(@"Extensions", IDC_MANAGE_EXTENSIONS)];
  [tools addItem:[NSMenuItem separatorItem]];
  NSMenu* browser_tools = [[NSMenu alloc] initWithTitle:@"Browser Tools"];
  [browser_tools addItem:ChromeItem(@"Developer Tools", IDC_DEV_TOOLS)];
  [browser_tools
      addItem:ChromeItem(@"JavaScript Console", IDC_DEV_TOOLS_CONSOLE)];
  [browser_tools addItem:ChromeItem(@"View Source", IDC_VIEW_SOURCE)];
  [browser_tools
      addItem:ChromeItem(@"Task Manager", IDC_TASK_MANAGER_MAIN_MENU)];
  NSMenuItem* browser_tools_item =
      [[NSMenuItem alloc] initWithTitle:@"Browser Tools"
                                 action:nil
                          keyEquivalent:@""];
  browser_tools_item.submenu = browser_tools;
  [tools addItem:browser_tools_item];
  NSMenuItem* tools_item = [[NSMenuItem alloc] initWithTitle:@"Tools"
                                                      action:nil
                                               keyEquivalent:@""];
  tools_item.submenu = tools;
  const NSInteger profiles_index =
      [main_menu indexOfItemWithTag:IDC_PROFILE_MAIN_MENU];
  [main_menu insertItem:tools_item
                atIndex:profiles_index == -1 ? main_menu.numberOfItems
                                             : profiles_index];
  if (NSMenu* window = [main_menu itemWithTag:IDC_WINDOW_MENU].submenu) {
    HideItems(window, {IDC_SHOW_DOWNLOADS, IDC_MANAGE_EXTENSIONS});
  }

  // Firefox's keys that Zen leaves unbound (browser-sets-inc-xhtml.patch):
  // Open File (Cmd+O, which is Expand Glance's) and Bookmark All Tabs.
  NSMutableArray<NSMenuItem*>* all_items = [NSMutableArray array];
  CollectItems(main_menu, all_items);
  for (NSMenuItem* item in all_items) {
    if (IsChromeCommand(item, IDC_OPEN_FILE) ||
        IsChromeCommand(item, IDC_BOOKMARK_ALL_TABS)) {
      item.keyEquivalent = @"";
      item.keyEquivalentModifierMask = 0;
    }
  }

  [target refreshKeysIn:main_menu];
  [target observeMenuBar:main_menu];
}

void ExtendDockMenu(NSMenu* dock_menu) {
  for (NSMenuItem* item in dock_menu.itemArray) {
    if (item.tag == IDC_NEW_INCOGNITO_WINDOW) {
      item.title = @"New Private Window";
    }
  }
  // The Spaces of the window that was active last, to switch to; the Dock
  // menu has no window to act on otherwise.
  KoanBrowserViewController* controller = ActiveController();
  if (!controller || !controller->workspaces()->IsEnabled()) {
    return;
  }
  const auto spaces = controller->workspaces()->GetSpaces();
  if (spaces.empty()) {
    return;
  }
  [dock_menu addItem:[NSMenuItem separatorItem]];
  for (size_t i = 0; i < spaces.size(); ++i) {
    NSMenuItem* item =
        [[NSMenuItem alloc] initWithTitle:SpaceTitle(spaces[i])
                                   action:@selector(dockSwitchSpace:)
                            keyEquivalent:@""];
    item.target = [KoanMenuTarget shared];
    item.tag = static_cast<NSInteger>(i);
    item.state =
        spaces[i].active ? NSControlStateValueOn : NSControlStateValueOff;
    [dock_menu addItem:item];
  }
}

bool HidesProfilesMenu() {
  return features::IsSidebarEnabled();
}

void RefreshMainMenuKeys() {
  [[KoanMenuTarget shared] refreshMainMenu];
}

std::vector<ui::Accelerator> ChromiumMacAccelerators() {
  std::vector<ui::Accelerator> accelerators;
  AcceleratorsCocoa* menu_keys = AcceleratorsCocoa::GetInstance();
  for (auto it = menu_keys->begin(); it != menu_keys->end(); ++it) {
    accelerators.push_back(it->second);
  }
  for (const KeyboardShortcutData& shortcut :
       GetShortcutsNotPresentInMainMenu()) {
    int modifiers = 0;
    if (shortcut.command_key) {
      modifiers |= ui::EF_COMMAND_DOWN;
    }
    if (shortcut.shift_key) {
      modifiers |= ui::EF_SHIFT_DOWN;
    }
    if (shortcut.cntrl_key) {
      modifiers |= ui::EF_CONTROL_DOWN;
    }
    if (shortcut.opt_key) {
      modifiers |= ui::EF_ALT_DOWN;
    }
    accelerators.emplace_back(ui::KeyboardCodeFromKeyCode(shortcut.vkey_code),
                              modifiers);
  }
  return accelerators;
}

}  // namespace koan
