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

## 3. Firefox defaults that stay (macOS), most used

From Firefox's macOS keyset, unchanged by Zen's patch; **from memory plus the patch context, to be verified by the live dump in section 6**:

| Group | Keys |
|---|---|
| Tabs | Cmd+T new tab, Cmd+W close tab, Cmd+Shift+T reopen closed tab (or window/session), Cmd+Shift+W close window, Cmd+1..8 select tab n, Cmd+9 last tab, Ctrl+Tab / Ctrl+Shift+Tab next/previous tab, Cmd+Shift+[ and ] previous/next tab, Ctrl+PageUp/PageDown |
| Windows | Cmd+N new window, Cmd+Shift+P new private window, Cmd+M minimise, Cmd+H hide, Cmd+Q quit, Ctrl+Cmd+F full screen |
| Navigation | Cmd+L focus the URL bar (Zen: floating), Cmd+R reload, Cmd+Shift+R reload bypassing cache, Cmd+[ and Cmd+Left back, Cmd+] and Cmd+Right forward, Cmd+. stop, Cmd+Shift+H history/library |
| Page | Cmd+F find, Cmd+G / Cmd+Shift+G find next/previous, Cmd+P print, Cmd+U view source, Cmd++ / Cmd+- / Cmd+0 zoom |
| Bookmarks and tools | Cmd+D bookmark this page, Cmd+Shift+B bookmarks toolbar, Cmd+B bookmarks sidebar, Cmd+, settings, Cmd+Opt+I developer tools |

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

1. Dump every key to a table (Marionette, chrome context): `[...document.querySelectorAll("key")].map(k => [k.id, k.getAttribute("modifiers"), k.getAttribute("key") || k.getAttribute("keycode"), k.getAttribute("command"), ShortcutUtils.prettifyShortcut(k)])`, and compare with sections 1-3 (fills in the full list and fixes errors in section 3).
2. Real keyboard: does Cmd+Opt+U fire (Option+U is a dead key)? Does Cmd+Shift+* mean Cmd+Shift+8 on a US layout?
3. Cmd+Opt+Left/Right conflict: which one wins in the real app (we saw Spaces).
4. Settings -> keyboard shortcut editor: layout, search, conflict handling, reset.

## 7. Test script (Mac loop)

For each row of section 1 that exists in Koan: send the chord to the frontmost window with a prepared state, and assert the effect
(compact toggled, Space changed and wrapped, split created, Library opened, Glance expanded, pinned/unpinned, URL copied). For each
unbound-by-default key: assert nothing happens (Cmd+O opens no file dialog, Esc does not stop a loading page, Cmd+Shift+N is not "undo
close window"). Use the Marionette key-action path in Zen and Koan's test hooks in Koan; do not rely on Option dead keys.
