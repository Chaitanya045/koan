# Spaces: switching, swipe, creating, deleting, moving tabs

Zen 1.24b. Source: `src/zen/spaces/ZenSpaceManager.mjs` (3706 lines), `ZenSpacesSwipe.mjs`,
`ZenSpaceAddSwipe.mjs`, `ZenSpaceCreation.mjs`, `ZenSpace.mjs`, `ZenSpaceIcons.mjs`,
`src/zen/drag-and-drop/ZenDragAndDrop.js`, `prefs/zen/workspaces.yaml`,
`locales/en-US/browser/browser/zen-workspaces.ftl`, Zen's tests in `src/zen/tests/spaces/`.
Live: spec sections 3, 6, 11, 2A (sizes, menus, shortcuts).

## 1. Model

- A **Space** = `{uuid, name, icon, theme{type,gradientColors,opacity,texture}, containerTabId}`
  (`#createWorkspaceData`). Stored in the session store (`zen-sessions.jsonlz4`), per
  window set (synced between Zen windows unless a window is "unsynced").
  `containerTabId` is the Firefox container (profile): **out of scope for Koan** (no
  containers on Chromium; Koan profiles are #111).
- Every tab carries `zen-workspace-id`. **Essentials** (`zen-essential`) belong to no Space
  and are shown in all of them (`zen.workspaces.separate-essentials` true means: *per
  container*, i.e. all Spaces of the same container share one Essentials grid; with no
  containers = one grid for everything). The **empty tab** is moved into whichever Space is
  active.
- Default Space: name **"Space"**, no icon (the header shows no icon; `icon` undefined),
  theme default (no colours, opacity 0.5, texture 0). In a **private window** the single Space
  is named **"Incognito"** with the icon `private-window-small.svg`, and Spaces are disabled
  (`privateWindowOrDisabled`): no creation, no switching, no swipe, no wheel.
- `zen.workspaces.active` stores the active Space id (restored at startup).
- Each Space has its own tab section in the sidebar: pinned container + tabs container +
  "+ New Tab" button, laid out **side by side as a horizontal strip** (each `zen-workspace`
  element is `translateX(100% * offset)`, offset wraps around the shortest way), so switching is a
  slide, not a swap.

## 2. Switching

### Inputs

| Input | Result |
|---|---|
| **Cmd+Opt+Right / Left** (`cmd_zenWorkspaceForward/Backward`) | next / previous Space (`changeWorkspaceShortcut(+1 / -1)`), **wraps around** (`zen.workspaces.wrap-around-navigation` true; when false it clamps at the ends) |
| **Ctrl+1 ... Ctrl+9, Ctrl+0** (`cmd_zenWorkspaceSwitchN`) | Space 1..10 (`shortcutSwitchTo(index)`): ignored if that Space doesn't exist |
| Click a Space button in the bottom bar (17 x 32 each, centred) | switch to it; the active one shows its icon at full colour, the others a dot or dimmed icon; with many Spaces the buttons shrink to fit (`icons-overflow`, min/max button widths, `ZenSpace.mjs`) |
| Pick a Space from the **Spaces** menu-bar menu (a row per Space with its icon and Ctrl+N) or from the more-actions menu (when opened from the sidebar, not from a Space button) | switch |
| **Trackpad swipe** (2-finger horizontal on the sidebar; `zen.workspaces.swipe-actions` true) | see section 3 |
| **Mouse wheel / scroll**: horizontal scroll, or vertical scroll **with the activation modifier** (`zen.workspaces.scroll-modifier-key` = ctrl) over the sidebar | next/previous Space; **200 ms** cooldown between steps; only line-mode wheels (`deltaMode == 1`, i.e. mouse wheels, not trackpad pixel scrolls); `zen.workspaces.natural-scroll` (false) inverts |
| **Mouse Back / Forward buttons** (AppCommand) while the pointer is over the sidebar | previous / next Space; also forces the sidebar visible |
| **Drag a tab** to the left/right edge of the sidebar (`handle_sidebarDragOver`) and hold `zen.tabs.dnd-switch-space-delay` = **500 ms** | switches Space while dragging; hovering a Space button during a tab drag switches to that Space after the same delay (`zen.workspaces.dnd-switch-padding` = 20 px edge band) |
| `zen` command line / URL with a Space | `changeWorkspaceFromCommandLine` |

### Result and timing

`changeWorkspace(workspace)`: finishes any running switch animation first (`animation.complete()`),
then `#performWorkspaceChange`:
1. set `activeWorkspace`; mark each Space element `active`; make sure the empty tab is first in the
   new Space; point `gBrowser.pinnedTabsContainer` at the new Space's;
2. pick the tab to select (`_handleTabSelection`): the Space's **last selected tab** if it is still
   there; else the first non-pinned tab; else the last visible tab; else the **empty tab**
   (and a Space with no tabs shows the empty state, blank pill);
3. `gBrowser.warmupTab(tab)`; theme update (`gZenThemePicker.onWorkspaceChange`) next animation frame;
4. **animate** (`#animateTabs`, only if the window is not initialising): spring, **bounce 0**,
   duration `zen.workspaces.switch-animation-duration` = **250 ms**:
   - each Space strip animates its `transform` from the current to the new `translateX(offset*100%)`; only
     the Spaces **between** the old and new position are animated (others jump); direction = the shorter way
     round (`diff` wraps);
   - the **window background** cross-fades: `--zen-background-opacity` animates from 0 (old gradient
     shown as `...-old`) to 1 on the browser and toolbar background layers (`animating-background` set);
   - the **Essentials grid** slides only if the Essentials differ between the two Spaces (different
     container): `shouldAnimateEssentials`; with one shared grid it does not move;
   - the Space header (name/icon) changes at the end (`updateWorkspaceIndicator`), bottom-bar active dot
     changes immediately (`workspaceIcons.activeIndex`);
   - the selected tab changes when the strip reaches offset 0 (`gBrowser.selectedTab = tabToSelect`).
5. A second switch started during the animation completes the first instantly then starts.
6. After: update the Spaces menu, bookmark containers, Ctrl-Tab scope (`#fixCtrlTabBehavior`: Ctrl+Tab cycles
   the current Space's tabs plus Essentials, unless the ctrl-tab prefs say otherwise).

## 3. Swipe (macOS two-finger)

`ZenSpacesSwipe` on the sidebar (`MozSwipeGesture*` events), only when `zen.workspaces.swipe-actions`
and not in a private window / disabled / while a switch is running. Not started on the floating URL bar, the
Space buttons or the download list.
- **Start**: swipe allowed in both horizontal directions; sidebar items get `swipe-gesture` (no pointer
  events, no scrollbars, grain transitions off).
- **Update**: `translateX = lastDelta + event.delta * deltaMultiplier` (`zen.workspaces.swipe-actions.delta-multiplier`
  = **100**), with a rubber-band `forceMultiplier = min(1, 1 - |x| / (stripWidth * 4.5))`; while > 0.5 the
  movement is damped, below that it stops. The strips follow the finger (`_organizeWorkspaceStripLocations(...,
  offset)`, offset in %/2), the **next Space's gradient fades in** (`--zen-background-opacity = 1 - |offset|/200`)
  and the Essentials/grain interpolate.
- **Direction decision** (first movement): towards the Library side = **Library gesture** (opens the Library
  panel as you swipe, `ZenLibrary.swipeProgress`, only if it can open); away from it **past the last Space** =
  **Add Space gesture** (`ZenSpaceAddSwipe`, a "+" ring that fills; success at **0.8** of its travel, 0
  velocity contribution; entry travel 55 px, spaces translate -25 px; on success it opens the **Create Space**
  form); otherwise it is a normal Space swipe.
- **End** (`MozSwipeGesture`): the Space swipe commits if the travel exceeds **0.25** of the strip width (with
  velocity contribution **0.5**); direction mapping respects RTL and natural scroll;
  `changeWorkspaceShortcut(dir, true)`. If not committed it springs back
  (`_cancelSwipeAnimation`).
- **Cleanup** (`MozSwipeGestureEnd` or a popup showing): background opacity back to 1, grain restored, containers
  updated. `zen.widget.macos.override-system-swipe-gestures` (true) stops macOS from using the swipe for
  Back/Forward.
- `zen.workspaces.swipe-actions.edge-actions` (true): swipe at the ends does the Library / Add Space actions.

## 4. Creating a Space

| Input | Result |
|---|---|
| Spaces menu **Create Space**, the bottom-bar **"+"** (opens a small menu: New Folder, New Space ...), the Library, the swipe past the last Space | `openWorkspaceCreation()` |
| What happens | sets `zen-creating-workspace="true"` on the window; creates a Space named **"Space"** inserted **right after the active Space**; shows the **creation form** (`zen-workspace-creation`) *in place of that Space's tab area*, sliding in with the Space strip (no Essentials for it while creating); the new Space becomes active |
| The form | title "Create a Space", description "Spaces are used to organize your tabs and sessions.", an **icon button + name field** (emoji picker `ZenEmojiPicker`, or the 87 `selectable/` icons), "Theme..." button (opens the theme picker; panel offset y -160 here), **Create Space** (disabled until named) and **Cancel**; Enter creates, Esc cancels |
| Create | saves name/icon/theme, removes the form, switches to the Space (empty tab shown), emits the Spaces UI update |
| Cancel | deletes the half-created Space and goes back to the previous Space (animated) |
| Orphan tabs | tabs without a Space id (e.g. just opened) are adopted by the **new** Space (`#prepareNewWorkspace`: every tab with no `zen-workspace-id` that isn't pinned/Essential moves into it); if there are none the empty tab is selected |
| Default name/icon when created without the form (command-line, sync) | name "Space" (or the container's label); icon none |

## 5. Editing a Space

- **Rename**: double-click the Space name in the header (inline field), or menu "Change Name...": Enter saves,
  Esc cancels, empty name rejected. Koan's existing port shows a "renamed Space" toast (`koan_zen_toasts.h`, from `zen-general.ftl`): **LIVE CHECK** when it appears.
- **Change icon**: "Change Icon..." / click the icon in the header -> emoji picker (emojis + `selectable/` icons +
  "no icon").
- **Theme...**: opens the theme picker (see the theme-picker script).
- **Reorder**: drag a Space button in the bottom bar or a card in the Library "Spaces" view
  (`reorderWorkspace(id, position)`).
- **Set Profile** (container): out of scope.

## 6. Deleting / unloading

| Input | Result |
|---|---|
| More-actions menu (header "..." / Space button right-click) **Delete Space** | confirm dialog **"Delete Space?"**, body "Are you sure you want to delete {name}? This action cannot be undone."; OK -> `removeWorkspace`: **all non-Essential tabs of that Space are closed** (`removeTabs`, `closeWindowWithLastTab: false`), including its pinned tabs and folders; the Space is removed from the list; another Space becomes active (**LIVE CHECK** which one: the code path is `removeWorkspace` -> `ZenWorkspacesUIUpdate` -> the manager picks the active one); the empty tab is not closed |
| **Unload Space** | `gBrowser.explicitUnloadTabs` for every loaded tab of that Space (Essentials and the empty tab excluded): memory freed, rows greyed (pending), nothing closed |
| **Unload All Other Spaces** | same for every other Space |
| **Clear** (divider button) / Cmd+Shift+K | `closeAllUnpinnedTabs`: closes all unpinned tabs of the active Space **except** selected, multiselected, picture-in-picture, sound-playing and active-webrtc tabs (`#getClosableTabs`; if none are safe, closes all) and shows the toast **"Tabs Closed! Use Cmd+Shift+T to undo."** (`zen-workspaces-close-all-unpinned-tabs-toast`); if the Space ends with no tabs the empty tab is shown |
| Last remaining Space | cannot be deleted (menu item disabled when there is one Space) **LIVE CHECK** |
| Delete in a private window | disabled |

## 7. Moving tabs between Spaces

| Input | Result |
|---|---|
| Tab context menu **Move Tab > Move to Space** (`context_zenChangeWorkspaceTab`: a submenu with the other Spaces and their icons) | `changeTabWorkspace(id)`: moves the tab (or the multiselection) to the end (or **start** when `zen.view.show-newtab-button-top` true, the default) of that Space's tabs or pinned container (pinned stays pinned; **Essentials cannot be moved**), **then switches to that Space** and selects the moved tab; a **split-view group moves as a unit**; a Glance tab child follows its parent |
| Drag a tab onto a Space button / the sidebar edge and hold 500 ms | switches Space while dragging, then the drop lands in the new Space |
| Opening a link from a Space | the tab opens in the **current** Space (Space routing can override: out of scope) |
| Dragging a tab into the Library "Spaces" card | moves it into that Space |

If the moved tab was the "last selected tab" of its old Space that memory is cleared.

## 8. Other behaviours

- **Closing the last tab of a Space** and the empty tab: see close-last-tab-and-empty-tab.md.
- **Scrolling a long tab list**: each Space has its own scrollbox; `zen.view.show-scroll-to-selected-tab` (true)
  shows a "scroll to selected" affordance.
- **New window / sync**: a new window copies the Space list and active Space (`zen.window-sync.enabled` true);
  "New Blank Window" (Cmd+Shift+N) is an *unsynced* window with only its own Space; `zen.window-sync.sync-only-pinned-tabs`
  (false) limits tab sync.
- **Essentials rule**: see essentials-and-pinned.md.
- **Switching while the floating URL bar / a Glance is open**: the URL bar closes on tab switch
  (`onSwitch`, text discarded); a Glance is closed.
- **Reduced motion** (`gReduceMotion`): no slide (the animations are skipped).

## 9. Prefs

| Pref | Default |
|---|---|
| `zen.workspaces.wrap-around-navigation` | true |
| `zen.workspaces.swipe-actions` / `.delta-multiplier` / `.edge-actions` | true / 100 / true |
| `zen.workspaces.natural-scroll` | false |
| `zen.workspaces.scroll-modifier-key` | ctrl |
| `zen.workspaces.switch-animation-duration` | 250 ms |
| `zen.workspaces.separate-essentials` | true (per container) |
| `zen.workspaces.open-new-tab-if-last-unpinned-tab-is-closed` | false |
| `zen.workspaces.continue-where-left-off` | false |
| `zen.workspaces.dnd-switch-padding` | 20 px |
| `zen.tabs.dnd-switch-space-delay` | 500 ms |
| `zen.workspaces.hide-default-container-indicator` | true |
| `zen.workspaces.force-container-workspace` | false |
| `zen.widget.macos.override-system-swipe-gestures` | true |
| `zen.window-sync.enabled` | true |

## 10. LIVE CHECK list

1. Swipe on the real trackpad: the strip follows the finger; the commit distance (25% of the sidebar width) and the
   rubber band feel; the Add Space ring at the last Space; the Library opening from the first Space.
2. Cmd+Opt+Right with 3 Spaces: the slide direction on wrap (last -> first).
3. Delete Space dialog look (native alert) and which Space becomes active.
4. Whether the last Space can be deleted.
5. The "+" bottom-bar button menu contents (this file only knows "New Folder" / "New Space").
6. The Spaces menu rows' Ctrl+N hints (seen: Ctrl+1, Ctrl+2 for Work, Play).
7. Rename by double-click: inline field position and the toast.

## 11. Test script for the Mac loop (Zen and Koan)

1. Create Spaces "A" (default), "B", "C" (with Create Space; check the form; Esc cancels one attempt: no Space added).
2. Cmd+Opt+Right x3: A->B->C->A (wrap); record the slide (250 ms, bounce-free); the background cross-fades; the
   bottom-bar active dot moves immediately.
3. Ctrl+2, Ctrl+9 (non-existent: nothing), Ctrl+1.
4. In B open 2 tabs; select the second; go to A and back to B: the **same tab** is selected.
5. Move a tab from A to C through the tab context menu: after the move the window is on C with that tab selected.
6. Drag a tab to the left sidebar edge and hold: switches after ~500 ms.
7. Close all tabs of C: the empty state (blank pill); Cmd+Opt+Left: back to B.
8. Delete C: confirm dialog text; its tabs are gone; Space list shows A and B; active = a neighbour.
9. Clear (divider): playing-audio tab and the selected tab survive; toast "Tabs Closed! Use Cmd+Shift+T to undo."
10. Private window: no Space switching, the Space is "Incognito" with the mask icon.
11. Relaunch: the active Space and each Space's last selected tab are restored.
