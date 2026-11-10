# Mac steps: compact mode follow-up (#144 traffic lights and header row, #121 holds and timings)

For the Mac loop and the hands-on tester. DCHECK build, a fresh profile, real pointer and keys (`scripts/mac/tools`). The
build is #121's head, which contains #144's commit. Layouts as in `bubble_anchors.md`: S = single toolbar (default);
CS = compact + hide sidebar. Timings are read from frame captures or a view-tree poll (`fv.mjs`, 50 ms), never by eye.

| # | Do | Expect |
|---|---|---|
| 1 | #144, CS: reveal the sidebar from the left edge; poll the collapse button's centre y and `KoanSidebarNavButtons`' centre y | The two are equal while the sidebar is out (one row, the buttons at the traffic lights' height), not the stacked collapsed layout. |
| 2 | #144, CS: reveal and hide again; note when the traffic lights appear and go relative to the slide | Known and open: they follow the region's width, so they show for the whole reveal while the strip is still sliding in and for ~150 ms after it has gone. Record the times; the fix (follow the slide's target) is a separate change. |
| 3 | #144, S (no compact) and fullscreen | The lights are always there (S) / AppKit's own (fullscreen); unchanged. |
| 4 | #121, CS: open a blank tab (Cmd+T replaces it with the URL bar: use `about:blank` from the omnibox), Cmd+S | The sidebar is out and stays out, **without sliding out and back in** (frame capture every ~30 ms for 600 ms: no frame with the strip off-screen after the first). |
| 5 | #121, CS: quit with compact on and an NTP open, relaunch | The window opens with the sidebar out, no slide. |
| 6 | #121, CS: hover the left edge while Cmd+S's collapse runs (within ~150 ms); Cmd+Opt+S in the same window | The sidebar comes out at the end of the collapse without leaving and re-entering the edge; Cmd+Opt+S pins it (and the next one unpins). |
| 7 | #121, CS: open the page info at the pill, then Cmd+S while it is open | No crash, no CHECK in the log; the sidebar is out when the collapse has settled, held by the bubble. |
| 8 | #121, CS: right-click the sidebar (menu), choose nothing, close it with the pointer over the window | The hold is let go **at once** (no 150 ms wait); the sidebar stays only if the pointer is on it. With the pointer outside the window: it stays until the pointer moves back over it. |
| 9 | #121 crash path: Space "+" menu or the sidebar menu open and closed, then Cmd+W within 150 ms; also close the last tab with an NTP active (all tabs closing) | No crash, no CHECK, no new `.ips`. |
| 10 | #121, CS: leave the window buttons' strip (CT layout) with the pointer | It hides ~1 s later (Zen's toolbar-hide-after-hover.duration), the toolbar hides at once inside the window. |
