# Mac steps: the left screen edge reveals the sidebar in fullscreen + compact (#168)

Real pointer (`scripts/mac/tools/ptr`), DCHECK build, a fresh profile with one https page (example.com). Run Koan with
`--enable-logging=stderr --vmodule=koan_compact_mode_controller=1` to get the "Koan: pointer monitor ...",
"Koan: fullscreen left edge ..." and "Koan: sidebar hot zone ..." lines (PR #168's diagnostics).

Reveal check, used by every case below: pointer ramps to x=3, then x=0 (the screen's left edge), 2.2 s each, then a
wiggle (x=0..3, y +-20). The sidebar region (`VerticalTabStripRegionView`) is `x=0 w=240` when it is out.

| # | Do | Expect |
|---|---|---|
| 1 | Ctrl+S (compact on), Ctrl+Cmd+F (fullscreen), reveal check | Revealed. (Control: this works.) |
| 2 | Ctrl+Cmd+F, Ctrl+S, reveal check | Revealed. |
| 3 | Fullscreen, compact on; peek the sidebar, press the pill's Site information button (the first `ImageButton` of `KoanUrlPill`), Esc; reveal check | Revealed. |
| 4 | **The failing sequence:** Ctrl+Alt+B (multiple toolbars); click the toolbar's site icon (`LocationIconView`: page info); Esc; Ctrl+Alt+B (back); Ctrl+Cmd+F; Ctrl+S; reveal check | Revealed. Failed on develop def7: no sidebar, even with a wiggle. |
| 4a | Same without the page-info click and Esc | Revealed? (is the layout toggle alone enough?) |
| 4b | Same without Ctrl+S before the check, but with Ctrl+Alt+S (the keyboard peek) | If the peek also fails, the sidebar's expand-on-hover is blocked by a lock; if it works only the edge check is broken. |
| 4c | In the failing state, leave fullscreen (Ctrl+Cmd+F) and hover the rail's edge in the window | Does a plain compact reveal (hot zone gutter) work in a window? |
| 4d | In the failing state, Ctrl+S (compact off) then collapse the sidebar with its toggle and hover the rail (plain expand on hover) | If this fails too, a lock on the vertical tab strip region is stuck (upstream: `force_collapse` from the omnibox popup, or `keep_current_state`). |

Report, for 4: the log lines around each action (was the pointer monitor on after Ctrl+S? did "left edge hovered" appear
at x=0-3? was "sidebar hot zone hovered" logged, and did it hold?), the strip width over time, and the answers to 4a-4d.

## More cases (review of #171)

Logs: run with `--vmodule=koan_compact_mode_controller=1`: "Koan: pointer monitor on (application wide | this window)" /
"off" appear only on a change.

The "invisible window at the screen's left edge" in the failing state (500x500 or 64x64 at x=0, y=456 in CGWindowList) is a
`TUINSWindow` (delegate `TUINSWindow`, not visible): macOS's own text-input helper window, there in every fullscreen
change, also in the passing runs. It is not ours and not the cause; the window monitor missed the pointer there for
another reason (the events went to a different window than the browser's), which the application-wide monitor avoids.

| # | Do | Expect |
|---|---|---|
| 4e | In the failing state (4), reveal the sidebar (x=0) and click a tab at y about 470 | That tab activates (the stray window does not take the click). |
| 4f | In the failing state, Ctrl+Alt+S | The sidebar peeks. |
| 5 | Two displays, the external one on the left. A fullscreen + compact on the built-in display, B windowed on the external one. Move the pointer all over B, along B's top and left edges | A's sidebar and toolbar never reveal. |
| 6 | Two fullscreen windows (two Spaces). Hover x=0 in A, three-finger swipe to B, hover x=0 | Only the visible one reacts; the other is still collapsed when you arrive. |
| 7 | A fullscreen + compact; open a second window B (windowed, over A's display), move the pointer along A's screen edge where B is not | A reveals; over B nothing happens to A. |
| 8 | Fullscreen + compact, right-click the sidebar to open its menu (or a tab context menu), move to x=0 | The sidebar does not slide under the menu. Same while dragging a tab: nothing slides. |
| 9 | Fullscreen + compact with Glance open (Alt-click a link), pointer to x=0 | (Compare with Zen 1.24b live: Glance is in-window there.) Note what happens. |
| 10 | Two windows, A fullscreen + compact; close A with Cmd+W while moving the pointer quickly in B | No crash, no DCHECK. |
| 11 | Transitions, each with the "pointer monitor" log line checked (on/off and the type must match the state): enter fullscreen with the green button, Ctrl+Cmd+F and View > Enter Full Screen; leave it with Esc, the green button and the menu; a page's video fullscreen inside window fullscreen and Esc; compact on and off while in fullscreen; Ctrl+Alt+B while in fullscreen; move a fullscreen window's Space to the other display | After each, the monitor is application wide exactly while the window is fullscreen with compact edges (and "this window" or off otherwise), and the edge reveal works / does not work accordingly. |
| 12 | Fullscreen + compact, scroll a long page with the trackpad with Instruments' Time Profiler on | `OnEvent` plus `EventFromNative` under 1% of the main thread. |
