# Mac steps: Zen's split pane header (#373)

For the Mac loop and the hands-on tester. DCHECK build, a fresh profile, real pointer and keys
(`scripts/mac/tools`). The scripted part is `scripts/mac/suite.sh split_header` (real input when the user is
away); the rest is by hand. Compare with Zen 1.24b on the same Mac where a step says "as Zen".

Set up: two tabs A and B (any pages, e.g. example.com and wikipedia.org), A selected, ⌘⌥V (side by side).

| # | Do | Expect |
|---|---|---|
| 1 | Move the pointer onto A's page, anywhere | After ~0.1 s a small tab fades in at the middle of A's top edge, hanging from the gap above it (about 54 x 23 pt, square top, rounded bottom corners, a faint shadow), in the active pane's outline colour, with a grip (horizontal dots) and an expand glyph. None on B. Capture it next to Zen's (`.zen-view-splitter-header`). |
| 2 | Move onto B, then into the gap between the panes, then onto the sidebar | A's header fades out and B's in; over the gap (resize cursor) and the sidebar none shows. The resize handle still drags in the gap, also right under a header. |
| 3 | Hover A, move onto its header and back onto the page | The header stays (no flicker). Over the grip the cursor is the move cursor, over the expand button a pointing hand. |
| 4 | Hover the header's top 5 pt (in the gap) and press-drag there; also press-drag on the page's empty top just beside the header | On the header: no window drag (the header takes the press). Beside it: the window drags as before (#365's top 5%, active pane only). |
| 5 | Click A's expand button | The split goes; A is selected and fills the page; B is back as its own row. ⌘⌥V again. Shift-click B's expand button: the split goes, A stays selected. |
| 6 | ⌘⌥V again; drag A's grip a little | The panes dim (B slightly, A strongly, fading over 0.2 s); a 160 x 90 thumbnail of A follows the pointer, 16 pt down-right of it; the trackpad gives no tick (Zen gives none). |
| 7 | Keep dragging onto B: its middle, then within a quarter of its left, right, top and bottom edges | Over B's middle a tinted zone covers B; near an edge it covers B's half on that side, sliding there quickly (0.08 s). Over A or the gap: no zone. |
| 8 | Release over B's middle | A and B swap places; each side keeps its width (a 30/70 split stays 30/70); the sidebar's split row swaps too; the dimming fades away. |
| 9 | Drag a grip onto the other pane's bottom quarter and release; then onto a side edge of the other pane | Stacked, the dragged pane below, at half height; then side by side again, the dragged pane on that side, at half width. ⌘⌥H / ⌘⌥V still switch the layout afterwards. |
| 10 | Start a grip drag, press Esc, release over the other pane | Nothing moves; the dimming goes at Esc. Also: start a drag and switch tabs with ⌃Tab: the drag is cancelled. |
| 11 | A split with the page loading (reload a slow page in the active pane) and the pointer over it | Zen's loading pill shows above the active pane; the header, when shown, is drawn above it. |
| 12 | Enter a video's fullscreen in a pane (YouTube), move the pointer | No header. Leave fullscreen: it comes back on hover. Native fullscreen of the window (⌃⌘F): the header still shows on hover, at the panes' tops. |
| 13 | Open Glance over the split (⌥-click a link) and move over the card and the dimmed page | No header. Close Glance: hovering a pane shows it again. |
| 14 | Compact mode with the sidebar hidden; reveal the sidebar over the left pane | No header while the pointer is over the floating sidebar; it shows once the pointer is on the page again. |
| 15 | Space theme with a dark colour scheme in a light system (or the reverse) | The header follows the window's (the Space's) scheme, like the toast and Glance. shadcn look (default) and Zen look: the header matches the active pane's outline in each. |
| 16 | Drag a sidebar tab over the page (#372's split drop) with the pointer passing over a pane | No header shows while the tab is dragged. |
| 17 | Two windows, each with a split; hover in the inactive one | Note whether the header shows in the inactive window (AppKit may send no moves there); either is acceptable, record it. |
| 18 | Close the window with a header shown, and during a grip drag; ⌘Q | No crash, no CHECK/DCHECK in the log. |
