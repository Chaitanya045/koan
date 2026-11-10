# Mac steps: dragging tabs across Spaces, reordering Spaces, the Essentials promo (#76)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome, real pointer (trackpad and mouse).
Zen's reference: `ZenDragAndDrop.js` (`zen.workspaces.dnd-switch-padding` 20 px, `zen.tabs.dnd-switch-space-delay`
500 ms), `ZenSpaceIcons.mjs`, `ZenEssentialsPromo.mjs`.

Prepare: three Spaces A, B, C (A active) with a few tabs each, no Essentials, sidebar expanded.

| # | Do | Expect |
|---|---|---|
| SD1 | Drag a tab of A and hold it at the sidebar's right edge (within 20 px, inside or over the page) | After half a second B slides in, the dragged tab is in it under the pointer; after another half second C, then A (wrap-around). Drop: the tab stays in that Space where dropped. |
| SD2 | The same at the left edge | Previous Space (C from A). |
| SD3 | Hold at the edge, then move to the middle before 500 ms | No switch. |
| SD4 | Drag a tab over B's button in the bottom bar | B shows at once with the tab; move back over the list and drop it there. |
| SD5 | Drag an Essential (after SD9) over a Space button | Nothing. Hold it at the edge: the Space switches, the Essential stays an Essential. |
| SD6 | Drag several selected tabs to the edge | All of them go along. |
| SD7 | Drag a tab to the edge, then Esc | The drag ends; the tab stays in the Space it was carried to (noted in the PR). No crash. |
| SD8 | Collapse the sidebar to icons (if reachable) and repeat SD1 | No switch at the edges (Zen needs the sidebar expanded); the Space buttons still switch. |
| SD9 | With no Essentials, drag a tab up above the Space header | "Add to Essentials / Keep your favorite tabs just a click away" appears under a heart, pushing the header down; over it the fill lightens and the dashed outline goes. Drop on it: the tab becomes an Essential and the promo goes. Drop elsewhere: the promo goes. |
| SD10 | SD9 with an Essential already there | No promo. |
| SD11 | Drag a tab out of the window (tear it off) while the promo shows | The promo goes within a quarter second; no crash. |
| SD12 | Press a Space button and drag it along the bar past another | After 5 px the others fade to 20% and grey; it jumps before/after the button under the pointer with a trackpad tick each step; release: the order stays (bottom bar, Library's Spaces, ⌃⌥←/→ order) and survives a restart. |
| SD13 | SD12 in a narrow sidebar (column of buttons) | The same, vertically. |
| SD14 | Click a Space button without moving | Switches as before, no fade. |
| SD15 | Compare SD1, SD9 and SD12 side by side with Zen 1.24b, light and dark | Same timing and look. |
