# Mac steps: dragging the window from the page (#162, Z13, Z45)

For the Mac loop and the hands-on tester. `scripts/e2e/window_drag.mjs` checks what the page sees from synthetic
CDP input (it never holds the physical button, so on the Mac AppKit's drag never starts there and the gesture just
ends with the synthetic release: `down move up`, as on Linux). The window really moving needs a real pointer
(`scripts/mac/tools/ptr`, or by hand). Open `scripts/e2e/pages/window_drag.html` (it logs `down` / `move` / `up` in
`seen`; read it from the page's DevTools console or CDP `Runtime.evaluate`).

DCHECK build, a normal window with the sidebar, Dock and menu bar as usual.

| # | Do | Expect |
|---|---|---|
| 1 | Press on empty space in the top 5% of the page, move 100 px, release | The window follows the pointer and stays where released. `seen` ends `down move up`. |
| 2 | Repeat 20 times, fast; and two fast drags back to back | No crash, no stuck window, no CHECK in the log. |
| 3 | After a drag, move the pointer (no click) over the sidebar and the URL pill for 2 s; `seen.length = 0` first | The page sees **no** `move` (the page's view got a release from `EndNativeWindowMove()`: no stale hover or cursor over Koan's views). |
| 4 | Start a drag and press Cmd+L / Cmd+T / open a context menu at the same moment | No crash (nothing is captured: `Widget::RunMoveLoop` is gone). |
| 5 | macOS 15: drag the top band to the left screen edge and release | The tiling preview shows, the window tiles. Top edge: zoom. Drag onto another display / Space. |
| 6 | Tile the window left, press in the top band and move 10 px | No drag (Z45: 50 px when tiled). Move 60 px: it un-tiles and follows. Same with a zoomed (green button, Option) window. |
| 7 | Press without moving | Nothing moves; the page gets its click. |
| 8 | Close the window or Cmd+Q during a drag | No crash. |
| 9 | A PWA window (app shim) | The drag does nothing (documented: the NSWindow in the browser process is an invisible placeholder), no crash. |
| 10 | Fullscreen | No drag. |
