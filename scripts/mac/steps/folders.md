# Mac steps: tab folders (#42, PR #60; audit 3 §6, FO1-FO14)

For the Mac loop and the hands-on tester. Linux e2e (`scripts/e2e/folders.mjs`) runs nowhere since the Mac-only
switch, and the drags need a real pointer (`scripts/mac/tools/ptr`, or by hand). Zen 1.24b's behaviour is in
`.agent/notes/zen-behaviour/folders.md` and `live-results-2026-11-04.md`; the source is
`.cache/zen-desktop-1.24b/src/zen/folders/`.

DCHECK build, a normal window, vertical tabs, Spaces and pinned tabs on (folders need both). Grep the log for
`:FATAL:`, `CHECK`, `Spaces:` and `Koan: the vertical tab strip` after each block.

Setup for most steps: Space "Home" with 2 pinned tabs (⌃⇧D), 3 normal tabs; "+" > Create Folder "Work"; folder
menu > New Subfolder… "A"; Move to Folder > Work on one normal tab, > A on another.

## Order and pinned semantics (FD2, FD3)

| # | Do | Expect |
|---|---|---|
| 1 | Look at Home | Header → the 2 pinned tabs → Work row → its tab (indented 14 px) → A row (indented) → A's tab (28 px) → separator with Clear → "+ New Tab" → the normal tab. No folder row or folder tab below the separator. |
| 2 | Switch to another Space and back; quit and relaunch (continue where you left off) | Same order, same folders, A still inside Work, collapsed state kept. |
| 3 | Right-click a normal tab > Move to Folder > Work | It moves above the separator, under Work, and is pinned: ⌘W on it runs the pinned close behaviour (reset/unload/switch per Settings), not a close. Clear leaves it. |
| 4 | Move an Essential into Work (tab menu) | It leaves the Essentials grid, joins Work and keeps its pinned URL (the reset button shows once you navigate it away). |
| 5 | Folder menu > Unpack Folder on A | A's tab moves up into Work and stays pinned. Tab menu > Remove from Folder (Koan's extra item) on a Work tab: it stays pinned, among the pinned tabs above Work. |
| 6 | ⌃⇧D (Unpin) on a Work tab | It leaves Work and goes to the top of the normal tabs, below "+ New Tab". |
| 7 | Create a folder with no tabs ("+" > Create Folder) in a Space with no pinned tabs | Its row sits above the separator, not on top of it (empty folders are part of the pinned section). |

## Drag (FD7, FD9; needs a real pointer)

| # | Do | Expect |
|---|---|---|
| 8 | Drag a normal tab onto the middle of the Work row and release | It joins Work as its **first** tab, pinned (Zen: a drop on the label goes before the folder's first tab). Also try the tab right above the Work row: a drag that never changes the tab's place in the strip isn't seen (Koan learns of a drag from the moves). Note whether it joins. |
| 9 | Drag a tab onto the top fifth of the Work row | It goes just above Work, not into it (zen.tabs.folder-dragover-threshold-percent = 20). The bottom fifth: into Work while Work is expanded with 2+ items, below the folder when it is collapsed or has fewer. |
| 10 | Drag an Essential tile onto a folder row | It leaves the Essentials and becomes the folder's first tab. (Live check: whether upstream reports that drag as a move; if not, the Essential lands as a pinned tab beside the folder.) |
| 11 | Drag a Work tab down among the normal tabs | It leaves Work and is unpinned. Drag it among the pinned tabs above Work, or to the very top of the Space, instead: it leaves Work, stays pinned. |
| 11b | Drag a Work tab past the Space's end (or over the Essentials) and back among the normal tabs before releasing; drag one into a second window's normal tabs | Both are unpinned where they drop (a drag that leaves the Space on the way is still decided at the drop). |
| 12 | Drag Work's last tab past the separator and back to its place before releasing; then drag it down and press Esc before releasing | Both times it is still in Work, at its place. |
| 13 | Drag a tab between two Work tabs | It joins Work there. Same with a split view: both halves join (and leave) together, and the strip doesn't keep reordering. |
| 14 | Drag over a collapsed folder (holding its active tab) | Nothing drops onto a hidden tab; the drop lands between visible rows (FD8, live check). |

## Input (FD7)

| # | Do | Expect |
|---|---|---|
| 15 | Double-click a folder row | It toggles twice (collapsed, then expanded): **no** rename, like Zen. |
| 16 | Folder menu > Rename Folder… | The name edits in place; Enter keeps it, Esc keeps the old name, an empty name becomes "Folder". |
| 17 | Collapse the sidebar (icons only), right-click a folder | The folder menu has no Rename Folder… (Zen hides it). |
| 18 | ⌃⇧PgUp / ⌃⇧PgDn on a Work tab with another Work tab next to it | It swaps places inside Work, and stays swapped. |
| 18b | ⌃⇧PgDn on the last tab of the last folder (a normal tab below it), and ⌃⇧PgUp on the first pinned tab's neighbour below the folders | It moves past the edge, then ~0.4 s later the folder tab is back in its folder, still pinned (a move that isn't a drag never unpins: Zen's moveTabTo clamps); a normal tab moved up past the separator goes back below it, unpinned. |
| 18c | DevTools on the extension test page: `chrome.tabs.move(<folder tab id>, {index: <a normal tab's index>})` | As 18b: back in its folder, pinned. |

## Temporary ungroups (FD4)

| # | Do | Expect |
|---|---|---|
| 19 | With a Work tab active: Mac menu Tab > Group Tab (if shown), ⌃⇧PgDn at the Space's edge, extension `chrome.tabs.ungroup(id)` | Afterwards the tab is still in Work and still pinned (Spaces put it back; a regroup keeps a pinned tab pinned). |
| 19a | The same on a Work tab of a Space that isn't shown (extension `chrome.tabs.ungroup(id)`) | Back in its own Space (not the shown one), in Work, still pinned. |

## Persistence (FD6, review 3 B1)

| # | Do | Expect |
|---|---|---|
| 19b | Continue where you left off (the default): close the **last window** with the red button, and again with ⇧⌘W; each time reopen from the Dock | Work, A and their tabs are back, collapsed state kept. Then ⌘Q and relaunch: the same. (Closing the last window keeps the app on macOS; the window comes back from the session, which closing must not have emptied.) |
| 20 | chrome://settings/onStartup "Open the New Tab page", quit, relaunch | Work, A and their tabs come back (unloaded until shown, pinned, with their pinned URL). With a folder in the second Space too: both Spaces come back (#159's stored Spaces), each with its folders. Same after closing the last window and reopening from the Dock. |
| 20b | The same with two windows, each with its own folders; quit from the second | The folders of the window you were in come back (Koan's Spaces and folders are per window, and stored together). Also: in the window with folders press ⌘N, ⌘W the new window, then ⌘Q and relaunch: the folders come back (the window you come back to stores its folders again). |
| 20d | Startup "Open the New Tab page", one window with Work and A: ⌘N, then ⌘Q from the new window (it's frontmost); relaunch. Again: ⌘N, ⌘W the **first** window, ⌘Q from the new one; relaunch | Both times Work, A and their tabs come back (a window that never had a folder leaves the stored folders alone). |
| 20e | Startup "Open the New Tab page": Spaces One, Two, Three with a folder in Two; drag Three to the top in the Library (or the Space bar), quit, relaunch. Then hand-edit `koan.folders.list` in `Preferences` to drop a folder's `space_id` and relaunch | The folder comes back in Two (its Space by id), not in the Space now at its old place. Without `space_id` it comes back by place. `koan.workspaces.list` entries have an `id` (a UUID) and the folder entries a matching `space_id`. |
| 20c | Startup "Open the New Tab page": close the last window, reopen from the Dock (folders come back), then History > Reopen Closed Window | Known edge (documented): the reopened window has the same folders too. No crash, no CHECK. Also: a crash, then don't restore, edit a folder, quit, relaunch: the crashed session's folders are still offered back on the next start without a session. |
| 21 | Crash-restore into an open window, from a session with 2 Spaces that each have a folder | Each folder lands in its own Space. |
| 22 | Close a Work tab, ⌘⇧T | It reopens in Work. Delete Folder on a Space's only tabs: the Space stays with a new tab, ⌘⇧T reopens the tabs loose. |

## Other actions and teardown

| # | Do | Expect |
|---|---|---|
| 23 | Convert folder to Space; Change Space; Unload All Tabs; folder search (hover a collapsed folder 500 ms) | As USAGE.md describes; the new Space is named after the folder; its subfolders stay folders there. |
| 23b | Compact mode hiding the sidebar: peek it, hover a collapsed folder until its search opens, move the pointer into the popup, type, pick a tab | The sidebar stays out while the popup is open (no floating or vanishing popup), and slides away after it closes. |
| 23c | Compact mode hiding the sidebar: peek it, right-click a folder row, move the pointer over the menu, wait 2 s, pick an item | The sidebar stays out while the menu is open, and slides away after it closes. |
| 24 | Rename a folder and switch Spaces 20 times during the rename, then ⌘W the window mid-rename | No crash, no CHECK. |
| 25 | Create 5 levels of subfolders | New Subfolder… is disabled at level 5. |
| 26 | chrome://flags `tab-groups-focusing` Disabled; relaunch | No crash. Rows follow their groups or are hidden. |

## Known limits

- 20d: the new window's Spaces are stored (they are the window you were in), the first window's folders stay. Their Space ids aren't among the new window's Spaces, so they come back by place in its Spaces (the last Space if it has fewer).
