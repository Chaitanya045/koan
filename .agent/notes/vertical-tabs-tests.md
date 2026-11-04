# Vertical tabs lockdown (#154, PR #155): the 26 audit tests, as the Mac loop runs them

Source: `.agent/notes/vertical-tabs-audit.md` section 4 (numbers are the same). Run on a **DCHECK build**, so that `KoanVerticalTabsPolicy`'s leak detector (`DLOG(FATAL)` when the strip's mode changes, or it collapses or expands without Koan asking) can fire; a crash or a `Koan: the vertical tab strip was ...` line in the log fails any test.

Written but **not run** (Mac only, nothing is built on Linux): the e2e scripts compile (`node --check`) and import what they use, nothing else is known. Expect to fix selectors and timings on the first Mac pass; tell the author what changed.

Every check also asserts the common invariant (`scripts/e2e/lib/vertical_tabs.mjs`, `checkKoanSidebar()`): vertical strip, no horizontal `TabStripRegionView`, no icon rail (a strip 1 to 119 wide), no Chromium toolbar drawn (unless `koan.single_toolbar` is false), Koan's Space header inside the region, expanded 163 to 500 or hidden by compact mode with width 0.

## Automated (`scripts/e2e/run.sh <name>`; `KEEP_LOG=<file>` for the leak-detector check)

| Test (audit §4) | Script | Notes |
|---|---|---|
| 1 `vertical_tabs.enabled=false` | `vertical_tabs_old_profile` | seed `prefs/vertical_tabs_old_profile.json`; also reads the pref back (`true`) |
| 2 `collapsed_state=true`, `uncollapsed_width=56` | `vertical_tabs_old_profile` | same seed; expanded at 163..500, pref back to `false` |
| 3 `expand_on_hover=false` + compact on | `vertical_tabs_hover` | the pref is back to `true` and the hot zone reveal works. **The pointer-over-the-sidebar half needs a real pointer**: see Manual M3 |
| 6 Chrome profile (collapsed, on the right) | `vertical_tabs_old_profile` | seed has `side_panel.is_right_aligned=true`, `enabled=false`, `collapsed_state=true`; Koan's own `koan.tabs.on_right` is the only side switch (sidebar stays on the left) |
| 12 chrome://settings writes | `vertical_tabs_old_profile` | writes the three prefs through `chrome.settingsPrivate`: window unchanged, each snaps back |
| 13 Shift+Cmd+L | `vertical_tabs_old_profile`, `vertical_tabs_no_compact` | toggles compact mode, never a rail; also with the URL bar focused. Not covered: with the Library open, in fullscreen (`vertical_tabs_teardown` covers fullscreen on/off, not the key) |
| 14 Shift+Cmd+A | `vertical_tabs_old_profile` | the CDP command `Browser.executeBrowserCommand openTabSearch` is `IDC_TAB_SEARCH`, the same command; expects Koan's URL bar and no `TabSearch*` view |
| 17 drag below the snap width | `vertical_tabs_drag` | also tabs on the right; samples the strip's width during the drag (never the 56 wide rail) |
| 18 drag with compact on, revealed | `vertical_tabs_drag` | peeked with Ctrl+Alt+S |
| 19 keyboard resize | `vertical_tabs_drag` | Left x8 hides with compact mode, Right x8 widens to at most 500 |
| 20 `--disable-features=KoanCompactMode` | `vertical_tabs_no_compact` (`.flags`) | drag snaps back; the sidebar button and Shift+Cmd+L do nothing |
| 21 20 toggles + close mid-animation | `vertical_tabs_teardown` | also closes with the sidebar revealed, pinned, and the Space form open. The menu-open case is Manual M21 |
| 22 fullscreen on/off | `vertical_tabs_teardown` | macOS only (`isMac()`); compact off and on |
| 23 no hint after the collapse | `vertical_tabs_hover` | waits 4 s and 11 s with the real timers. The 10 minute timer (hint while collapsed) isn't waited for: Manual M23 |
| 24 detector silent in the whole suite | all of the above (`leakLogged()`), and the Mac loop for `compact_*`, `sidebar_*`, `spaces_*`, `fullscreen_*`, `window_close` | grep the loop's saved log for `vertical tab strip` |

## Manual or semi-automated (the e2e harness can't do these, say why)

UI DevTools can't read a menu item's label (`MenuItemView` paints its title) and CDP can't run an arbitrary `IDC_*` command, so the menu tests are done by the loop with `osascript` (System Events) and screenshots. Capture each menu with `screencapture -l` per `koan-mac-window-capture`.

- **M4 session with a saved collapse.** Build c129u (the last build before #155) with the profile `~/koan-vt-4`: turn compact on, quit with Cmd+Q (the session saves `vertical_tab_strip_collapsed=true`), then `plutil -replace koan.compact_mode.enabled -bool false <profile>/Default/Preferences`; start the #155 build on the same profile: the window opens expanded, no rail. Then in it: close the window with Cmd+W and Cmd+Shift+T (reopen closed window): expanded again, no rail.
- **M5 crash while compact hides the sidebar.** Compact on, `kill -9` the browser, relaunch, choose "Restore": hidden (strip width 0) without an animation (record the first 1 s with `screencapture -V`/frames: no strip sliding), then Ctrl+S: expanded, no rail.
- **M3 real pointer.** Seed `prefs/vertical_tabs_hover.json`, compact on. With `cliclick` (see `koan-input-grant-and-front-pid`): move to the left edge, then onto the sidebar, rest 3 s: still out. Move off: it hides after about 150 ms. (UI DevTools mouse events don't move the system pointer, which upstream's hover reads.)
- **M7 tab menus.** Right-click, in turn: a tab, an Essential, a pinned tab, a tab in a folder, a multi-selection. Screenshot each: no "Show Tabs Horizontally" item and no orphan separator above where it was (the last item before Koan's group should be the Koan items, or Chromium's last, never a separator at the end).
- **M8 Mac View menu.** `osascript -e 'tell application "System Events" to tell process "Koan" to get {name, enabled} of menu items of menu "View" of menu bar 1'`: neither "Show Tabs Vertically" nor "Collapse Vertical Tabs" (nor "Expand Vertical Tabs"). Repeat in fullscreen, after leaving it, and after switching windows (validation runs again each time). `ValidateUserInterfaceItem` hides them after upstream's switch; if one is back, that is a regression of the `browser_native_widget_mac.mm` hunk.
- **M9 app menu.** Set `koan.single_toolbar` false (Ctrl+Alt+B), open the app menu, More tools: no "Show tabs horizontally/vertically" (the actions are `SetVisible(false)`; in the older menu model the item may remain greyed: report, `app_menu_model.cc` would need a hook, audit A7).
- **M10 sidebar button's menu.** Right-click the button at the left end of the sidebar's top row: Koan's sidebar menu (Compact Mode submenu, Tabs on the right), no "Expand tabs on hover".
- **M11 other context menus.** Right-click the empty sidebar, the Space header, the Space bar: only Koan's menus.
- **M15 direct commands.** Needs a hook to call `chrome::ExecuteCommand(IDC_TOGGLE_VERTICAL_TABS / _EXPAND_ON_HOVER / IDC_VERTICAL_TABS_SEND_FEEDBACK)`; there is none (CDP's `executeBrowserCommand` knows only tab search and Glic). Not covered. Proposed follow-up: a `--koan-test-commands` DevTools method in a Koan-only test build. Meanwhile the three actions are re-pointed in `InstallActions()`, which a `frame/` browser test would assert, and the state controller hook (A1) ignores a mode change whatever route it takes: chrome://settings writing `vertical_tabs.enabled` (test 12) reaches the same function. Repeat test 12 with `--disable-features=UseActionsForBrowserCommands` to see the legacy path go through A1.
- **M16 `TabStripModel::CommandToggleVertical`.** The tab menu no longer has the item (M7). The call itself can't be made from outside; it ends in `SetVerticalTabsEnabled()`, which the hook ignores (same function as M15).
- **M21 menu open while closing.** Open the sidebar's menu (right-click), close the window with Cmd+W from the menu bar's File menu while the menu is still open (or `Target.closeTarget` from CDP): no crash. The menu holds a lock (`menu_lock_`).
- **M23 the 10 minute hint.** Compact on, leave for 11 minutes: no bubble.
- **M25 extensions.** Load a test extension with `chrome.windows.create({type: 'normal'})`: Koan sidebar; `{type: 'popup'}`: no sidebar, no crash. `chrome.tabGroups.update(spaceGroup, {collapsed: true})` and `chrome.tabs.ungroup` on a Space's tabs: Koan's Space state stays consistent (that is the Spaces audit's scope, report what you see).
- **M26 extension shortcut on Shift+Cmd+L.** An extension `commands` entry with `Command+Shift+L`: record which one runs (Chromium lets the browser's accelerator win for reserved ones, an extension's for the others); no rail either way.
