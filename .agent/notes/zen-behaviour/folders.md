# Folders

Zen 1.24b. Source: `src/zen/folders/ZenFolders.mjs` (manager), `ZenFolder.mjs` (the `<zen-folder>` element),
`zen-folders.css`, `zen-folder-icon.inc.css`, `src/browser/base/content/zen-panels/popups.inc` (menu markup),
`prefs/zen/folders.yaml`, `locales/en-US/browser/browser/{zen-folders,zen-vertical-tabs,zen-general}.ftl`, and
Zen's own tests in `src/zen/tests/folders/` (create, empty_tab, basic_toggle, collapse_all, reset_button,
subfolder, max_subfolders, density, multiselected, visible_tabs, owner_tabs, issue_*).
Scope: ordinary folders. **Live Folders** (GitHub / RSS, `isLiveFolder`) and **Share Folder** are out of scope
(Koan has no equivalent); where they touch the code below it is noted. Related: `essentials-and-pinned.md`
(folders live in the pinned section), `spaces.md` (folders belong to one Space).

## 1. Model

- A folder is a `<zen-folder>` (a subclass of Firefox's tab group) that lives **in the pinned section of one
  Space**, between the Space header and the pinned/normal divider. It can hold pinned tabs, split-view groups and
  **subfolders**. Folders are never in the Essentials grid and never in the unpinned list.
- Every folder owns a **hidden pinned empty tab** (`zen-empty-tab`, `about:blank`, lazy browser) as its first
  child. It is invisible; it is the anchor that keeps a folder alive with no tabs in it and it is what a folder
  with only that tab is made of. Consequence: a new empty folder has `tabs.length == 1`, a folder with one real
  tab has 2 (Zen test `test_Create_Folder`, `test_Empty_Tab_First`; the empty tab stays first).
- Putting a tab in a folder **pins** it (`gBrowser.pinTab`). Taking it out does not unpin it; it lands in the
  pinned section. A tab in a folder that is also an Essential is removed from Essentials first.
- Nesting: `zen.folders.max-subfolders` = **5** levels (`folder.level + 1 >= 5` refuses). The "New Subfolder…" menu
  item is disabled on the folder at the deepest allowed level; dropping a folder onto a too-deep folder is refused.
- Default name `New Folder` (`_createFolderNode`); subfolders are created as `Subfolder`. Clearing the name in
  the rename box gives `Folder`. (`createFolder` option `label` overrides; tests use it.)
- Per-folder state saved in the session: `collapsed`, user icon, `zen-workspace-id`, parent. Restored at startup
  (`_sessionRestoring`); no animation while restoring, collapsed folders get their hidden margins recomputed once
  (`#queueCollapsedRelayout`, next animation frame).

## 2. Look of a folder row

- Header row = a normal tab row: height equals a tab's (`--tab-min-height` + 2 x `--tab-margin-block`, test
  `test_Folder_Density` asserts equal height/width in compact and touch density), `font-weight: 600`, the label
  starts 32 px from the left, text colour `--toolbar-color`. Hover paints a rounded `::before` plate inset 2 px
  (`--tab-background-color-hover`, `--border-radius-medium`); `[selected]` (a tab inside is selected and the
  folder is collapsed) paints `--zen-toolbar-element-bg-hover`. The Firefox tab-group coloured line is hidden.
- **Icon**: a 28 x 28 folder drawing at `left: 5px` (expanded sidebar), made of a *back* path, a *front*
  rounded rect (`x 5.6, y 9.6, 16.75 x 12.75, r 2.4`), an optional user icon (11 x 11 image centred on the
  front) and three dots. Colours derive from the Space accent: back `color-mix(primary 60% gray)`, front
  `color-mix(primary, white 70%)` (dark: darker mix), stroke `color-mix(primary 50% black)`; `filter: contrast(125%)`.
  `zen-folder-icon.inc.css` draws the **open / closed** poses by `svg[state=open|close]` (the back flap and front
  tilt); state is `open` when expanded, `close` when collapsed. During a tab drag over a collapsed folder the
  icon shows **open** (`highlightGroupOnDragOver`) and goes back to `close` when the drag leaves.
- **Dots instead of the user icon**: when a folder is **collapsed and has an active (selected/loaded) tab**
  (`svg[active=true]`) the icon image fades out and the three dots fade in. This is how a collapsed folder
  says "something in here is open".
- **Indentation**: children of a folder are indented **14 px per level** in the expanded sidebar
  (`--zen-folder-indent`, animated with the tabbox indent transition). In the collapsed (icon-only) sidebar the
  folder is one tile wide, the label is hidden and the icon nudges 1 px left when open.
- Folder rows have no close button. The only header button is the **reset button** (see 4.4), shown on hover.

## 3. Creating a folder

| Input | Zen does |
|---|---|
| Tab context menu -> **Move to Folder** (submenu: existing folders, separator, **New Folder**) | Pins and moves the tab(s) into the chosen folder; "New Folder" creates one holding the tab(s) |
| Same, with several tabs multi-selected | All selected tabs (plus the clicked tab) go in, in tab order |
| Right-click an empty part of the sidebar / toolbar -> **New Folder** (`zen-context-menu-new-folder-toolbar`) | Empty folder (just the hidden empty tab) |
| Command palette "New Folder" (`zen-action-new-folder`, URL bar actions) | Empty folder |
| Menu items disabled in private windows and when Spaces are disabled | |
| Context-menu "New Folder" on a tab that is a **Live Folder item** | Does nothing |

Placement (`#onNewFolder`): if the folder can go **before the trigger tab** (trigger is not an Essential, not in
a split-view group, and the depth rule allows it) the folder is inserted right before that tab's position;
otherwise it goes **after** the trigger's group. For the toolbar item, or when there is no trigger, it goes at the
end of the pinned section, just above the pinned/normal divider. The folder appears with the open animation
(`animateItemOpen`, the same slide/fade a new tab uses), is **expanded**, and **inline rename starts at once**
(`renameFolder: true`): the label becomes an input with the default name selected; Enter commits, Esc keeps the
default name (see 4.2).

Subfolder: folder menu -> **New Subfolder…**. Expands the folder and every ancestor first, creates a folder named
`Subfolder` **after the last child** of the folder, starts rename. Disabled at the depth limit (5 levels).

## 4. Interactions

### 4.1 Expand / collapse (click on the header)

- Click on the header row (not the reset button) toggles `collapsed` (`ZenFolder.on_click` -> Firefox tab-group
  click handler).
- **Collapse** (`animateCollapse`, 0.18 s, ease-in-out; 0 when `_dontAnimateFolder`): the children **fade to opacity 0
  while shrinking to height 0** and the next sibling slides up by the same height (the group's start element gets a
  negative `margin-top` = -(content height + 4 px)). Afterwards the container is `hidden`.
- **Collapse with an active tab inside**: the selected (or loaded: `folder-active`) tabs are **not** hidden. They stay
  visible, **indented one level**, directly under the folder header, and the folder gets `has-active`; the folder icon
  swaps to dots. Everything else in the folder hides as above. Only the active tabs are listed in `activeTabs`.
  Nested: a collapsed subfolder inside a collapsed parent hides with the parent; its own active tabs show under the
  outermost collapsed folder.
- **Expand** (`animateExpand`, 0.18 s): container unhidden, hidden items fade/grow back, margin back to 0, tabs that
  were shown as "active" lose `folder-active` and go to their normal indent, folder loses `has-active`, the icon goes
  to `open`. Expansion computes heights up front so there is no jump; it handles being started while a collapse is
  still running (`#settleAnimation` pins the current value instead of restarting).
- **Collapse all / expand all**: command-palette actions **"Collapse All Folders"** / **"Expand All Folders"**
  (`zen:global-action-collapse-all-folders`, `…expand-all-folders`; test `collapse_all`); icons and `has-active`
  follow the same rules per folder.
- **Adding a tab to a collapsed folder** (drop, or the folder-search list, or New Tab in folder): the folder
  **expands** (`group.collapsed = group.hasAttribute("has-active")`, i.e. stays collapsed only if it has an active
  tab), then the tab is placed. Dropping into an active collapsed folder keeps it collapsed with the new tab
  counted as active.
- **Spam-click** on a parent folder is supported (tests `Spam_Toggle_Parent_Folder`,
  `Collapsed_Subfolder_Stays_Collapsed`): a subfolder that was collapsed stays collapsed when the parent
  re-expands.

### 4.2 Rename

- Menu -> **Rename Folder…** only: `rename()` -> `gZenTabRenamer.start({ explicit: true })`, the same inline
  editor as tab renaming. **Double-click does not rename a folder** (ZenTabRenamer's comment: "`explicit` allows
  renaming folder labels, which dblclick does not"). Also started automatically after creation. **Rename is a no-op in the collapsed (icon-only) sidebar** and the menu
  item is hidden there.
- Editor: input replaces the label, padded 32 px left like the label, text selected. Enter commits, Esc cancels,
  blur commits. Empty name -> `Folder`. Commit fires `ZenFolderRenamed` and a session save.

### 4.3 Select a tab inside a collapsed folder

When the selected tab is in a collapsed folder (reached by Cmd+1.., the folder search list, back/forward in tab
history, a link from another tab, glance, closing the neighbour, or Cmd+Shift+T), `on_TabSelect` sets `has-active`
on every collapsed ancestor and runs `animateSelect`: the collapsed folder **shows only the selected tab** (indented
under the header, or, if the folder had been expanded by something else in between, loops to the next collapsed
ancestor). The folder is **not** fully expanded. Click on the header then expands it.

### 4.4 Reset button (unload everything active in this folder)

- The reset button (`tab-reset-button`, icon `reset`) sits at the right edge of the header, **hidden until hover**,
  and only while the sidebar is expanded **and** the folder has an active tab (`:root[zen-sidebar-expanded]
  zen-folder[has-active] > .tab-group-label-container:hover .tab-reset-button { display: flex }`).
  Tooltip **"Unload active in this folder"**.
- Click: stops the click propagating (so the folder does not toggle) and runs **`unloadAllTabs`**:
  `gZenPinnedTabManager.onCloseTabShortcut(event, folder.tabs, { noClose: true, alwaysUnload: true,
  folderToUnload: folder })`, then `activeTabs = []` and `collapsed = true`. So every loaded tab in the folder is
  **unloaded** (sleeping, grey, back to the pinned URL, not closed), the selected one is replaced by another
  tab as in `essentials-and-pinned.md` 4 (reset-unload-switch), and the folder **ends collapsed**.
- In a Live Folder the same button is a refresh/"broken" icon with its own action; ignore.
- Menu -> **Unload All Tabs** does the same as the button (also available while expanded and with nothing active; no-op
  if nothing is loaded).

### 4.5 Folder menu (right-click the header, or the label)

Order (popups.inc): *(Live Folder options, hidden)* | **Rename Folder…** | **Change Icon…** | ---- | **Unload All
Tabs** | **New Subfolder…** | ---- | **Change Space** (submenu of the other Spaces) | **Convert folder to Space** |
*(Share Folder…, out of scope)* | ---- | **Unpack Folder** | **Delete Folder**.

- **Change Icon…** opens the emoji/icon picker restricted to SVG icons (`onlySvgIcons`, "none" offered when an icon
  is set, stays open after a pick). The picked icon is drawn 11 x 11 on the folder front (fill = mix of stroke and
  text colour, 0.9 opacity). Fires `TabGroupUpdate` for the session.
- **Unpack Folder** (`unpackTabs`): expands the folder, then every child is **ungrouped** in reverse order, so tabs land
  in the pinned section **in place of the folder** in the same order, still pinned; the hidden empty tab is closed;
  subfolders are left as folders (one level up); split-view groups move as a unit. The folder disappears.
- **Delete Folder**: closes the hidden empty tabs and then calls `removeTabGroup` with `isUserTriggered`; **all tabs
  in the folder and its subfolders are closed** (Firefox group removal, `isUserTriggered`). The menu handler calls `delete()`
  directly: **no confirmation dialog**. Whether Cmd+Shift+T restores the group is a LIVE CHECK.
- **Change Space**: moves the whole folder (tabs and subfolders) to the pinned section of the chosen Space,
  re-tags `zen-workspace-id` on every tab and the folder, fixes `lastSelectedWorkspaceTabs`, and the selected tab,
  if inside, makes the target Space the active one on next switch. Also what drag-to-Space does while
  dragging (`hasDndSwitch`).
- **Convert folder to Space**: creates a **new Space** named like the folder, with the folder's icon image as its icon
  and the *current Space's container*; the folder's tabs (except the empty one) are appended to the new Space's
  pinned section and the folder is deleted; all non-Essential tabs get the new Space id; the selected tab (if it
  was in the folder) becomes that Space's last-selected tab. The new Space is **activated** (not `dontChange`),
  running the normal Space-switch animation (see `spaces.md` 3).

### 4.6 Folder tab search (hover popup)

- Pref `zen.folders.search.enabled` (default true). **Hover a collapsed folder's header** (not over the reset
  button, no tab drag in progress) for `zen.folders.search.hover-delay` = **500 ms** and a popup opens.
- Not opened if a tab is being renamed (`zen-renaming-tab`), the URL bar is focused, or the folder has no tab that
  should appear.
- The list shows only tabs **not already visible**: it excludes the folder's active tabs (those shown under the
  collapsed folder), `hidden` tabs and the empty tab (`#shouldAppearOnTabSearch`).
- Popup `#zen-folder-tabs-popup`: **250 px wide**, no padding, anchored to the **right** of the header (`topright topleft`,
  x = +10; mirrored to the left of the header when the sidebar is on the right, x = -10), vertically centred on the
  header (`y = -(min(n, 6) x 48) / 2`). Header row: a search icon (0.7 opacity) and a borderless, transparent input with
  the placeholder **"Search {folder name}..."**; list below: max height 263 px, scrolls; each item = favicon
  (`--size-item-small`), title, and a secondary line `<relative time since last access> • <folder name>`
  (tabs with no title fall back to the URL host).
  Empty result: **"No tabs matching that search 🤔"** centred.
- The input is focused and its text selected as soon as the popup shows. Typing filters items by
  `label.includes(query)` (lower-cased, live). **Arrow Up/Down and Tab/Shift+Tab** move the highlight (wraps),
  scrolling smoothly; **Enter** activates the highlighted item (what a click does: select that tab, which then
  shows under the collapsed folder per 4.3); Esc closes.
- Closing: leaving the header starts a **200 ms** timer, which closes the popup unless the pointer is over the
  popup or the header again. The header gets `open` while the popup shows (stays highlighted).
- Starting a folder collapse/expand animation, or dragging, cancels the timer and hides the popup.

### 4.7 Drag and drop

- Drag a tab (or several, or a split group) **onto a folder header**: the folder is highlighted (icon switches to
  open if it was collapsed); drop adds the tab(s) as pinned children of the folder; a collapsed target expands (or
  stays collapsed if it has an active tab, 4.1). **Live (real drag with HID events): dropping a tab on the header centre puts it at the START of the folder** (`[DragMe, FF]`); dropping an **Essential** on a folder header moves it into the folder (placed first) and removes it from Essentials; dropping a loose tab on the Essentials area makes it an Essential (appended last).
- Drop **between** rows inside an expanded folder inserts at that position and pins the tab; between top-level
  pinned rows and the folder inserts outside. Rule over a folder **header** (`ZenDragAndDrop.js` ~1676, pref
  `zen.tabs.folder-dragover-threshold-percent` = **20**): pointer in the **top 20 %** of the row = *into* the folder;
  in the bottom 20 % = *into* only if the folder is collapsed or has fewer than 2 children; the rest of the row
  is "next to" (before/after). Hovering a collapsed folder while dragging a tab over a different Space edge
  uses `zen.tabs.dnd-switch-space-delay` = 500 ms (see spaces.md).
- Dragging a tab **out** of a folder onto the normal list unpins it; onto the pinned section outside any folder keeps
  it pinned; onto an Essentials tile makes it an Essential (and removes it from the folder).
- Dragging a **folder** moves it with its children; it can be dropped into another folder as a subfolder if the depth
  limit allows (`canDropElement`: `target.group.level + 1 >= 5` refuses; the target folder is not highlighted when it
  would exceed the limit).
- During any drag the 14 px indentation transitions follow the pointer (`--tab-dragover-transition`).

### 4.8 Closing tabs / the last tab of a folder

- Closing a tab inside a folder (middle click or Cmd+W) follows the pinned-tab close behaviour (unload by
  default). The folder never disappears by closing its tabs: the hidden empty tab keeps it. A folder with no visible
  tabs shows its header only (no placeholder row, not collapsible content).
- When the last *active* tab of a collapsed folder is unloaded or closed (`animateUnload`), the folder loses
  `has-active`, its icon returns from dots to the user icon, and the margin animation (0.18 s) re-hides the
  now-empty collapsed content.

### 4.9 Split-view groups inside folders

A split-view group is treated as one item in the folder. Pinned split groups can be dragged in and out. Active
split groups show as one unit under a collapsed folder.

## 5. Prefs

| Pref | Default |
|---|---|
| `zen.folders.search.enabled` | true |
| `zen.folders.search.hover-delay` | 500 (ms) |
| `zen.folders.max-subfolders` | 5 |
| `zen.tabs.folder-dragover-threshold-percent` | 20 |
| `zen.folders.owned-tabs-in-folder` | false (a tab opened from a pinned tab in a folder, e.g. by Cmd+click, does **not** join the folder; true: it is pinned and added to it) |

Constants in code: animation 0.18 s (`#folderAnimationDuration`), reveal 0.2 s, collapsed margin offset 4 px,
indent 14 px per level, search popup 250 px wide / 48 px per row / max 6 rows, mouse-leave close 200 ms.

## 6. Edge cases worth copying

- Selecting a tab (not the folder) never expands a collapsed folder; it shows only the selected tab (4.3).
- The reset button never toggles the folder.
- A folder cannot contain an Essential; pinning a tab does not change which folder it is in.
- Folders cannot be created in private windows or when Spaces are disabled.
- Rename is unavailable when the sidebar is collapsed to icons.
- Folder delete is not confirmed.
- The folder's depth limit applies to drags, the menu item and to `createSubfolder`.
- Hover search never opens over an expanded folder, while renaming, or with the URL bar focused.

## 7. LIVE CHECK list

1. Double-click on a header: confirm it only toggles twice (does not rename) and what that looks like.
2. Esc during first rename: does the folder keep "New Folder" or get removed?
3. Exact look of the open/closed icon poses and the dots (zoomed 2x capture, light and dark; `zen-folder-icon.inc.css`
   gives the transforms but not the appearance).
4. Collapse animation: does the collapsed active tab slide, or stay put? Frame timings at 60 fps (expect 180 ms).
5. The drop zones (top 20 % / bottom 20 % rule): the indicator while dragging over a header, expanded and collapsed.
6. Reset button: hover reveal timing, hover colour, its exact icon, and the selected tab after clicking it.
7. Search popup: position relative to the header at default width, row layout, whether the folder header
   keeps the hover plate while it is open, 500 ms hover delay.
8. Convert to Space: what the new Space's icon looks like when the folder has no user icon, and whether it
   switches with the animation.
9. Delete Folder: confirm no dialog; then Cmd+Shift+T: does it restore the folder with its tabs?
10. Whether "Unpack" keeps the unpacked tabs selected and how the list re-flows (animation).

## 8. Test script (Mac loop; Zen and Koan)

1. Right-click a tab -> Move to Folder -> New Folder: a folder `New Folder` appears with the tab inside, editing
   state with the text selected. Type `Work`, Enter: label `Work`, tab under it indented 14 px, folder expanded.
2. Click the header: with the tab selected it stays visible under the header (indented), the rest fades and
   shrinks (180 ms) and the icon shows dots; click again: back to the full list.
3. Add a second tab by dragging it onto the header: it joins at the end; with the folder collapsed and nothing
   active, the folder expands to show it.
4. Collapse the folder with tab A selected; Cmd+click/select tab B outside: the folder row's dots go; click
   the header: expands fully, A is not indented specially.
5. Hover a collapsed folder 500 ms: the search popup appears (250 wide), type part of a title: list filters; Down,
   Enter: that tab is selected and shown under the collapsed folder.
6. With a loaded tab inside: hover the header: reset button; click: folder collapses, tab unloads (grey), the
   selected tab changes; the click did not toggle twice.
7. Right-click the folder: Rename, Change Icon, Unload All Tabs, New Subfolder, Change Space, Convert folder to
   Space, Unpack, Delete in this order. Create subfolders to depth 5: the sixth "New Subfolder…" is disabled.
8. Unpack: the tabs take the folder's place in the pinned list; Delete: all tabs close (and see LIVE CHECK 9).
9. Collapsed icon-only sidebar: folder is one tile, no label; rename is unavailable.
10. Relaunch: folders, collapsed state, icons and nesting persist; nothing animates on restore.
