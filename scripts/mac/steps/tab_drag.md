# Mac steps: Koan's own tab drag in the sidebar (#372)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome, real pointer (trackpad and
mouse), Zen 1.24b open beside it for the side-by-side steps. Zen's reference: `src/zen/drag-and-drop/ZenDragAndDrop.js`,
`vertical-tabs.css` (`#zen-drag-indicator`, `#zen-dragover-background`), `nsZenDropLandingCocoa.mm`. Koan:
`KoanTabDrag` (`koan_tab_drag.h`), `KoanTabDragImage`, `PlaceInSpaceList()` (`koan_space_drag_logic.h`).

Prepare: two Spaces A and B, A with two pinned tabs, a folder of two tabs and five normal tabs; two Essentials; the
sidebar expanded; a second Koan window beside the first.

| # | Do | Expect |
|---|---|---|
| TD1 | Press a normal tab and move the pointer a few px | Nothing until Views' drag threshold; a click without moving still selects the tab; the close button and the pinned tab's reset button still click (no drag from them). |
| TD2 | Drag a normal tab down the list slowly | The tab stays in its place; an opaque copy of its row (label cut short, corners rounded) follows the pointer, held where you grabbed it, over the sidebar and over the page. No row moves. A 2 px line with a hollow ring at its left end snaps above or below the row under the pointer by its halves, from 10 px in to 2 px before the row's end. A trackpad tick each time the line moves, but not the first. |
| TD3 | Drop it | The copy glides (about 0.2 s, easing out) onto the tab's new place, where the tab shows once it lands; the rows around settle; the line goes at the drop. |
| TD4 | Drag the last tab into the empty space below the tabs, then over the separator, then over **+ New Tab** | Below the tabs: the line under the last tab. Separator: under the last pinned item, the drop pins the tab (last of the pinned section). New Tab row: above the first normal tab, the drop leaves it a normal tab. |
| TD5 | Select three tabs (⌘-click) and drag one | Three stacked copies, each 4 px right and down of the one before, the back ones faint, a red dot with "3" off the top-left corner; the pointer at 18, 18 on the front copy. The drop moves all three, in their order. |
| TD6 | Drag a pinned tab below the separator and a normal one onto the Space header | The first becomes a normal tab (pinned URL gone); the header lights with the full-width bar and the second becomes the first pinned tab. |
| TD7 | Drag an Essential | Its tile hides while the copy (a tile) follows the pointer. Over another tile an upright line shows the slot; the drop reorders the grid. Over the Space's list the line shows where it leaves the grid for (pinned above the separator, normal below). |
| TD8 | Drag a tab onto the Essentials grid (and, with no Essentials, onto the promo) | As `pin_drag` / `essentials_drag` (flows below): the promo shows and lights; the drop makes it an Essential where the line is. |
| TD9 | Drag a tab to B's Space button, back onto the list, drop | B shows at once; the tab stays in A while you drag (only the copy follows); the drop puts it in B where the line is, selected. Drop on the button instead: at B's end. |
| TD10 | Drag a tab, switch to B (button or edge), press Esc | A shows again on the tab you had, the selection as it was; the copy glides back onto the tab's row. Same for a release over the page or out of the window. |
| TD11 | Compact mode (sidebar hidden): reveal the sidebar, drag a tab out over the page and back | The sidebar stays out for the whole drag and the landing, then hides as usual when the pointer leaves. |
| TD12 | Drag a tab from window 1 onto window 2's sidebar | Over window 2 the copy goes and upstream's drag takes over: the tab moves to window 2 where dropped, as before #372. With every tab of a Space selected: nothing happens over window 2 (Koan keeps the drag; the drop puts them back). |
| TD13 | Drag every tab of window 1 | The drag stays in the sidebar (no window move, which upstream's drag did). |
| TD14 | Keyboard: focus a tab, ⌃⇧PageUp / PageDown; VoiceOver on a tab | Moves as before; VoiceOver reads the tabs, never the drag's views. |
| TD15 | Reduced motion on (System Settings > Accessibility > Display), TD3 and TD10 | No glide: the copy disappears at the drop, the tab shows at once. |
| TD16 | Side by side with Zen 1.24b, light and dark, shadcn and Zen styles: TD2, TD3, TD5 | Same image, line, ring, dot and timing. The landing's length is AppKit's in Zen: note any difference from Koan's 200 ms. |
| TD17 | Switch Space mid-drag with a Space that has its own theme | The copy takes the new Space's colours (Zen recolours its image on macOS). |

Flows to re-run: `scripts/mac/video/flows/pin_drag.mjs` (rounds 1 and 4 now expect the line and no separator shift,
round 1 the drag image), `essentials_drag.mjs`, and e2e `space_drag` (its Esc leg now expects the Space it started in).
