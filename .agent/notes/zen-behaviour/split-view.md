# Split view

Zen 1.24b. Source: `src/zen/split-view/ZenViewSplitter.mjs` (2659 lines), `zen-split-view.css`,
`zen-split-group.inc.css`, `zen-splitview-overlay.inc.xhtml`, `src/zen/drag-and-drop/ZenDragAndDrop.js`
(`_handle_tabDragOverToSplit`, `#createFakeTabSplit`), `src/browser/components/tabbrowser/content/tab-mjs.patch`
(tab click split), `src/browser/base/content/zen-keysets.inc.xhtml`, `src/zen/common/zen-sets.js` (commands),
`prefs/zen/zen.yaml`, `locales/en-US/browser/browser/{zen-split-view,zen-general}.ftl`, and Zen's tests in
`src/zen/tests/split_view/`. Related: `glance.md` (Glance -> split), `folders.md` (split groups in folders),
`essentials-and-pinned.md` (pinned tabs in a split).

## 1. Model

- A split view is a **group of 2-4 tabs shown side by side** in the content area. `MAX_TABS = 4`.
  It is represented in the sidebar by a Firefox tab group with attribute `split-view-group` (not a folder).
- Layout is a **tree** (`nsSplitNode` rows/columns with `sizeInParent` percentages; leaves are tabs). `gridType`:
  - `grid` (default): 2 tabs = one row of 2 columns; 3 tabs = left column of 2 stacked + 1 full-height tab on the
    right; 4 tabs = two columns of 2 stacked each. Rows are columns of `ceil(n/2)` equal width; an odd tab is its own
    full-height column.
  - `vsep` ("vertical" split): tabs in one row, equal width, split by vertical bars.
  - `hsep` ("horizontal"): tabs in one column, equal height.
- One tab of the group is **selected**; the others are shown too (all `docShellIsActive`), but only the selected pane
  gets the **active outline**. Clicking any pane (mousedown on its container) selects that tab.
- A tab can be in only one split group. Hidden tabs and the hidden empty tab cannot be split (filtered out).
- A group has `groupId`; pinned state is per group: if one tab is pinned the group is pinned (`on_TabPinned` syncs).
- Pinned/Essential mix: if any tab is an **Essential**, or some tabs are pinned and some are not, the pinned ones are
  **duplicated** (`gBrowser.duplicateTab(tab, true)`) and the copies are split; the originals stay where they are.
  All-pinned groups keep their tabs and the group is pinned (tests `Basic_Split_Groups_Pinning`).
- Live Folder items cannot be split.

## 2. Look

### 2.1 Browser area

- Panes are absolutely positioned inside the content area (`.browserSidebarContainer[is-zen-split]`); gap between panes
  = `--zen-element-separation`: **8 pt measured live** (spec section 7: two panes 612.5 x 907, 8 pt gap, 7 pt radius); forced
  to 8 when `zen-no-padding`; Glance overrides it to 6. Row gap = that, column gap = that +1 px. Panes keep the
  rounded content radius (`--zen-native-inner-radius`).
- **Active pane**: `outline: 2px solid --zen-active-split-outline-color`, colour = accent darkened 20 lightness points
  (light) / the primary button border colour (dark). The outline is hidden in DOM fullscreen and on Glance overlays.
  Inactive panes have no outline; they dim only while **rearranging** (opacity .85).
- Pane move/resize animation: `inset 0.09 s ease-out` (the whole layout transitions to the new inset); browser opacity
  `0.2 s ease-out`. Not applied while resizing by dragging a splitter, nor during the `zen-split-view-no-transition`
  frame used when creating/activating.
- **Pane header** (hover only): a small tab that hangs from the **top edge of each pane, centred**
  (`.zen-view-splitter-header`): background = the active-outline colour, 6 px radius on the bottom corners only,
  padding `0.4rem 0.6rem 0.2rem`, 0.8rem gap, shadow `0 0 5px 1px rgba(0,0,0,.1)`. Two 16 x 16 toolbar buttons:
  **rearrange** (grip, rotated 90 degrees, cursor `move`) and **unsplit** (x, 10 px glyph). Fades in `0.1 s` with a
  `0.1 s` delay when the pointer is over the pane (not in DOM fullscreen) or over the header; invisible and
  non-interactive otherwise.
- **Splitters**: invisible bars in the gap between panes (`.zen-split-view-splitter`, vertical bars have `ew-resize`,
  horizontal ones `ns-resize`, thickness = the gap), always above the panes (overlay z-index 2). The overlay wrapper
  ignores the pointer except on splitters.

### 2.2 Sidebar row

- One **group row** replaces the tabs: a rounded container (`--border-radius-medium`, min-height = tab height,
  padding `0 2px`, `margin-inline: 2px`) holding the member tabs **side by side**, each `flex: 1`, content padding 4 px,
  with `--tab-min-height: 28px` (smaller than a normal row) when expanded. Between neighbours a **1 x 16 px
  separator** (`light-dark(rgba(0,0,0,.1), rgba(255,255,255,.2))`), hidden on the active group.
- The group gets the selected background (`--tab-background-color-selected`) when any member is selected
  (`hasactivetab`); the selected member's own background is `--zen-split-view-active-tab-bg` (toolbar element bg at
  60 %), no tab shadow. Hover: `--zen-toolbar-element-bg` on the whole container. Press scales the container to
  `--zen-active-tab-scale`.
- Close button: hidden on members; shown on **group hover** for a member whose width >= 70 px (container query) and
  that is not a pinned tab in its unloaded state (`:not([pinned]:not([pending]))` is the CSS test, read as: close shows
  for unpinned tabs and pending pinned ones; verify in LIVE CHECK 5). Reset buttons are hidden.
- **Collapsed (icon-only) sidebar**: members stack **vertically**, 34 px wide, the container has a 2 px
  `--zen-colors-border-contrast` outline, separators become 16 x 1 horizontal lines centred at the bottom of each tab.
- Pinned groups live in the pinned section and can be dragged/dropped into folders as a unit (`folders.md` 4.9).

## 3. Creating a split

| Input | Zen does |
|---|---|
| **Cmd+Opt+G** | `toggleShortcut("grid")`: splits the selected tab with the **next visible tab** in the list (wraps to the first); if several tabs are multi-selected, those. Needs at least 2 visible tabs |
| **Cmd+Opt+V** | the same with `vsep` (side by side) |
| **Cmd+Opt+H** | the same with `hsep` (stacked) |
| **Cmd+Opt+U** | `unsplitCurrentView` (see 6) |
| **Cmd+Shift+\*** (key `*`; Cmd+Shift+8 on a US layout) | **new empty split** (see 3.1); disabled when the selected tab is the empty tab or already split |
| Tab context menu -> **Join N Tabs** (multi-select, 2-4) / **Add Split View...** (single tab) | `contextSplitTabs`: 2+ tabs -> `splitTabs`; a single tab -> new empty split with that tab |
| Tab context menu on an existing split member -> **Split Out Tab** | unsplit that member (all-in-group: remove each) |
| Page context menu -> **Split Link to New Tab** | opens the link in a new tab and splits it with the current tab (`splitTabs([current, new], undefined, 1)` -> the **new** tab is selected) |
| Command palette / panel "New Empty Split" | as Cmd+Shift+* |
| Drag a tab from the sidebar onto the page (3.2) | `moveTabToSplitView` |
| Drag a tab over another tab in the sidebar (3.3) | splits the two |
| Click a tab row with pref `zen.splitView.enable-tab-click-split` on | see 3.4 |
| Glance -> Split (see `glance.md`) | `cmd_zenGlanceSplit` |

Rules (`splitTabs`): tabs are filtered (no hidden / empty tab, glance tabs mapped to their parent, de-duplicated);
fewer than 2 or more than 4 = **nothing happens**. If a tab is already in a group and the group has `MAX_TABS`
tabs: a toast **"Can't add more panels to the split view!"** (`zen-split-view-limit-toast`) and nothing else. Adding
tabs to an existing group re-balances: each existing node's size shrinks by `n/(n+1)`, the new leaf gets the rest
(prepended or appended depending on the path). A `gridType` change with no new tabs recomputes the whole layout tree
(this is what Cmd+Opt+G/V/H do on an existing split: re-layout).
After creating, the first tab (or `initialIndex`) is selected and the split activates **without animation**
(`zen-split-view-no-transition` for one frame).

### 3.1 New empty split (Cmd+Shift+*)

1. The current tab plus the hidden **empty tab** (`gZenWorkspaces._emptyTab`) are split (`vsep`, empty on the **right**,
   or on the left / top / bottom if started by a drag on that side; `hsep` for top/bottom). The empty pane shows
   nothing (transparent).
2. The **floating URL bar opens at once** (`gZenUIManager.handleNewTab(...)` with a close token; see
   `cmd-t-cmd-l-session.md`), as if Cmd+T was pressed, bound to the split session.
3. Pick a result / enter a URL: the empty tab is dropped out of the group, and the **real tab joins the split**
   (`splitTabs([...], gridType, topOrLeft ? 0 : 1)`): the new tab is selected and the pane appears with the usual
   layout transition.
4. Esc / clicking away / switching tabs without picking: `cleanup` removes the empty tab from the split. The group
   dissolves (it had only 2) and the original tab is shown full width again (the selected tab is the original when
   cancelled; when the cause was a switch, the switch wins).
5. While the session is open `cmd_zenNewEmptySplit` is disabled (tests `Split_View_Empty*`: the urlbar keeps its
   `zen-newtab` attribute, the empty tab is not in any group, picking a result gives two split tabs).

### 3.2 Drag a tab onto the page

(`zen.splitView.enable-tab-drop` = true; `onBrowserDragOverToSplit` on `#tabbrowser-tabbox`, `moveTabToSplitView`.)

- Preconditions: dragging a **single** tab from this window (not multi-selected), not already split, not a Live Folder
  item, the selected tab is not the empty tab, the current group has fewer than 4 tabs.
- The pointer must be **10+ px inside** the content area and within the outer **quarter** of it on some side; the
  nearest side (distance < quarter of that dimension) wins: **left, right, top or bottom**. The middle half does
  nothing.
- On entering the zone (once per drag): **haptic feedback** (`Services.zen.playHapticFeedback`), the selected tab
  switches to the previous tab (`_lastOpenedTab`, the tab that was current before the drag) so the page behind is that
  one, the drag image becomes a **200 x 250 card** (rounded 16, centred icon/label) (`#zen-split-view-drag-image`).
- **Preview animation, 0.1 s ease-out**: the content box gets padding on the chosen side equal to
  `size / (tabs+1)` (half for the first split) and a **fake browser** (`#zen-split-view-fake-browser`, `side=...`:
  2 px accent border, rgba(255,255,255,.1) fill, inner radius, big shadow) grows from 0 to
  `half - gap` in the gap on that side, pushing the page. Browsers get `min-width/min-height` pinned so the page
  does not reflow while the box animates.
- Dragging out of the fake browser (`dragleave`) or cancelling: the same animation reversed (0.1 s), the fake
  browser and padding are removed.
- **Drop** inside the preview: the dragged tab joins (see 3), selected, and the new pane sits on the dropped side of the
  pane under the drop; dropping back on the very tab being dragged creates an empty split on that side instead
  (3.1). Dropping outside the content area aborts.
- Pinned/Essential special-case as in section 1.

### 3.3 Drag a tab over another tab in the sidebar

(`zen.splitView.enable-drag-over-split` = true, `zen.splitView.drag-over-split-threshold` = **40** (%),
`zen.splitView.drag-over-split-delayMC` = **500** ms; code defaults are 25 and 300, the yaml values rule.)

- Single, non-split dragged tab over a single normal tab (not Essential, not glance, not in a split group, not a Live
  Folder item). Vertical band: the **middle 20 %** of the target row (the top and bottom 40 % are ignored; those are
  reorder zones). Horizontal half decides the side: **left half** = dragged tab goes left, **right half** = right.
- After the pointer rests in that spot for **500 ms**, a `zen-split-fake-tab` appears inside the target row on the
  chosen side (a placeholder cell, other drag indicators removed) and a drop lands as a split of the two tabs. Moving
  to another tab / side, or off, clears the timer and the fake tab.

### 3.4 Tab click split

`zen.splitView.enable-tab-click-split` (code fallback **false** in `tab-mjs.patch`; `prefs/zen/zen.yaml` sets **true**).
The patch adds, in `MozTabbrowserTab.on_click` right after Firefox's Alt-click handling (`trigger: "alt_click"`), a branch
for clicks that are not on the close button, audio icon or overlay icon: a tab **not in a split** runs
`contextSplitTabs(this)` (splits it with the selected tab; if the selected tab is already in a split it joins that one,
up to 4); a tab **in a split** is selected and **removed from the split**
(`removeTabFromGroup(..., { forUnsplit: true, changeTab: false })`). The patch alone does not show **which modifier**
reaches this branch (it belongs to the Alt/Accel-click path): LIVE CHECK 1.

## 4. Using a split

- **Click a pane**: selects that tab (mousedown, so also on first click into a page). The outline and the sidebar
  highlight move at once.
- **Click a split row in the sidebar**: selects the tab you clicked on inside the group; the group stays active.
- **Select a different tab** (not in the group): the split deactivates (panes hidden, splitters removed,
  `zen-split-view` attributes removed) and the single page fills the area; selecting a member again re-activates the
  same layout (`updateSplitView`). Pending (unloaded) members are loaded with their session history on activation.
- **Resize**: drag a splitter. Cursor `ew-resize` / `ns-resize` for the whole drag (`window.setCursor`), live
  (per animation frame), no transition (`zen-split-resizing`). Moving a bar pushes against the neighbours:
  each tab shrinks down to the minimum **7 %** (`zen.splitView.min-resize-width`) before the next one starts shrinking.
  A tree may nest a row inside a column, so a bar resizes only its own siblings.
- **Rearrange**: drag the grip button in a pane header (the drag starts only if the press is within the header
  button, not the right-edge 22 px, which is the unsplit button). On drag start: **rearrange mode** (panes
  `opacity .85`, `pointer-events: none` on pages, the dragged pane dims to **.2**, a 160 x 90 thumbnail is the drag image).
  Hovering another pane shows the **drop zone**: a translucent rect (`--zen-colors-secondary` 30 %) that animates its
  `inset` (0.08 s ease-out) to cover the **half** of that pane on the hovered side if the pointer is within the
  outer **24 %** (`zen.splitView.rearrange-edge-hover-size`; note the yaml spells it `rearrange-hover-size` and is
  not read) of the pane on that edge, or the **whole pane** when the pointer is in the centre. Drop in the
  centre = **swap** the two panes; drop on an edge = the dragged pane is **removed from its place and inserted
  beside** that pane on that side at 50 % (`splitIntoNode`). Rearrange mode exits on Esc, a left click, or any tab change.
  (`enableTabRearrangeView` is called from the header-grip drag; the locale has an "Activate reallocation" item under
  the "Split View" modifier header, but no caller in the 1.24b source: treat it as unused.)
- **Pane header unsplit button**: click = remove that tab from the split and **select it** (it becomes the single
  page); Shift+click = remove it without selecting it (the selection stays where it was). The other panes re-flow;
  with 3 or 4 tabs the node is removed and sizes redistributed (`removeNode`: others grow by
  `100 / (100 - removed)`); with 2 tabs the group is dissolved.
- **Closing a member tab** (Cmd+W, middle click, close button): `handleTabClose` -> `removeTabFromGroup`. With 3-4 tabs
  the pane disappears and sizes redistribute; if it was selected, the group's first tab is selected. With 2 tabs the
  group dissolves and the remaining tab is selected full-width.
- **Unloading** a member (reset/auto-discard) unloads the **whole group** together (`handleTabBrowserDiscarded`, also
  its glance tabs).
- **Split Link to New Tab** (page context menu) is disabled when the active group is full
  (`maybeDisableOpeningTabOnSplitView`), and so is Glance -> Split.
- **Location change / tab select** in any pane re-runs `onLocationChange`: closes rearrange mode, re-activates the
  layout, removes a leftover fake browser, enables/disables New Empty Split.
- **Fullscreen** (DOM): outlines and headers hide; only the pane in fullscreen shows (`inset: 0`).
- **Session restore**: split data is saved per group (`storeDataForSessionStore`: tabs, grid type, layout tree sizes)
  and re-applied after the Spaces session restore (`AfterWorkspacesSessionRestore`); no animation, pending tabs load when
  the group activates.

## 5. Prefs

| Pref | Default |
|---|---|
| `zen.splitView.enable-tab-drop` | true (drop a tab on the page edge) |
| `zen.splitView.enable-tab-click-split` | true |
| `zen.splitView.min-resize-width` | 7 (%) |
| `zen.splitView.rearrange-hover-size` | 24 (%; the code reads `...rearrange-edge-hover-size`, default 24) |
| `zen.splitView.enable-drag-over-split` | true |
| `zen.splitView.drag-over-split-delayMC` | 500 (ms) |
| `zen.splitView.drag-over-split-threshold` | 40 (%) |

Constants: MAX_TABS 4; layout transition 0.09 s; drop preview 0.1 s; header fade 0.1 s (+0.1 s delay); drop-zone 0.08 s;
sidebar member min-height 28.

## 6. Unsplit

- **Cmd+Opt+U** (`unsplitCurrentView`): dissolves the **active** split group: the group is removed, every tab is
  ungrouped (it keeps its own sidebar row, pinned state unchanged), the layout is discarded, and the currently
  selected tab stays selected and fills the area. No animation (`no-transition`).
- Pane header unsplit button / tab menu **Split Out Tab**: remove a single member (section 4).
- Removing leaves tabs where the group was in the sidebar, in group order.

## 7. Edge cases worth copying

- 5th tab: toast, nothing else; 1 tab: nothing.
- The split group is **one** sidebar item (selection, drag, context menu on the whole row).
- Hidden/empty tab are never part of a split except transiently (3.1).
- Pinned + unpinned mix duplicates the pinned tab(s); Essentials are never moved.
- Splitters resize only the siblings of that node; min 7 %.
- A tab dragged into the content area while the middle half is hovered does nothing (no preview).
- Drag preview needs `draggedTab.documentGlobal === window` (another window's tab is ignored).

> **Live note (2026-11-04):** Cmd+Opt+V/H/G fired from synthesized key events; **Cmd+Opt+U did not** (the `cmd_zenSplitViewUnsplit` command never ran,
> the split was only deactivated). Calling `gZenViewSplitter.unsplitCurrentView()` dissolves the group correctly. Treat the
> shortcut failure as an artefact of synthesized key events (Option+U is a macOS dead key); the shortcut stays as specified.

## 8. LIVE CHECK list

1. Which modifier triggers the click-split (3.4) and whether a plain click on a tab inside the split row unsplits.
2. Pane gap (px), active outline colour (sample) in light and dark, header colours/sizes at 2x.
3. Drag-to-edge preview: frame timings (expect ~100 ms), haptics not observable; the fake browser colours and the
   drag card look.
4. Rearrange: drop-zone look (colour, inset) and swap/insert results for 3 and 4 panes.
5. Sidebar row look: separators, the active member colour, collapsed-sidebar stacking, close button reveal.
6. After "new empty split" Esc: which tab is selected and whether the URL bar pill shows anything.
7. Whether Cmd+Opt+G on an existing 2-split with a third tab selected adds it (up to 4) or shows the toast.
8. Cmd+W on a member of a 3-pane split: which tab is selected afterwards.

## 9. Test script (Mac loop; Zen and Koan)

1. Two tabs A, B (B selected). Cmd+Opt+V: A and B side by side (50/50), B outlined; hover the top of a pane: the
   header with grip and x appears after ~0.1 s; sidebar shows one row with both titles and a 1 px separator.
2. Click A's pane: outline moves to A. Drag the splitter 30 % right: A 80 %, B 20 %, never below 7 %.
3. Header x on A: unsplit, A selected, B back in the list. Re-split; header x with Shift: B stays selected.
4. Cmd+Opt+H, Cmd+Opt+G on the same two: stacked, then side by side again (grid with 2 = row).
5. Add C, D with the context menu "Join 4 Tabs": 2x2 grid. Try a 5th: toast "Can't add more panels to the split view!".
6. Close D with Cmd+W: three panes remain (the others grow to fill the space); LIVE CHECK 8 for which tab is selected.
7. Cmd+Opt+U: all single again, the selected tab fills the area.
8. Cmd+Shift+*: URL bar opens, current page is half, the other half empty; type a URL, Enter: second pane loads and is selected. Repeat and
   press Esc: back to one pane.
9. Drag tab C from the sidebar to the right edge of the page: preview slides in (100 ms); release: split.
10. Drag tab over tab D in the sidebar, hold 500 ms in the middle band on its right half: fake tab appears; release: split with C on the right.
11. Grab a pane's grip, drop it in the centre of another pane: they swap; drop on its left edge: inserts beside.
12. Pin a split tab: the group goes to the pinned section. Relaunch: layout restored.
