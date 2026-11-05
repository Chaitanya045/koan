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
