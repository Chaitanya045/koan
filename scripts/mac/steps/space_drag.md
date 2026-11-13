# Mac steps: dragging tabs across Spaces, reordering Spaces, the Essentials promo (#76)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome, real pointer (trackpad and mouse).
Zen's reference: `ZenDragAndDrop.js` (`zen.workspaces.dnd-switch-padding` 20 px, `zen.tabs.dnd-switch-space-delay`
500 ms), `ZenSpaceIcons.mjs`, `ZenEssentialsPromo.mjs`.

Prepare: three Spaces A, B, C (A active) with a few tabs each, no Essentials, sidebar expanded.

| # | Do | Expect |
|---|---|---|
| SD0 | **Mandatory, high risk.** DCHECK build. Drag one tab, then a 3-tab selection, across Spaces both over a Space button and by holding at the edge; drop in each case. Repeat with Esc mid-drag. | No CHECK/DCHECK, no crash, the tabs land in the new Space. Since #372 Koan's drag (`KoanTabDrag`) only switches the Space while it runs and moves the tabs on the drop; upstream's `TabDragController` waits on the press the whole time and is destroyed at the drop. |
| SD1 | Drag a tab of A and hold it at the sidebar's right edge (within 20 px, inside or over the page) | After half a second B slides in, the drag image still under the pointer (the tab itself stays in A); after another half second C, then A if Settings > Tab Management wraps around (else it stops at C). Drop on the list: the tab joins that Space where the line was, and is selected. |
| SD2 | The same at the left edge | Previous Space (C from A). |
| SD3 | Hold at the edge, then move to the middle before 500 ms | No switch. |
| SD4 | Drag a tab over B's button in the bottom bar | B shows at once; move back over the list and drop it there: it joins B at the line. Drop it on the button instead: it joins B at the end of its tabs. |
| SD5 | Drag an Essential (after SD9) over a Space button | Nothing. Hold it at the edge: the Space switches, the Essential stays an Essential. |
| SD6 | Drag several selected tabs to the edge | All of them go along. |
| SD7 | Drag a tab to the edge, let the Space switch, then Esc | The drag is cancelled, the switch too: A shows again on the tab you had, the same tabs selected, the image glides back onto its row. Dropping over the page does the same. No crash. |
| SD8 | Collapse the sidebar to icons (if reachable) and repeat SD1 | No switch at the edges (Zen needs the sidebar expanded); the Space buttons still switch. |
| SD9 | With no Essentials, drag a tab up above the Space header | "Add to Essentials / Keep your favorite tabs just a click away" appears under a heart, pushing the header down; over it the fill lightens and the dashed outline goes. Drop on it: the tab becomes an Essential and the promo goes. Drop elsewhere: the promo goes. |
| SD10 | SD9 with an Essential already there | No promo. |
| SD11 | Drag a tab out of the window while the promo shows, and release there | The promo goes, the image glides back onto the tab's row, nothing tears off; no crash. |
| SD12 | Press a Space button and drag it along the bar past another | After 5 px the others fade to 20% and grey; it jumps before/after the button under the pointer with a trackpad tick each step; release: the order stays (bottom bar, Library's Spaces, ⌃⌥←/→ order) and survives a restart. |
| SD13 | SD12 in a narrow sidebar (column of buttons) | The same, vertically. |
| SD14 | Click a Space button without moving | Switches as before, no fade. |
| SD15 | Compare SD1, SD9 and SD12 side by side with Zen 1.24b, light and dark | Same timing and look. |
