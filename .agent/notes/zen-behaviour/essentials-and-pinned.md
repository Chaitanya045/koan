# Essentials and pinned tabs

Zen 1.24b. Source: `src/zen/tabs/ZenPinnedTabManager.mjs` (1197 lines), `src/zen/spaces/ZenSpaceManager.mjs`
(`getEssentialsSection`, `onPinnedTabsResize`, `data-hack-type`), `src/zen/drag-and-drop/ZenDragAndDrop.js`,
`src/zen/tabs/zen-tabs/vertical-tabs.css`, `prefs/zen/workspaces.yaml`, `prefs/zen/zen.yaml`,
`locales/en-US/browser/browser/{zen-general,zen-vertical-tabs}.ftl`,
`locales/en-US/browser/browser/preferences/zen-preferences.ftl`. Live: spec sections 2A, 3, 11, 16.

## 1. Three kinds of top-level tab

| | **Essential** | **Pinned** | Normal |
|---|---|---|---|
| Attribute | `zen-essential="true"` (+ `pinned`) | `pinned` | none |
| Lives in | the **Essentials grid** at the top of the sidebar, shared by **all Spaces** of the same container (with no containers: all Spaces) | the **pinned section of one Space** (between the Space header and the divider; also inside folders) | the Space's tab list under "+ New Tab" |
| Max | `zen.tabs.essentials.max` = **12** (the context menu shows `n / 12`) | unlimited | n/a |
| Moves between Spaces | **never** (shown everywhere) | yes (stays pinned) | yes |
| Close (Cmd+W / middle-click) | never closes: unload + switch (see 4) | same | closes |
| Look | tile: 50 high, inner 46, radius 14, 16 px favicon centred (spec 2A/16) | a normal 218 x 40 row (36 pill, r14) | same |

## 2. Layout of the sidebar column

Row 1 -> URL pill -> **Essentials grid** (6 pt padding, 4 pt gap) -> **Space header** (44) ->
**pinned container** (pinned tabs and folders, a normal list) -> **divider** (22: 1 pt line + "Clear" on hover) ->
**"+ New Tab" row** (36) -> normal tabs -> bottom bar. The pinned container and the tab list scroll together
inside the Space's scrollbox; the Essentials grid is outside it and does not scroll.

### Essentials grid columns (the wrap rule)

`grid-template-columns: repeat(auto-fit, minmax(max(23.7%, tileMin), 1fr))`, gap 4. Four columns fit only when
the container (sidebar width - 12) is >= ~231 pt, so 7 tiles are 3+3+1 at the default 230 sidebar and 4+3 at
262. The tile count overrides the percentage (`data-hack-type` set by `onPinnedTabsResize` from
`essentialContainer.children.length`): **6 or 9 tiles -> 30%** (3 per row); an **even count < 8 (2, 4, 6->30%)**
-> paired columns `minmax(max(23%, min), 1fr) minmax(...)`; **exactly 5 -> `max(25%, 48px)`**; otherwise
23.7%. The container grows/shrinks height with a `max-height 0.3s ease-out` transition and columns
`grid-template-columns 0.3s ease-out` when the count or width changes. In compact/collapsed sidebar mode the
Essentials have `padding: 0`.

## 3. Making and removing Essentials / pins

| Input | Result |
|---|---|
| Tab menu **Add to Essentials** (badge `n / 12`) / **Remove from Essentials** (`context_zenAddEssential`...) | `addToEssentials`: refuses (silently, `movedAll = false`) when 12 are already there, or the tab's container differs from the active Space's container (when container-specific Essentials are on); otherwise sets `zen-essential`, **removes `zen-workspace-id`**, pins the tab, moves it to the **end of the Essentials grid**; if it was selected, `switchTabIfNeeded`; fires `TabAddedToEssentials`; recomputes layout. Multiselection adds all (until the limit) |
| **Remove from Essentials** | clears `zen-essential`, gives the tab the **active Space's** id, **unpins** it (default) -> it becomes a normal tab at the top of that Space's list; with `unpin=false` (drag into the pinned area) it becomes a pinned tab at the **start** of the Space's pinned container; fires `TabRemovedFromEssentials` |
| **Cmd+Shift+D** / menu "Pin Tab" / "Unpin Tab" (`cmd_zenTogglePinTab`) | toggles `pinned` on the selected tab (ignored for the empty tab; for a Glance it acts on the parent) |
| **Drag a tab onto the Essentials grid** | allowed only if it is not already Essential and `canEssentialBeAdded`; a drop indicator (insertion line / gap of 8 pt, haptic tick) shows where; dropping inserts it at that position |
| **Drag an Essential onto the pinned area or the tab list** | the "remove from Essentials" path (the area highlights), pinned/normal accordingly |
| **Drag a pinned/normal tab** into the pinned section | pins it at the drop position; drop onto a folder puts it in the folder |
| Onboarding Essentials (the 9 app tiles) | created as lazy (unloaded) tabs with the bundled favicon, added to Essentials, history entries inserted so they show in the URL bar |

## 4. Interacting with a pinned / Essential tab

### Clicking

- Click selects it (loading it if unloaded: a pending tab loads on first select, `pending` removed).
- A pinned tab navigating to **another domain** opens the new page in a **new tab** instead
  (`zen.tabs.open-pinned-in-new-tab`, true; `getOpenUILinkWhere`: the pinned tab keeps its URL). Same-domain
  navigation stays in the pinned tab.
- Once the user navigates a pinned tab away from its pinned URL (ignoring the `#fragment`):
  - the tab gets `zen-pinned-changed` and its pinned URL's icon is kept in `--zen-original-tab-icon`;
  - a **reset button** (`unpin.svg`, `.tab-reset-button`) appears on hover with the sublabel
    **"Back to pinned url"**; holding **Cmd** over it changes the sublabel to
    **"Separate from pinned tab"** (Cmd+click detaches the current page into a new tab and resets the pin);
  - returning to the pinned URL clears the changed state.
  - split-view tabs get `had-zen-pinned-changed` instead.

### Closing: Cmd+W, the sidebar close, middle-click

`onCloseTabShortcut` (bound to `cmd_close`, and to middle-click via `_onTabClick`) handles any selected
*pinned* tab (Essentials are pinned) per `zen.pinned-tab-manager.close-shortcut-behavior`
(default **`reset-unload-switch`**):

| Behavior (Settings > Pinned Tabs > "Close Tab Shortcut Behavior") | What Cmd+W does |
|---|---|
| **Reset URL, unload and switch to next tab** (default) | select another tab (`_findTabToBlurTo`), **unload** the pinned tab (it greys, `pending`), and **reset** it to its stored pinned URL |
| Unload and switch to next tab | same without the reset |
| Reset URL and switch to next tab | select another tab and reset, no unload |
| Switch to next tab | just select another tab |
| Reset URL | reset in place, stay |
| Close tab | really close it |

Details: an **Essential is never closed** (the `close` behaviour is downgraded to unload-switch for
Essentials: `noClose`); a pinned tab inside an **active folder** plays the folder's unload animation first
(`gZenFolders.animateUnload`); a **Glance** child closes through the Glance (waits for `GlanceClose`, max 3 s);
a **split-view group** unloads/resets all its tabs; **middle-click** on an already-unloaded pinned tab
**closes it for real** (`zen.pinned-tab-manager.wheel-close-if-pending` true) while the first middle-click
unloads it. `explicitUnloadTabs` can be refused (e.g. a tab with an active call), in which case nothing else happens.
Normal tabs are untouched by this handler.

### Context menu additions (pinned tabs)

"Reset Pinned Tab", "Replace Pinned URL with Current" (toast "Pinned tab URL has been replaced with the
current URL!"), "Edit Pinned URL..." (dialog "Edit Pinned URL" / "Enter the URL this pinned tab should point to:",
errors "That doesn't look like a valid URL.", toast "Pinned tab URL has been updated!"), "Change Label...",
"Change Icon..." (custom label/emoji for the pinned tab, `tab-context-zen-edit-title/icon`),
"Add to Essentials" / "Remove from Essentials".

## 5. Startup, sync, restore

- Pinned/Essential tabs are restored **unloaded** (`pending`) like all tabs.
- `zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url` (false): on startup reset each pinned tab to its
  original URL instead of where you left it.
- Essentials are shared across windows and Spaces; pinned tabs sync between Zen windows
  (`zen.window-sync.*`).
- Favicon background: `zen.theme.essentials-favicon-bg` (true) tints the Essential tile from its favicon
  (`--zen-essential-tab-icon` as a blurred backdrop, selected state `--zen-essential-tab-selected-bg`:
  dark `rgba(68,64,64,.85)` / light `rgba(255,255,255,.85)` over the Space colour).
- Essentials promo (`ZenEssentialsPromo.mjs`): a dismissible promo shown in the grid area when there are none
  (no Essentials yet): out of scope unless the user wants it.

## 6. Prefs

| Pref | Default |
|---|---|
| `zen.tabs.essentials.max` | 12 |
| `zen.workspaces.separate-essentials` | true (per container) |
| `zen.pinned-tab-manager.close-shortcut-behavior` | `reset-unload-switch` |
| `zen.pinned-tab-manager.wheel-close-if-pending` | true |
| `zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url` | false |
| `zen.tabs.open-pinned-in-new-tab` | true |
| `zen.theme.essentials-favicon-bg` | true |
| `zen.view.show-newtab-button-top` | true (new tab row above the tabs; a moved tab goes to the start) |
| `zen.workspaces.open-new-tab-if-last-unpinned-tab-is-closed` | false |

## 7. LIVE CHECK list

1. Cmd+W on an Essential: which tab is selected afterwards (the "blur-to" choice) and the unload look (grey).
2. Cmd+click on the reset button: what is created (the "separate from pinned tab" action).
3. The drag indicator between Essential tiles (line vs gap, haptics) and dropping on the right/bottom edge.
4. Adding a 13th Essential: any toast/shake or fully silent?
5. The Essentials tile hover and selected states (pixel colours; `zen-colors.json` only has the base tint).
6. Remove from Essentials: where exactly the tab lands (top of the list).
7. Whether the grid collapses height/animates when the last Essential is removed.

## 8. Test script (Mac loop; Zen and Koan)

1. Open 8 tabs; add 7 to Essentials via the menu: tiles 3+3+1 at sidebar 230, 4+3 at 262; the menu shows `7 / 12`.
   Add up to 12: the 13th "Add to Essentials" item is disabled/refused.
2. Create a second Space: the same Essentials are shown, selecting one switches nothing; **move-to-Space is absent**
   for an Essential.
3. Pin a tab (Cmd+Shift+D): it moves to the pinned section of this Space; switch Space: it is not there.
4. Pinned tab: navigate to another domain by link: opens in a new tab; navigate within the domain: stays, and
   no reset button; use the address bar to leave the pinned URL: the reset button + "Back to pinned url".
5. Cmd+W on the selected pinned tab (default behavior): another tab is selected, the pinned tab is grey
   (unloaded) and back at its pinned URL; middle-click it: closes. Change the pref to `close`: Cmd+W closes a pinned
   tab but never an Essential.
6. Drag an Essential into the pinned section: it becomes pinned at the position; drag back to Essentials.
7. Relaunch: Essentials in place, all unloaded.
