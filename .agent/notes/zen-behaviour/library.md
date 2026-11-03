# Library

Zen 1.24b. Source: `src/zen/library/` (`ZenLibrary.mjs` shell, `ZenLibraryWidget.sys.mjs` sidebar button and download stack,
`ZenLibraryUtils.sys.mjs`, `ZenLibraryDragAndDrop.mjs`, `zen-library.css`, `sections/*.mjs`), `prefs/zen/library.yaml`,
`locales/en-US/browser/browser/zen-library.ftl`, `src/zen/common/zen-sets.js` and `zen-keysets.inc.xhtml`, tests in
`src/zen/tests/library/`. Depth: the **shell** (open/close, animation, swipe, rail, customize) and the **History**,
**Downloads**, **Spaces** sections were read in detail; **Space routing, Boosts and Media** only for their controls (marked
"light" below). Boosts' editor and Media's preview viewer are out of scope for Koan unless the user asks.
Related: `spaces.md` (swipe at the edge Space), `theme-picker.md` (Spaces section opens it), `folders.md`.

## 1. Model

- The Library is a **slide-over panel** that replaces the sidebar area: a full-height panel on the sidebar side (left;
  right when the sidebar is on the right) that pushes the web content aside, replaces the chrome and shows one **section**
  at a time. It is not a tab and not a window.
- Sections (id, tab label), in rail order: **Routing** (`space-routing`, hidden in private windows / Spaces disabled),
  **Media** (`media`, opt-in), **Downloads**, **Boosts**, **Spaces** (`spaces`, not in private windows), **History**.
  Default section is `history` (`zen.library.last-tab` remembers the last one; unknown -> history).
- `zen.library.enabled` (true) gates everything; `toggle()` does nothing when false.

## 2. Opening and closing

| Input | Zen does |
|---|---|
| **Alt+Shift+L** (`zen-library-toggle`, `cmd_zenToggleLibrary`) | toggles the Library (open <-> closed), last section |
| The **Library button** in the sidebar's bottom buttons (`zen-sidebar-foot-buttons`; `command=cmd_zenToggleLibrary`) | same |
| `cmd_zenManageWorkspaces` ("Manage Spaces") | opens straight to the **Spaces** section; if already open on Spaces, closes |
| Swipe on the trackpad past the **first Space** (left edge; last Space for a right sidebar) with `gZenWorkspaces.shouldSwipeEdgeActions` | drags the Library open (see 2.2) |
| **Esc** while open | closes it (unless a section handles Esc: e.g. Space routing's form, a media viewer; or focus is inside the Library but not in the search box: then Esc is ignored) |
| Footer **back** button (tooltip "Close library") | closes |
| Opening **any new tab** while open (`TabOpen`) | closes it (unless the Library itself opened the tab with `keepOpenWhile`, e.g. Cmd-click in History) |
| Clicking a row that opens something (a history page, a Space's tab, a download) | opens it and closes the Library |
| Disabled while open: `cmd_zenToggleSidebar` and `cmd_zenToggleTabsOnRight` (held and released afterwards) | |

### 2.1 Open/close animation

- A **spring** drives `openProgress` 0 -> 1: stiffness 720, damping 47, mass 1.2 (no bounce visible: critically damped); `transitioning`
  attribute while it runs. Re-targeting mid-way stops the old spring and animates from the current progress.
- At progress `p`: the Library `translateX(-100% * (1 - p))` (mirrored for right), the **web content wrapper** translates by
  `p * (libraryWidth - toolboxWidth)` (so the page slides right, only by the extra width beyond the sidebar it replaces), the
  **toasts** counter-translate, and the **sidebar toolbox** (`#navigator-toolbox`) fades and shrinks: `scale 1 - 0.04 * t`, `opacity
  1 - t` with `t = min(1, 3p)` (fully gone at p = 1/3). In **compact mode** (hide-tabbar or single toolbar) the toolbox instead
  slides out sideways by 100 % at the same rate, and the page offset subtracts `toolboxWidth - separation`.
- The **traffic lights** are moved into the Library's header ("adopted") once `p > 0.6` (macOS, sidebar on the left; the clone
  stays hidden when the sidebar is collapsed) and put back below 0.6.
- Width: `clamp(418px, --zen-library-content-width, 95vw)`; after opening, changes of width animate with `width .3s
  cubic-bezier(.36,.06,0,.94)`. The side rail is >= 110 px wide.
- Idle clean-up: 2 s after closing the DOM of non-active sections is dropped (`IDLE_CLEANUP_TIMEOUT_MS`), the active one
  stays mounted.

### 2.2 Swipe

Only from the edge Space. `swipeProgress(raw)`: translation x 0.8 added to the starting state (0 closed / 1 open), damped past the ends with a rubber band
(constant 0.08, dimension 0.2). On release: target = open if the direction was toward opening (`stopSwipe(direction)`), or by
progress > 0.5 for a stalled swipe (`swipeReset`). While swiping the Library ignores the pointer.

## 3. Layout

- **Rail** (left column): header (traffic-light area; padding 12/0/0/9 on macOS), the section tabs, and the **footer**: back
  (closes), **customize** (paintbrush, "Choose tabs"), **donate** (heart; opens zen-browser.app/donate in a tab and closes).
  On a right sidebar the footer is reversed and the back arrow rotated 180 deg.
  Tab = icon above/beside a label, gap 8; active tab highlighted; clicking sets the active section with the tab's "animate"
  attribute (icon animation) on the new one only.
- **Content** (right): the active section (`#zen-library-content`, no window dragging). Panel background: a left-to-right
  white 0.08 gradient on the rail, shadow `9px 0 17px rgba(0,0,0,.04)`.
- **Customize** (footer paintbrush toggles): every rail tab gets a small x ("Take this tab out") or +, "Put this tab back"
  button; clicking only toggles visibility and does not switch section; at least **one** tab always stays visible (hiding the last visible
  tab is refused). Hidden ids are stored in `zen.library.hidden-tabs`
  (comma list). If the active section is hidden, the first visible one becomes active. Customizing ends when the Library closes.
- **Right-click a rail tab**: a section-specific menu (`tabMenu`): History -> "Clear history" submenu (Last hour / 12 hours /
  Today / Last week / Last month / Everything, with a confirm prompt "Clear history?"), Downloads -> "Clear all downloads",
  Media -> "Turn off Media"; sections without items show none.

## 4. Common list pattern (History, Downloads, Media, Boosts, Routing)

Search box (placeholder per section; debounce **250 ms**; focused when the section opens), a **Filter** button that opens an
inline filter pane titled "Filter X…" with an exclusive "When" group (Today / This Week / This Month) and section-specific
groups (History: "What to look through" History / Closed tabs, "Sort by" By Date / By Site / By Most Visited / By Last Visited;
Downloads: file type Images / Video / Audio / Documents / Archives / Apps; Media: "Where to look" folders), "Done" closes it.
Lists render **100 rows per page** (`PAGE_SIZE`), more loaded when the end sentinel is reached; empty states ("No history found",
"No closed tabs", "No downloads found", "No media found", "No boosts yet").

## 5. History

- Data: Places history for the last **120 days**; with the "Closed tabs" source (`zen.library.history.closed-tabs`, false) the
  list shows recently closed tabs from the session store. Grouped by day (relative day names via `Intl.RelativeTimeFormat`, then
  medium dates) or by site (host; "Other" for no host), or flat for most/last visited.
- Row: favicon (or `page-icon:`), title (or URL), subtitle = URL (scheme stripped) + visit time (medium date, short time).
  Hover reveals two icon buttons: **forget** (trash, "Remove from history") and **reopen** (u-turn arrow, "Reopen page").
- **Click** a row: opens the page in a **new tab** and closes the Library (restores the tab via `undoCloseById` for closed tabs).
  **Cmd+click / middle click**: opens in the **background**, the Library stays open, toast "Opened tab in a new tab"
  (`library-history-opened-in-background`). Rows are draggable (to the tab strip/pinned area as a link).
- **Right-click** a row: Reopen Page, Forget This Page, Forget Site data ("places-forget-domain-data"). "Clear history" via the rail
  tab menu (confirm prompt with the Clear history / Cancel buttons, "cannot be undone").

## 6. Downloads

- Rows show an icon (or the file's **thumbnail preview** when drawable), name, and state text (speed "{rate} {unit}/s", "Opens when finished",
  blocked text, retry). Per-row buttons: **cancel** (while running), **show in Finder**, **retry** (failed), **more options** menu.
- **Click**: a running download toggles "open when done"; a finished one opens the file **in a tab**
  (`openDownload(openWhere: "tab")`); a blocked one asks `confirmUnblock` first.
- Menu: Open {name}, Copy {name}, Copy download link, Unblock, **Hide from Zen** (removes from the list only), **Delete File**,
  Clear Download Preview, Retry Download; "Show in Finder" on macOS. Rows are draggable as files.
- The rail tab menu has "Clear all downloads".

### 6.1 Library button and download stack

The sidebar **Library button** carries a download **badge** (a circular progress ring that fills while a download runs). On hover
(or when a new download starts, `recentNewDownload`) a **stack of up to 4 recent downloads** (`ENTRIES = 4`) rises from the bottom
buttons (`zen-library-download-list`, masked fade, `zen-library-stack-open`). Leaving the button or the stack closes it after
**200 ms**. Each entry: badge ring, title, subtitle (state), action button (cancel while running / show in Finder when done);
click opens the file, right-click offers the download menu, entries drag as files. The badge flies onto the newest entry when it opens
(`--zen-library-badge-to-x/y`).

## 7. Spaces section

A horizontally scrolling row of **cards, one per Space**, each a *live copy* of that Space's sidebar (pinned tabs, folders, tabs), kept in sync with
the real strips by listening to `TabOpen/Close/Move/Pinned/Hide/AttrModified/Group*` events (copy updates are batched and scoped to the
Space named in the event).
- Card header: **icon** button (opens the icon picker; "Change icon"), **name** (click to rename inline: Enter commits, Esc cancels,
  blur keeps), **theme** button (opens the theme picker for that Space, `theme-picker.md`), **drag handle** ("Drag to reorder"),
  **more options** (the Space "more actions" menu, `spaces.md`).
- **Click a tab** in a card: selects it (switching Space if needed) and closes the Library; the tab's x closes it (with the close animation) and leaves the Library open;
  **click a folder header**: collapses/expands the copy with the folder animations (`folders.md`), remembered per folder; reset buttons are ignored in copies;
  right-click opens the real tab's/folder's context menu at the pointer.
- **Reorder cards**: pointer-drag the handle; the dragged card follows the pointer, other cards slide to make room by slot centres, the strip auto-scrolls near the edges (48 px zone, 12 px/step); on drop the Space order is saved.
- A **"+" card** at the end (50 px "plus" badge) opens the Space creation form (`gZenWorkspaces.openWorkspaceCreation()`).
- Sizing: cards resize with a 600 ms fallback timeout while the panel width animates (`sizing` attribute hides scrollbars).

## 8. Light coverage

- **Routing** (`space-routing`): list of "when the link [contains | is exactly | RegEx] ... open it in [Space | Most recent space]" rules;
  **New** button opens an in-panel form ("New route" / "Edit route", Cancel / Add route / Save route), a trash button per rule removes it, a bottom group
  "Default route for external links". Esc closes the form. Draft is kept on a short timer. Empty state explains routes. (Backed by `space_routing` code; separate test dir.)
- **Boosts**: list with a toggle per boost (enable/disable), click opens the editor, context menu Edit / Export ("save") / Delete.
- **Media** (opt-in): first run shows an opt-in card ("Your Media in the Library", "Show Media" button, privacy note); then folders
  (`zen.library.media.folders` = downloads,documents,pictures; depth <= 2, <= 60 folders, <= 300 items) with a viewer (previous/next/close/more, Open, Copy,
  "Open in {app}"). `zen.library.media.enabled` false by default.

## 9. Prefs

| Pref | Default |
|---|---|
| `zen.library.enabled` | true |
| `zen.library.last-tab` | `history` |
| `zen.library.hidden-tabs` | "" |
| `zen.library.history.closed-tabs` | false |
| `zen.library.media.enabled` | false |
| `zen.library.media.folders` | downloads,documents,pictures |

Constants: spring 720/47/1.2; rail >= 110; panel `clamp(418px, content, 95vw)`; traffic lights adopted past p = 0.6; toolbox fully hidden at p = 1/3;
search debounce 250 ms; page 100 rows; history 120 days; download stack 4 entries, 200 ms close delay; idle cleanup 2 s.

## 10. LIVE CHECK list

1. Open/close with Alt+Shift+L: frame timings (expect ~300-400 ms settle), what the web content does (slides right by how many px), toolbox fade.
2. Panel width at the default window (418?) and whether it follows the window (95 vw), the rail width, tab label/icon look.
3. Where the traffic lights are while open (adopted into the Library header).
4. History default view (grouping by day, header look, row height), hover buttons, Cmd+click toast.
5. Customize mode: x buttons, refuse-to-hide-last behaviour, persistence across relaunch.
6. Download stack: hover timing, entry look, badge flight, when a download is running.
7. Spaces cards: width, scroll, reorder animation, theme/icon buttons.
8. Esc behaviour with focus in the search box vs elsewhere.
9. Opening a new tab (Cmd+T) while the Library is open: does the Library close and the URL bar open?

## 11. Test script (Mac loop; Zen and Koan)

1. Alt+Shift+L: Library opens from the sidebar side with the spring; page slides right; sidebar fades; traffic lights move into the Library header past 60 %. Alt+Shift+L again: reverse.
2. Open to History; click a visited page: new tab with it and Library closes. Re-open; Cmd+click another: opens behind, Library stays, toast.
3. Type in the search box: results filter after ~250 ms; Filter -> Closed tabs; Done.
4. Right-click the History rail tab -> Clear history -> Last hour: confirm prompt; Cancel.
5. Footer paintbrush: x buttons on tabs; hide Boosts; relaunch: still hidden; restore; try to hide all but one.
6. Download a file: badge ring fills, hover the Library button: stack with the entry; click opens the file.
7. Spaces section: cards for each Space; rename a Space by clicking its name; drag a card to reorder; "+" opens the creation form.
8. Esc closes. Reopen; press Cmd+T: record whether the Library closes and the floating URL bar opens (LIVE CHECK 9).
9. Swipe at the first Space (if a trackpad is available) opens the Library with rubber-banding.
