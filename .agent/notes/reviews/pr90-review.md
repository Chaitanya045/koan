# Review of PR #90 (draft): macOS Zen shortcuts, menu bar and Dock menu (#71)

Branch `feat/mac-shortcuts-menu` at 5434c11. Read-only review against Chromium
`67e29b82` (CHROMIUM_VERSION line 2, `~/work/koan/chromium/src` HEAD). Nothing was built or run.

Counts: **1 Blocker, 3 Should-fix, 5 Nit.**

Compile verdict: the Mac sources very likely compile (details in section 1). The Blocker is a
**start-up crash**, not a compile error.

---

## Blocker

### B1. "Toggle Library" in the History menu hits a `NOTREACHED()` in Chrome's `HistoryMenuBridge`

- `src/browser/ui/cocoa/koan_main_menu_mac.mm:499-501` inserts Koan's item into the History menu.
  `ActionItem()` (`:144-153`) tags it `static_cast<NSInteger>(Action::kLibrary)`, which is **29**.
- `chrome/browser/ui/cocoa/history_menu_bridge.mm:717-746`: `SetVisibilityOfMenuItems()` walks every
  item of the History menu and calls `ShouldMenuItemBeVisible(item)`. That function is a `switch`
  over the tag whose only cases are `IDC_HOME`, `IDC_BACK`, `IDC_FORWARD`, `kRecentlyClosed*`,
  `kVisited*`, `kShowFullSeparator` and `IDC_SHOW_HISTORY`. Anything else falls through to
  `NOTREACHED();` (line 745). The comment above it says "When a new menu item is introduced, it
  should be added to one of the cases above."
- In this Chromium `NOTREACHED()` with no `NotFatalUntil` argument is fatal (`base/notreached.h`:
  `NotReachedNoreturnError`).
- It runs from the `HistoryMenuBridge` constructor (`history_menu_bridge.mm:90`). `AppController`
  creates one on every profile change (`app_controller_mac.mm:2323`), so the browser crashes when
  the first window becomes active. The item is added to the live main menu, which is what
  `HistoryMenu()` reads (`history_menu_bridge.mm:264-266`).
- Fix, either of:
  - put "Toggle Library" in a menu Chrome doesn't audit (View, or the Koan-owned Spaces menu); or
  - add a hook patch to `ShouldMenuItemBeVisible` (a new row in REBASE.md). A tag of 400 or more
    that the bridge ignores would not work: the `switch` has no default.
- `docs/REBASE.md` (the "macOS menu bar" row) doesn't list this dependency, which is how it was
  missed. The other menus Koan extends (App, File, View, Tab) were checked and have no such
  audit: Tab's `TabMenuBridge` finds its items by `item.target`, and `app_controller_mac.mm:1173`
  looks items up by `IDC_*` tag only.

---

## Should-fix

### S1. THIRD_PARTY_NOTICES.md has no entry for the new MPL file (CONTRIBUTING.md:200-207)
`src/browser/ui/cocoa/koan_main_menu_mac.mm` carries the MPL header (`:1-11`, correctly formed)
but `THIRD_PARTY_NOTICES.md` (table at lines 26-35) isn't in the PR's file list, and
`grep koan_main_menu THIRD_PARTY_NOTICES.md` finds nothing. CONTRIBUTING says "list it in
THIRD_PARTY_NOTICES.md". The header (`:7-9`) lists `zen-menubar.ftl`, but no string from that
file is used (it has Appearance, Collapse Pinned Tabs, New Blank Window; the Koan code uses
`zen-workspaces.ftl` strings only: Spaces, Create Space, Next/Previous Space, Move To). Drop that
line from the header and the notice.

### S2. Holding Cmd+Shift+N opens windows repeatedly
`prePerformKeyEquivalent` (`chrome_command_dispatcher_delegate.mm:68-110`) runs the high-priority
Views accelerators first, and only afterwards the `isARepeat` drop for `IDC_NEW_WINDOW` /
`IDC_NEW_INCOGNITO_WINDOW` (`:98-102`). Making Cmd+Shift+N high priority (new behaviour in this
PR, `koan_shortcuts.cc:99-102` via `IsReservedOnMac`) means Chrome's repeat protection no longer
applies. `AcceleratorPressed` guards repeats for `kNewTab` only (`koan_shortcuts.cc:140`). Add
`kNewWindow` (and any other action that opens a window) to that guard.

### S3. `gn check` gap in `src/browser/ui/cocoa/BUILD.gn:19-37`
`koan_main_menu_mac.mm:30` includes `chrome/browser/ui/views/frame/browser_view.h`. That header
belongs to `//chrome/browser/ui/views/frame:browser_view_header` (`frame/BUILD.gn:710-711`). The
`:frame` target that `:impl` lists (line 29) only has `tab_strip_region_view.*` and doesn't
public-dep it. Compiling is unaffected (flat include paths) but `gn check` will flag it. Koan's own
`views:impl` lists `:browser_view_header`. Add it, and drop `:frame` if nothing else needs it.

---

## Nit

- N1. `koan_settings.js:193-215` now carries Zen's macOS glyph logic (`toDisplayString` and
  `keyToDisplayString` in `ZenKeyboardShortcuts.mjs`), but its MPL header and its notice row list
  only `zen-preferences.ftl (strings)`. Add `ZenKeyboardShortcuts.mjs` to both.
- N2. `koan_main_menu_mac.mm:51` uses `GetLastActiveBrowserWindowInterfaceWithAnyProfile()`, which
  its header marks DEPRECATED in favour of
  `GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser()`. `koan_workspaces.cc:32` already
  uses the same call, so this is consistent but worth one sweep.
- N3. No unit tests for the new pure functions (`IsReservedOnMac`, `IsSystemShortcutOnMac`,
  `DisplayText(accel, macos)`). The `bool macos` overload exists only "for tests"
  (`koan_shortcut_table.h`), and nothing calls it with `true`. The Linux e2e only asserts
  `mac === false` (`scripts/e2e/settings.mjs`).
- N4. `IsSystemShortcutOnMac` (`koan_shortcut_table.cc:248-270`) is missing a few system keys
  (for example Cmd+Shift+Q Log Out, Ctrl+Cmd+F Full Screen). Not verified against a Mac, so
  left as a note.
- N5. Zen's File > "New Blank Window" (Cmd+Shift+N, `ZenMenubar.mjs` `#initAppMenu`) has no menu
  item, so Cmd+Shift+N appears nowhere in the menu bar. Already stated in `ZEN_PARITY.md`, listed
  here only for completeness.

---

## 1. Will it compile on macOS?

No definite compile failure found. Traced against the pinned tree.

**Hook declarations and patches**
- `main_menu_builder.mm` hook: the call is at the end of `BuildMainMenu`, `main_menu` and `is_pwa`
  exist there. The `#include` sits in the right sorted spot. `koan::` resolves from inside
  `namespace chrome`.
- `app_controller_mac.mm` hooks: `dockMenu` is the local `NSMenu*`; both returns are covered
  (`:2171` is the `!profile` return, `:2199` the final one). `applicationDockMenu:` builds a fresh
  menu each call, so adding items is safe.
- GN: `main_menu_builder.mm` is in `//chrome/browser/ui/cocoa:cocoa` and gets `:hooks`.
  `app_controller_mac.mm` is in `//chrome/browser:core` under `if (is_mac)` (`BUILD.gn:1733`) and
  gets `:hooks`. `:impl` is added to `source_set("browser")` under `if (is_mac)` (`:4044`). The
  hunk offsets in `patches/chrome/browser/BUILD.gn.patch` are consistent (+4, +5, +6). The
  `assert(is_mac)` in `src/browser/ui/cocoa/BUILD.gn` is only reached from Mac-conditional
  references (`webui/BUILD.gn` uses `if (is_mac)`).
- `:hooks` is header-only and its `public_deps` cover `ui/base/accelerators/accelerator.h`.
  `ChromiumMacAccelerators()` is declared outside the `__OBJC__` guard so the plain C++
  `koan_settings_handler.cc` can call it. It is defined in `:impl`, linked through `browser`.

**API checks against Chromium (all present with matching signatures)**
- `ui::GetKeyEquivalentAndModifierMaskFromAccelerator` and `KeyEquivalentAndModifierMask`
  (`ui/base/accelerators/platform_accelerator_cocoa.h`), `ui::MacKeyCodeForWindowsKeyCode`
  (`flags` is `NSUInteger`, so `0` is fine) and `ui::KeyboardCodeFromKeyCode(unsigned short)`.
- `AcceleratorsCocoa::GetInstance()`, `begin()`/`end()` (non-const members, called on a pointer),
  `GetShortcutsNotPresentInMainMenu()` (declared in `global_keyboard_shortcuts_mac.h`, defined
  outside any anonymous namespace), `KeyboardShortcutData` field names.
- `IDC_CHROME_MENU`, `IDC_FILE_MENU`, `IDC_VIEW_MENU`, `IDC_HISTORY_MENU`, `IDC_TAB_MENU`,
  `IDC_OPTIONS`, `IDC_TOGGLE_VERTICAL_TABS_COLLAPSE`, `IDC_SHOW_HISTORY`, `IDC_NEW_SPLIT_TAB`,
  `IDC_SAVE_PAGE`, `IDC_NEW_INCOGNITO_WINDOW`, `IDC_DEV_TOOLS_INSPECT`, `IDC_WINDOW_MUTE_SITE`,
  `IDC_SHOW_DOWNLOADS`. All exist, and those used by `ExtendMainMenu` are menu-builder tags or
  command ids (`main_menu_builder.mm:52,121,294,375,519`; items at 156, 314, 355, 402, 486, 538,
  566).
- `BrowserView::GetBrowserViewForBrowser(const BrowserWindowInterface*)`, `GetIsNormalType()`
  (public), `BrowserView::browser()` returns `BrowserWindowInterface*`, so `GetTabStripModel()` and
  `GetWindow()->Activate()` (`ui/base/base_window.h` is included) are valid.
  `selection_model().GetListSelectionModel().selected_indices()` is a `base::flat_set<size_t>`.
  `TabStripModel::GetTabAtIndex(int)` and `GetActiveTab()` exist.
- Koan side: `KoanBrowserViewController::From/shortcuts/workspaces/browser_view`,
  `KoanWorkspaceController::SpaceInfo{id,name,icon,active}`, `GetSpaces`, `SwitchTo`,
  `MoveTabsToSpace(const std::vector<tabs::TabInterface*>&, const TabGroupId&)`, `IsEnabled`,
  `KoanShortcutController::{Run,CanRun,bindings,GetAccelerator}` and every `Action` enumerator used.
- Language details I checked: `const std::vector<Binding> b = c ? ref : DefaultBindings();` is
  well-formed (copy of the const lvalue). `@selector()` of undeclared selectors is not an error
  here (`-Wundeclared-selector` is iOS-only in `build/config/compiler/BUILD.gn:1917`).
  `NSMenuItemValidation`, `NSMapTable<K,V>`, `__weak` in a block and ARC pointers in
  `struct MenuKey` / `std::optional` are fine under Chromium's ARC. `[NSApp
  activateIgnoringOtherApps:]` is also used by `app_controller_mac.mm:2140`. The local
  `std::vector<tabs::TabInterface*> tabs;` is fine (the type is looked up before the name is
  declared).
- Shared (Linux-compiled) code additions in `koan_shortcut_table.cc` and `koan_shortcuts.cc` are
  unchanged-semantics C++ on Linux. In `koan_settings_handler.cc`, `!!BUILDFLAG(IS_MAC)` is a
  `bool` and `build/build_config.h` is now included.

**Risks I could not close without a Mac**: the GN dependency graph for `:impl` (it depends on
`//chrome/browser/ui/cocoa` and the Koan views `:impl`, both already linked into `browser`, so no
new cycle is expected); and `NSMenuDidBeginTrackingNotification`'s `object` for the menu bar (the
walk up `supermenu` handles either a submenu or the main menu).

## 2. Behaviour

**Defaults vs Zen (macOS), glyph for glyph**: they match.
- `accel` is Cmd and `kAccel` is `EF_PLATFORM_ACCELERATOR` (`ZenKeyboardShortcuts.mjs:735-860,
  1040-1230`). Mute stays Control+M (`browser.xhtml:333`, modifiers `control`); Downloads is
  Cmd+Shift+J (`:323-326`); Inspect is Cmd+Alt+L (Zen moves `key_inspectorMac`/`key_inspector` to
  "L", `:888-900`); Save Page is Cmd+Alt+Shift+S (`:735-747`); Ctrl+1..9,0 are Space 1..10
  (`:777-790`); private window is Cmd+Shift+P (`browser.xhtml:434-435`); Library is Alt+Shift+L.
- Display notation: modifier order is Control, Cmd, Alt, Shift, space-separated, then the key
  (`ZenKeyboardShortcuts.mjs:207-232`). Koan's C++ and JS give the same order. Space is `␣`,
  Escape `⎋`, Enter `↩`; arrows are `←↑→↓` on every platform (`:608-650`).

**Collisions** (checked against `accelerators_cocoa.mm` and `global_keyboard_shortcuts_mac.mm`):
- Taken from Chrome, and documented in USAGE: Cmd+S (Save Page, moved), Cmd+Shift+D (Bookmark All
  Tabs), Cmd+Shift+N (Incognito, moved to Cmd+Shift+P), Cmd+Option+H (Hide Others), Cmd+Option+U
  (View Source), Cmd+Shift+C (hidden inspect), Cmd+Option+L (hidden Downloads), Cmd+Option+Left/Right
  (tab switching), Cmd+, (Preferences). `refreshKeysIn:` clears the matching key on Chrome's items
  only when a Koan window is the last active one.
- Cmd+Option+R (reader) coincides with Chrome's hidden `IDC_SHOW_READING_MODE_KEYBOARD`; harmless
  (same intent), but the USAGE table doesn't mention it.
- No default collides with a macOS system key in `IsSystemShortcutOnMac`. Ctrl+1..0 coincide with
  "Switch to Desktop N" (off by default), noted in USAGE.

**Precedence**: correct. For normal-priority accelerators, `postPerformKeyEquivalent`
(`chrome_command_dispatcher_delegate.mm:115-125`) runs the Views `FocusManager` **before** it hands
a main-menu match back with `kPassToMainMenu`, so Koan beats the menu for non-reserved keys. For the
keys Chrome treats as reserved, `prePerformKeyEquivalent` executes them directly. High-priority
registration comes first there (`:68-74`), so registering Koan's shortcut on `IsReservedOnMac()`
keys at high priority is the right fix. `IsReservedOnMac` covers the Mac keys of
`kReservedCommandIds` (`accelerator_table.cc:344-358`): Close Tab/Window, New Window/Incognito/Tab,
Reopen Tab, Next/Previous Tab (all three key sets), Ctrl+Tab cycling, Quit. When a Koan shortcut
declines (for example one Space), the event goes on to Chrome's own handler. Verified by reading:
`Run` returns false and the hidden-shortcut table still maps Cmd+Option+arrows.
One consequence is S2 above.

**Menu bar wiring**: each Koan item runs `KoanShortcutController::Run(action)`, so it does exactly
what the key does (the actions map to `IDC_SAVE_PAGE`, `IDC_SHOW_HISTORY`, `IDC_DEV_TOOLS_INSPECT`,
`IDC_NEW_WINDOW`, `IDC_NEW_INCOGNITO_WINDOW`, `IDC_WINDOW_MUTE_SITE`, `IDC_SHOW_BOOKMARK_SIDE_PANEL`,
`IDC_SHOW_DOWNLOADS`, `IDC_SHOW_READING_MODE`; `koan_shortcuts.cc:300-336`). `Relocated`
(`.mm:115-121`) only changes keys of Chrome items that already do the same thing. `CanRun`
mirrors `Run`'s preconditions. Spaces menu index tags are re-derived at click time, so a stale menu
can't select the wrong Space; it does re-read `ActiveController()` then.
New items use their own `perform:` / `switchSpace:` actions with `target = KoanMenuTarget`, so
`MenuCommandForKeyEvent` (`global_keyboard_shortcuts_mac.mm:55-60`: only `commandDispatch:` items
count) ignores them, and Chrome's key handling is unaffected.

**Dock menu**: correct. "New Window" stays, the incognito item is renamed by its tag, the Spaces of
the last active window are appended (✓ on the active one), `dockSwitchSpace:` activates the app and
the window first. Nothing is added when no window exists.

## 3. Linux

Unaffected. All Mac code is in Mac-only files or behind `BUILDFLAG(IS_MAC)` / `if (is_mac)`
(`koan_shortcuts.cc` Mac block, `koan_shortcut_table.cc` `#if BUILDFLAG(IS_MAC)` blocks,
`koan_settings_handler.cc` include and loop, `webui/BUILD.gn`, all three `BUILD.gn` patch hunks).
`kAccel` equals `kCtrl` on Linux, so the Linux key set is identical. Inspect and Downloads moved
into an `#else` branch and now come last in the vector instead of in the middle, which has no
effect because no key is duplicated. Linux notation (`Ctrl+Win+Alt+⇧+Key`) is unchanged in both
C++ and JS. Hook patches are minimal: one include plus one call in `main_menu_builder.mm`; one
include plus two calls in `app_controller_mac.mm`; GN deps in `ui/cocoa/BUILD.gn` and
`chrome/browser/BUILD.gn`. They are listed in `docs/REBASE.md` (two new rows; they omit the
History-menu dependency, see B1).

## 4. Licence headers

- `koan_main_menu_mac.mm`: MPL header present and in CONTRIBUTING's form (see S1 for the notice
  entry and the unused `zen-menubar.ftl` line).
- `koan_shortcut_table.cc` (MPL, already listed): the new Mac glyph table is in the same file, OK.
- `koan_settings.js` (MPL): see N1.
- `koan_main_menu.h`, `BUILD.gn`s, `koan_shortcuts.{cc,h}`, `koan_settings_handler.cc`: MIT
  headers; they contain declarations and wiring, not ported Zen material, so MIT is right.
