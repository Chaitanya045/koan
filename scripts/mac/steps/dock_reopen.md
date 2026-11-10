# Mac steps: Dock click after the last window closed (Z6, #161)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome (a fresh `--user-data-dir`, finish
the welcome, ⌘Q, relaunch once: the first process after a fresh install reads the startup setting as Chromium's
default and opens a plain window). Real clicks on the Dock icon. Zen's reference: closing the last window and clicking
the Dock icon brings that window back with its Spaces and tabs.

Prepare: 2 Spaces with 3 tabs each (e.g. example.com, example.org, wikipedia.org in each), the second tab of Space 2
selected, one pinned tab and one Essential.

| # | Do | Expect |
|---|---|---|
| DR1 | ⌘⇧W (or the red button), then click the Dock icon | One window: both Spaces, all 6 tabs, the pinned tab and the Essential, Space 2 active with its second tab selected. No empty extra window. No CHECK/DCHECK in the log. |
| DR2 | DR1, then ⌘⇧W and the Dock icon again, three times in a row, fast | The same window each time; no duplicate windows, Spaces or tabs. |
| DR3 | Close tabs with ⌘W until the window closes (if the window closes on the last tab), then the Dock icon | A window with a new tab page (the window had no tabs left). No crash. |
| DR4 | Settings > On startup > "Open the New Tab page", ⌘⇧W, the Dock icon | A new window with a new tab page, as in Chrome. Set it back to "Continue where you left off". |
| DR5 | ⌘⇧W, then File > New Window (⌘N) from the menu bar | A new, empty window (upstream; only the Dock click restores). Then ⌘⇧W, the Dock icon: the window that was just closed (the empty one) comes back. |
| DR6 | A Private window open and the normal window closed, then the Dock icon | The Private window comes to the front; nothing is restored (a window is visible). |
| DR7 | ⌘⇧W, click the Dock icon twice quickly (the second while the restore runs) | One window. |
| DR8 | ⌘⇧W, the Dock icon, then ⌘Q at once while the tabs load | Quits cleanly; the next launch restores the session once. |
| DR9 | After a crash restore (`first_run_lockdown.md` NT5e) | As NT5e: the window comes back once, no duplicate. |
