# Mac steps: the new tab page and the empty tab (#95, #96)

For the Mac loop and the hands-on tester. DCHECK build, Spaces on. `scripts/e2e/newtab.mjs`, `newtab_custom.mjs`
and `empty_tab.mjs` are the Linux drivers for several rows; they run nowhere now.

| # | Do | Expect |
|---|---|---|
| 1 | A new window (⌘N) | The page is blank in the look's colour (Zen look: opaque theme colour; set Settings > Look to shadcn dark: #18181b, a card lighter than the #09090b sidebar and gutter), no flash of another colour. The New Tab bar is open (Zen's open-on-startup). |
| 2 | Relaunch with the previous session restored | The restored tabs, no New Tab bar. |
| 3 | Open a new tab page A and B (two windows, or two Spaces), close B, reload A | A keeps its colour (never white or empty). With B still open, A has its own window's colour. |
| 4 | With a new tab page open: switch Space; edit the theme; switch the look between shadcn dark / light / Zen | The page loads again in the new colour (Space switch: report if it stays stale). |
| 5 | Drag a window's only tab into another window | The source window closes (no hidden window left, none in the Window menu). |
| 6 | Window with one tab: tab menu > Move to another window | The source window closes. Same for the last of several tabs. |
| 7 | Close a window's only tab (⌘W) | The window stays with the empty tab, one Space, no tab row, "+ New Tab" shown. |
| 8 | From the empty tab, open a bookmark | It opens in place (an ordinary tab now, with its row). Back into the new tab page: the row stays, ⌘W closes just that tab. |
| 9 | Settings > Look and Feel > New tab page: Chromium's, with a non-Google default search engine | That engine's own remote new tab page (document, not a bug). |
| 10 | With the `NewTabPageLocation` policy set (defaults write com.koan... NewTabPageLocation), close the window's only tab | One tab afterwards (the policy page), not two. |
| 11 | Compact mode off, empty tab selected: turn compact on | The sidebar stays shown. Open a page in that tab by dropping a link: the sidebar can hide again. |
| 12 | Ctrl+Tab / ⌘1-9 over a Space whose only other tab is the empty one | Known gap: the hidden empty tab is reached (ZEN_PARITY: Missing). |
| 13 | Drag a tab that shows a new tab page (typed `chrome://newtab`, or the horizontal strip's +) out into a new window; also tab menu > Move to new window | The New Tab bar does **not** open in the new window (it is the window's adopted tab, not its own). A ⌘N window still opens it (row 1). |
| 14 | With a new tab page open and the New Tab bar open on it: edit the theme colour or switch the look | The page loads again in the new colour; the bar stays open and keeps its focus. |
| 15 | New window in a Space with a colour (Space tint) | The page's colour is the tinted window's, after at most one reload; no stale untinted page. |
| 16 | Drag the theme colour picker continuously with several new tab pages open | No reload storm (each page reloads at most about four times a second, not on every step); a bookmark clicked in an NTP tab at the same time still opens. |
| 17 | A Space with two pages, the second selected: Clear twice (hover the sidebar, ↓ Clear; or ⇧⌘K) | First the other page goes; then the selected one, and the Space shows no rows at all (no "New tab" row), the page is the empty tab, Clear is gone. ⌘W then closes the window. |
| 18 | A profile from before this fix with a listed "New tab" row as a Space's only tab: click its × | The row goes and stays gone (the tab becomes the empty tab); the sidebar doesn't jump or flash. |
