# Mac steps: the glass URL bar (#105)

For the Mac loop and the hands-on tester. DCHECK build. "Glass URL bar" (`chrome://koan-settings`, URL Bar) is **off** by
default: rows 1 and 2 check that, the rest turn it on. Judge the blur and the fill **on the real screen**. After every row:
`grep -E ':FATAL:|Check failed' <koan log>` is empty and there is no new `.ips`.

| # | Do | Expect |
|---|---|---|
| 1 | Default settings: ⌘T over a busy page | The card is opaque (Zen's `rgb(24,24,24)` / white), as in Zen's release. |
| 2 | The setting is off in the settings page | The checkbox is unchecked. |
| 3 | Turn "Glass URL bar" on, ⌘T over a busy page (dark, then light) | The card is **translucent with the page blurred behind it** (about 92% / 93% fill); no solid block behind the field; the shadow only outside the card. |
| 3b | Same card, look closely at it | The text is **crisp at 100% opacity** (not faded), the page behind is **visibly blurred, not sharp** (stripes or small text behind it become soft); the fill is not rgb 24 solid. |
| 4 | ⌘L on a page; type so the suggestions list opens | The input row is glass; the suggestions list under it stays opaque (it is not part of the card layer). |
| 5 | With 3 windows open, ⌘T / Esc ×20 in one window | The other windows **don't repaint** (Quartz Debug "flash screen updates") and frame time stays under 16 ms: nothing process-wide changes. |
| 6 | With the card open, click another app, then back | **Live**: the card turns opaque while the window is inactive and glass again when it is active, without closing it. Also toggle the setting in another window's settings page, and Reduce transparency, with the card open: it follows at once. |
| 7 | Turn Reduce transparency on, ⌘T | The card is opaque. Off again: glass. |
| 8 | Open the bar from the sidebar pill (typing in it) | The pill stays opaque; only the floating card is glass. |
| 9 | Cost of the 40 px blur on a rounded card | Frame time while the bar fades in and while typing; compare with the setting off. |
| 10 | With glass on: ⌘L on an https page, then on an http page ("Not secure"); then ⌘T and type a few letters | **Only Koan's magnifier** at the card's left, nothing under it: no lock / tune / "Not secure" chip, no search-engine or globe icon, no ink drop. **Take a screenshot of each** (the real screen: a window capture shows the blur flat) and attach them to the PR. |
| 11 | Same with glass off | Only the magnifier too (the location icon is hidden in the card either way). |
| 12 | Native fullscreen (Ctrl+⌘+F), ⌘T | The card opens and is glass: the field is clear, no solid block in it (the location bar refreshes itself, whatever widget the top container is in). |
