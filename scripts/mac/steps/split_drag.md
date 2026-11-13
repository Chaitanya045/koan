# Mac steps: split by drag (#372)

Hands-on, Zen 1.24b beside it. Zen's reference: `ZenDragAndDrop.js` (`_handle_tabDragOverToSplit()`,
`#createFakeTabSplit()`, `_handle_dropCreateSplit()`), `ZenViewSplitter.mjs` (`onBrowserDragOverToSplit()`,
`_animateDropEdge()`, `#getDragImageForSplit()`), `zen-split-view.css`. Koan: `KoanTabDrag` (`koan_tab_drag.h`),
`SplitByDrag()` (`koan_split_view.h`), `SplitBandSide()` / `PageSplitEdge()` / `FakePaneBounds()`.

Prepare: a Space with tabs A, B, C (A shown), one split view S of D and E, an Essential.

| # | Do | Expect |
|---|---|---|
| SP1 | Drag C over B's row, at its vertical middle, left half; hold | The line shows first; after half a second B's left half fills with the fake tab (primary colour, rounded), the line gone. |
| SP2 | Move to B's right half | The fake tab goes; half a second later it shows on the right. Off the middle band (top or bottom 40 %): the line again. |
| SP3 | Drop while the fake tab shows (right half) | No glide: the image goes at once. B and C form a split side by side, C on the right, selected. |
| SP4 | Drag A (the shown tab) onto the page, near its right edge (inside the outer 10 px: nothing) | The image turns into Zen's card (favicon above the bold title); a tick; a pane with a primary border slides in from the right in 0.1 s. Move to the middle: it slides back and the image is a row again. |
| SP5 | Press another tab (B) and drag it onto the page's left edge, drop | The page shows A again as the pane opens (the tab shown before the press); the drop splits B (left) with A (right), B selected. Top / bottom edges: stacked. |
| SP6 | Drag the shown tab onto its own page's edge and drop | Zen's empty split: the new pane on that edge, the URL bar over it. |
| SP7 | Show S and drag a tab onto the page's edge | Zen's toast "Can't add more panels to the split view!" once; no pane. Over D's or E's row: no fake tab. |
| SP8 | Drag two selected tabs, or an Essential, over a row's middle and the page's edge | No fake tab, no pane. |
| SP9 | Press D (one half of S's row) and drag it down among the normal tabs, drop (#380) | The image is S's whole row, both tabs' icons and titles. S moves there whole, still split; nothing unsplits. Same pressing E. |
| SP10 | Esc while the fake tab or the fake pane shows | It goes; nothing splits; the tab shown before the drag is selected again. |
| SP11 | Reduced motion on: SP4 | The pane shows at once, no slide. |
| SP13 | Show S, drag D's pane grip (its header's left button) onto the sidebar, over C's row's top half (#380) | The drop zone over the other pane goes; the line shows above C (a tick as it moves, not the first); over a folder's middle the folder lights. Back over E's pane: the drop zone again, no line. |
| SP14 | Drop it there | D leaves the split, lands above C, selected; E alone. With Shift held at the drop: same, the selection stays on E. |
| SP15 | SP13, then Esc, or drop over the Essentials or the Space buttons | Nothing changes; the line goes. While a sidebar tab drag's image lands (drop a tab, then move over S at once), S's headers stay hidden until it has landed. |
| SP12 | Side by side with Zen, light and dark, both styles: SP1, SP4 | Same colours, sizes and timing. Known difference: Zen pads the page aside for its fake pane; Koan's covers the page. |
