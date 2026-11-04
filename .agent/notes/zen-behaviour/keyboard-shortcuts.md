# Keyboard shortcuts (macOS)

Zen 1.24b. Sources: `src/browser/base/content/zen-keysets.inc.xhtml` (the Zen keyset `zenKeyset`),
`src/browser/base/content/browser-sets-inc-xhtml.patch` (Zen's edits to Firefox's own keys),
`src/zen/common/zen-sets.js` (what each command does), `src/zen/kbs/ZenKeyboardShortcuts.sys.mjs` (the shortcut editor
categories and the legacy migration), `src/browser/components/customkeys/CustomKeys-sys-mjs.patch`,
`locales/en-US/browser/browser/preferences/zen-preferences.ftl` (labels), and Zen's test
`src/zen/tests/kbs/browser_zen_keys.js` (states the expected defaults). Live checks: Space switching, Cmd+Opt+V/H/G, Cmd+T/L, Cmd+W,
Cmd+O and Esc were exercised (`live-results-2026-11-04.md`).

In this file **Cmd** is the XUL `accel` modifier (Control on other platforms), **Opt** is `alt`.

## 1. Zen's own keys (the `zenKeyset`)

| Keys | Command | What it does | Script |
|---|---|---|---|
| **Cmd+S** | `cmd_toggleCompactModeIgnoreHover` | toggle compact mode; the sidebar does not pop open because the pointer is over it | `compact-mode.md` |
| **Cmd+Opt+S** | `cmd_zenCompactModeShowSidebar` | pin/unpin the floating sidebar (compact only) | `compact-mode.md` |
| **Ctrl+1 ... Ctrl+9, Ctrl+0** | `cmd_zenWorkspaceSwitch1..10` | go to the 1st ... 9th, 10th Space (Control on macOS, **Alt** on Windows/Linux) | `spaces.md` |
| **Cmd+Opt+Right / Left** | `cmd_zenWorkspaceForward/Backward` | next / previous Space; wraps | `spaces.md` |
| **Cmd+Shift+K** | `cmd_zenCloseUnpinnedTabs` | "Clear": close all unpinned tabs of the Space (except selected etc.) | `spaces.md` |
| **Cmd+Opt+G** | `cmd_zenSplitViewGrid` | split the selected tab with the next one: grid layout | `split-view.md` |
| **Cmd+Opt+V** | `cmd_zenSplitViewVertical` | same, side by side | `split-view.md` |
| **Cmd+Opt+H** | `cmd_zenSplitViewHorizontal` | same, stacked | `split-view.md` |
| **Cmd+Opt+U** | `cmd_zenSplitViewUnsplit` | dissolve the active split. (A synthesized key event did not trigger it in the live session, while calling `unsplitCurrentView()` did: treat the shortcut as unverified/an artefact of synthesized keys; it is defined as in this table) | `split-view.md` |
| **Cmd+Shift+\*** (`*`) | `cmd_zenNewEmptySplit` | new empty split | `split-view.md` |
| **Alt+Shift+L** | `cmd_zenToggleLibrary` | open/close the Library | `library.md` |
| **Cmd+O** | `cmd_zenGlanceExpand` | expand the Glance to a tab (Firefox's "Open File" is unbound) | `glance.md` |
| **Cmd+Shift+D** | `cmd_zenTogglePinTab` | pin / unpin the selected tab (acts on the Glance parent if a Glance is open; ignores the empty tab). Firefox's "Bookmark All Tabs" (also Cmd+Shift+D) is unbound | `essentials-and-pinned.md` |
| **Cmd+Shift+N** | `cmd_zenNewNavigatorUnsynced` | new **unsynced** window (`OpenBrowserWindow({ zenSyncedWindow: false })`: a window whose Spaces/tabs are not synced with the other windows; also in the app menu as "New unsynced window") | |
| **Cmd+Shift+C** | `cmd_zenCopyCurrentURL` | copy the current URL (shown as `⇧⌘C`); toast "Copied current URL!" | |
| **Cmd+Opt+Shift+C** | `cmd_zenCopyCurrentURLMarkdown` | copy the current URL as a Markdown link; toast "Copied current URL as Markdown!" | |

**No default key** (can be bound in the editor): `cmd_zenToggleSidebar` ("Toggle Sidebar's Width", Zen's test asserts it is unbound),
`cmd_zenOpenWorkspaceCreation` ("Create New Workspace"), `cmd_zenPinnedTabReset` ("Reset Pinned Tab to Pinned URL"),
`Browser:DuplicateTab` ("Duplicate Tab"). Menu items that show a shortcut look it up from the command
(`getShortcutDisplayFromCommand`); unbound commands show none.

## 2. Firefox keys Zen changes (`browser-sets.inc.xhtml` patch)

| Firefox key | In Zen |
|---|---|
| Cmd+S (Save Page) | moved to **Cmd+Opt+Shift+S** (`key_savePage`); Cmd+S is compact mode |
| Cmd+O (Open File) | **unbound** (`openFileKb`); Cmd+O is Glance Expand |
| Cmd+Shift+D (Bookmark All Tabs) | **unbound** (`bookmarkAllTabsKb`) |
| Esc (Stop loading) | **unbound** (`key_stop`); Cmd+. (`key_stop_mac`) still stops |
| Cmd+Shift+N (Undo Close Window) | **unbound** (`key_undoCloseWindow`); Cmd+Shift+N is the new unsynced window |
| Open Tabs sidebar key (`viewOpenTabsSidebarKb`), Firefox split-view keys (`key_addTabSplitView`, `key_separateTabSplitView`), `key_duplicateTab` | **removed** (Zen has its own split view and a separate Duplicate Tab key) |

Everything else in `browser-sets.inc.xhtml` keeps Firefox's default (see 3). The two `Cmd+Opt+Left/Right` meanings collide with
Firefox's default "previous/next tab" on macOS; in Zen they switch **Spaces** (verified live).

## 3. Every other key in the window (live dump, macOS, 2026-11-04)

Dumped from a running Zen 1.24b with `[...document.querySelectorAll("key")]` and `ShortcutUtils.prettifyShortcut` (136 `<key>` elements, 125 with a binding).
These are Firefox's own keys as Zen ships them (after the changes in section 2), in `mainKeyset`:

| Shortcut | Key id | Command |
|---|---|---|
| ⌘N | `key_newNavigator` | `cmd_newNavigator` |
| ⌘T | `key_newNavigatorTab` | `cmd_newNavigatorTabNoEvent` |
| ⌘L | `focusURLBar` | `Browser:OpenLocation` |
| ⌘K | `key_search` | `Tools:Search` |
| ⌥⌘F | `key_search2` | `Tools:Search` |
| ⌘J | `key_openDownloads` | `Tools:Downloads` |
| ⇧⌘E | `key_openAddons` | `Tools:Addons` |
| ⇧⌥⌘S | `key_savePage` | `Browser:SavePage` |
| ⌘P | `printKb` | `cmd_print` |
| ⌘W | `key_close` | `cmd_close` |
| ⇧⌘W | `key_closeWindow` | `cmd_closeWindow` |
| ⌃M | `key_toggleMute` | `cmd_toggleMute` |
| ⌘Z | `key_undo` |  |
| ⇧⌘Z | `key_redo` |  |
| ⌘X | `key_cut` |  |
| ⌘C | `key_copy` |  |
| ⌘V | `key_paste` |  |
| Del | `key_delete` | `cmd_delete` |
| ⌘A | `key_selectAll` |  |
| Backspace | `(no id)` | `cmd_handleBackspace` |
| ⇧Backspace | `(no id)` | `cmd_handleShiftBackspace` |
| ⌘← | `goBackKb` | `Browser:Back` |
| ⌘→ | `goForwardKb` | `Browser:Forward` |
| ⌘[ | `goBackKb2` | `Browser:Back` |
| ⌘] | `goForwardKb2` | `Browser:Forward` |
| ⌥Home | `goHome` |  |
| F5 | `key_reload2` | `Browser:Reload` |
| ⌘Y | `showAllHistoryKb` | `Browser:ShowAllHistory` |
| ⌃⌘F | `key_enterFullScreen` | `View:FullScreen` |
| ⇧⌘F | `key_enterFullScreen_old` | `View:FullScreen` |
| F11 | `key_enterFullScreen_compat` | `View:FullScreen` |
| ⌃⌘F | `key_exitFullScreen` | `View:FullScreen` | disabled (shown only in the opposite state)
| ⇧⌘F | `key_exitFullScreen_old` | `View:FullScreen` | disabled (shown only in the opposite state)
| F11 | `key_exitFullScreen_compat` | `View:FullScreen` | disabled (shown only in the opposite state)
| ⌥⌘R | `key_toggleReaderMode` | `View:ReaderView` | disabled (shown only in the opposite state)
| ⇧⌥⌘] | `key_togglePictureInPicture` | `View:PictureInPicture` |
| ⇧⌥⌘} | `(no id)` | `View:PictureInPicture` |
| ⌘R | `key_reload` | `Browser:Reload` |
| ⇧⌘R | `key_reload_skip_cache` | `Browser:ReloadSkipCache` |
| ⇧Esc | `key_aboutProcesses` | `View:AboutProcesses` |
| ⌘U | `key_viewSource` | `View:PageSource` |
| ⌥⌘U | `key_viewSourceSafari` | `View:PageSource` |
| ⌘I | `key_viewInfo` | `View:PageInfo` |
| ⌘F | `key_find` | `cmd_find` |
| ⌘G | `key_findAgain` | `cmd_findAgain` |
| ⇧⌘G | `key_findPrevious` | `cmd_findPrevious` |
| ⌘E | `key_findSelection` | `cmd_findSelection` |
| F3 | `key_findAgain2` | `cmd_findAgain` |
| ⇧F3 | `key_findPrevious2` | `cmd_findPrevious` |
| ⌘D | `addBookmarkAsKb` | `Browser:AddBookmarkAs` |
| ⇧⌘O | `manBookmarkKb` | `Browser:ShowAllBookmarks` |
| ⌘B | `viewBookmarksSidebarKb` |  |
| ⇧⌘B | `viewBookmarksToolbarKb` |  |
| ⌘. | `key_stop_mac` | `Browser:Stop` |
| ⌃X | `viewGenaiChatSidebarKb` |  |
| ⌃Z | `toggleSidebarKb` |  |
| ⇧⌘H | `key_gotoHistory` |  |
| ⌘- | `key_fullZoomReduce` | `cmd_fullZoomReduce` |
| ⌘_ | `(no id)` | `cmd_fullZoomReduce` |
| ⌘+ | `key_fullZoomEnlarge` | `cmd_fullZoomEnlarge` |
| ⌘= | `(no id)` | `cmd_fullZoomEnlarge` |
| ⌘0 | `key_fullZoomReset` | `cmd_fullZoomReset` |
| ⇧⌃Tab | `key_showAllTabs` |  |
| ⇧⌘X | `key_switchTextDirection` | `cmd_switchTextDirection` |
| ⇧⌘P | `key_privatebrowsing` | `Tools:PrivateBrowsing` |
| ⇧⌘S | `key_screenshot` | `Browser:Screenshot` |
| ⇧⌘A | `key_searchTabs` | `Browser:SearchTabs` |
| ⇧⌘Del | `key_sanitize` | `Tools:Sanitize` |
| ⇧⌘Backspace | `key_sanitize_mac` | `Tools:Sanitize` |
| ⌘Q | `key_quitApplication` |  |
| ⇧⌘T | `key_restoreLastClosedTabOrWindowOrSession` | `History:RestoreLastClosedTabOrWindowOrSession` |
| ⌘1 | `key_selectTab1` |  |
| ⌘2 | `key_selectTab2` |  |
| ⌘3 | `key_selectTab3` |  |
| ⌘4 | `key_selectTab4` |  |
| ⌘5 | `key_selectTab5` |  |
| ⌘6 | `key_selectTab6` |  |
| ⌘7 | `key_selectTab7` |  |
| ⌘8 | `key_selectTab8` |  |
| ⌘9 | `key_selectLastTab` |  |
| ⌘M | `key_minimizeWindow` | `cmd_minimizeWindow` |
| ⌘, | `key_preferencesCmdMac` |  |
| ⌘H | `key_hideThisAppCmdMac` |  |
| ⌥⌘H | `key_hideOtherAppsCmdMac` |  |

Developer-tools keyset (`devtoolsKeyset`):

| Shortcut | Key id | Command |
|---|---|---|
| ⌥⌘I | `key_toggleToolbox` |  |
| F12 | `key_toggleToolboxF12` |  |
| ⇧⌥⌘I | `key_browserToolbox` |  |
| ⇧⌘J | `key_browserConsole` |  |
| ⌥⌘M | `key_responsiveDesignMode` |  |
| ⌥⌘L | `key_inspector` |  |
| ⌥⌘K | `key_webconsole` |  |
| ⌥⌘Z | `key_jsdebugger` |  |
| ⌥⌘E | `key_netmonitor` |  |
| ⇧F7 | `key_styleeditor` |  |
| ⇧F5 | `key_performance` |  |
| ⇧F9 | `key_storage` |  |
| ⌥⌘W | `key_dom` |  |
| ⇧F12 | `key_accessibility` |  |
| ⇧⌘L | `key_inspectorMac` |  |

Notes from the dump:
- **Not `<key>` elements** (handled elsewhere, so absent from the dump): Ctrl+Tab / Ctrl+Shift+Tab (`⇧⌃Tab` here is only "show all tabs"), Cmd+Shift+[ / ], Ctrl+PageUp/PageDown, Cmd+Opt+Left/Right as *tab* switching: in Zen Cmd+Opt+Left/Right are the Space keys (`zen-workspace-forward/backward`), so there is no key conflict.
- **History is Cmd+Y** (`showAllHistoryKb`); Cmd+Shift+H is `key_gotoHistory`; **Cmd+Shift+O** opens the bookmarks library; **Cmd+B** the bookmarks sidebar; **Cmd+Shift+B** the bookmarks toolbar; **Cmd+K** / **Cmd+Opt+F** search; **Cmd+Shift+A** is "search tabs"; **Cmd+Shift+S** screenshot; **Cmd+I** page info; **Cmd+J** downloads; **Cmd+Shift+E** add-ons; **Ctrl+M** mute tab; **Cmd+Shift+P** private window; **Ctrl+Z** `toggleSidebarKb` (Firefox's sidebar toggle; Zen's own "Toggle Sidebar's Width" stays unbound); **Ctrl+X** `viewGenaiChatSidebarKb`.
- Disabled duplicates (`key_exitFullScreen*`, `key_toggleReaderMode`) are the state-dependent twins of an enabled key.
- `Cmd+Shift+Delete`/`Cmd+Shift+Backspace` clear history (`key_sanitize`).

## 4. Customisation

- Firefox's `CustomKeys` (about:keyboard-style editor in Settings) edits any `<key>`; Zen's patch to `CustomKeys` calls
  `ZenKeyboardShortcuts.migrateLegacyShortcuts` for every new window: an old `zen-keyboard-shortcuts.json` in the profile is read once,
  differing bindings become CustomKeys changes, and the file is deleted afterwards (legacy `zen.keyboard.shortcuts.version` pref cleared).
- The Zen entries show in the editor in **four groups**: "Compact Mode" (toggle, floating sidebar), "Workspaces" (Switch to Workspace 1..10,
  Create New Workspace, Close All Unpinned Tabs), "Split View" (grid, vertical, horizontal, close, new empty), "Other Zen Features"
  (Toggle Sidebar's Width, Toggle Library, Expand Glance, Toggle Pin Tab, Reset Pinned Tab to Pinned URL, Duplicate Tab, Copy current URL,
  Copy current URL as Markdown). Group titles come from `zenCKSOption-group-*`, entry names from the `zen-*-shortcut-*` strings.
- A customised Zen key runs its command and the display follows the customisation; resetting restores `accel,alt + Right` for
  Space-forward etc. (test `test_customized_zen_key_runs_command`).
- Modifier names: `ctrl/control` is Control on macOS (`accel` elsewhere), `meta` is Cmd on macOS, `alt` and `shift` as is.

## 5. Behaviour notes

- Cmd+T and Cmd+L open the floating bar (`cmd-t-cmd-l-session.md`); with the Library open, Cmd+T opens a tab and closes the Library
  (`library.md`, to verify).
- Sidebar-dependent commands (`cmd_zenToggleSidebar`, `cmd_zenToggleTabsOnRight`) are disabled while the Library is open.
- Space keys do nothing in private windows or when Spaces are disabled.
- Reserved keys: `key_close` (Cmd+W) and `key_closeWindow` (Cmd+Shift+W) are `reserved="true"` so pages cannot intercept them.

## 6. LIVE CHECK

1. (Done 2026-11-04) the full key dump is section 3; all Zen keys in section 1 matched.
2. Real keyboard: does Cmd+Opt+U fire (Option+U is a dead key)? Does Cmd+Shift+* mean Cmd+Shift+8 on a US layout?
3. Cmd+Opt+Left/Right conflict: which one wins in the real app (we saw Spaces).
4. Settings -> keyboard shortcut editor: layout, search, conflict handling, reset.

## 7. Test script (Mac loop)

For each row of section 1 that exists in Koan: send the chord to the frontmost window with a prepared state, and assert the effect
(compact toggled, Space changed and wrapped, split created, Library opened, Glance expanded, pinned/unpinned, URL copied). For each
unbound-by-default key: assert nothing happens (Cmd+O opens no file dialog, Esc does not stop a loading page, Cmd+Shift+N is not "undo
close window"). Use the Marionette key-action path in Zen and Koan's test hooks in Koan; do not rely on Option dead keys.
