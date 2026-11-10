# Mac steps: compact mode polish (#121's last three items)

DCHECK build, a fresh profile, real pointer and keys (`scripts/mac/tools`), `--vmodule=koan_compact_mode_controller=1,koan_outside_window_tracker=1`.
CS = compact + hide sidebar (single toolbar, the default); CT = compact + hide toolbar (multiple toolbars). Timings
from frame captures (60 fps, window only), never by eye. Compare with Zen 1.24b side by side where noted.

| # | Do | Expect |
|---|---|---|
| 1 | CS off → Cmd+S, capture frames | The page takes its full width at once; the sidebar slides off over it in ~100 ms (6 frames), no frame with the page resized twice, no flash of the empty gutter. Zen: card and sidebar move together in ~100 ms. |
| 2 | Cmd+S again (off), capture | The sidebar comes back in ~100 ms (Chromium's expand, shortened), the page narrows once. Then quit with compact on and relaunch: no slide. With an empty tab selected, Cmd+S: the sidebar stays out, no slide away and back. |
| 3 | CS: reveal the sidebar by hover, then move the pointer left out of the window (window not at the screen's edge), stop 150 pt left of it | The sidebar stays out. Move to 250 pt: it hides (within ~50 ms of crossing 200). Log: `pointer tracked outside the window (edge 3, 200pt)` then `ended`. |
| 4 | CS, sidebar hidden: fling the pointer from the middle of the page past the left edge in one fast move | The sidebar comes out and stays while the pointer is within 200 pt. |
| 5 | Step 3, then come back over the page (not on the sidebar) | It hides at once (no 150 ms keep-hover). Back onto the sidebar instead: it stays. |
| 6 | Step 3, then Cmd+Tab to another app, or click the desktop | It hides at once (the window is no longer active). |
| 7 | Step 3 but leave above or below the sidebar's extent (top-left corner), and leave through the bottom edge | Nothing is tracked: the sidebar hides as before. |
| 8 | Tabs on the right: same as 3 on the right edge | Same, mirrored. |
| 9 | CT: reveal the toolbar at the top edge, move up out of the window, stop 60 pt above it; then 120 pt | Stays at 60, hides past 100. Leaving the revealed toolbar through the left edge: it hides at once (no 1 s). |
| 10 | CS in native fullscreen (Ctrl+Cmd+F) | No tracking (log silent); the edge reveal works as before. Page edge to edge, no radius; split view: card margins and radius back. A video's fullscreen, then Esc: no sidebar flash, also with "flash popup" on in Settings. |
| 11 | Crash path: steps 3 and 9, then Cmd+S, Cmd+W, close the window, all while tracked; also Cmd+S twice within 100 ms with the pointer parked at the left edge (#176 T27) | No crash, no `Check failed: current_motion_`, no new `.ips`. |
