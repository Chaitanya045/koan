# Mac steps: the traffic lights at the top edge (#178)

Real pointer (`scripts/mac/tools/ptr`), a normal profile with one https page (example.com), the sidebar on the left.
Measure with `screencapture -l <window id>` (fullscreen: a screenshot of the display, menu bar included) and the views
dump (`MultiContentsView` y, the content card's y).

Pointer move used below: ramp to y=3, then y=0 at the same x (x=400), 2.2 s each, hold 1.5 s, then capture.

| # | Do | Expect |
|---|---|---|
| 1 | Click the **green button** (native full screen), compact mode off; pointer move at x=400 | The menu bar comes down **and** AppKit's title strip (about 32 pt tall) slides in below it with the three lights (red at x about 16, green about 62). No Koan toolbar. The page card does not move. |
| 2 | Same with compact mode on (Ctrl+S before or after entering) | Same as 1. |
| 3 | Pointer away from the top edge | The menu bar and the strip slide back up; Koan's layout is what it was. |
| 4 | Press a light in the strip (yellow is not available in fullscreen; click green) | Leaves full screen; the window comes back with its lights per the rules below (sidebar shown: lights in the top row; compact with the sidebar hidden: no lights). |
| 5 | Ctrl+Cmd+F (Chromium's fullscreen) in place of the green button, pointer move | Same as 1 and 2 (the strip comes with the menu bar). |
| 6 | Windowed, compact mode on, sidebar hidden (collapsed). Pointer move at x=400 | **Nothing appears** at the top: no strip, no lights. The page card stays at **y=8** (dump before and after: same). |
| 7 | Windowed, compact mode on; pointer to the window's left edge (the sidebar peeks); lights show in its top row; pointer to the top edge of the window above the sidebar | The lights stay with the sidebar; no extra strip; the page card y stays 8. |
| 8 | Windowed, compact mode on, tabs on the **right**; pointer move at x=400 | As before this change: the strip with the lights opens (44 pt) and the page card moves to y=44 (known difference from Zen, `docs/ZEN_PARITY.md`, window buttons row). |
| 9 | Windowed, compact mode off, sidebar shown | Lights in the sidebar's top row always; pointer at the top edge changes nothing. |
| 10 | Enter fullscreen from 6 (lights hidden in the window), then leave it | In fullscreen the strip has the lights (1/2). After leaving, the lights are hidden again (compact, sidebar hidden). |

Report: for 1, 2 and 5 a capture at the top edge (the strip's height, colour and the lights' centres); for 6 the card's y
before and after the pointer move.
