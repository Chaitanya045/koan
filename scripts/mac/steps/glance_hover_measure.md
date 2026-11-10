# Mac steps: Glance buttons and compact hover, measured against Zen 1.24b (#104)

DCHECK build, a fresh profile, real pointer (`scripts/mac/tools`). Measure from window-only frame captures at 60 fps
(`screencapture -v` or the bench's capture tool), and from UI DevTools bounds, never by eye. Same window size as the
zen-ref capture (1470 x 956 pt content) and Zen 1.24b beside it with the same page.

| # | Do | Expect (Zen 1.24b, `.agent/notes/zen-mac-spec.md` §7 and §5) |
|---|---|---|
| 1 | ⌥-click a link of `scripts/mac/glance/serve.sh`'s parent page; read the three buttons' bounds (UI DevTools, `ImageButton` under `KoanGlanceView`) | 32 x 32 each, x = card right + 12, y = card top + 27, + 44, + 88 (Zen: x 1354, y 35 / 79 / 123 in that window). Icon 16 pt. |
| 2 | Hover each button, then press and hold | Hover: the disc turns rgb(204, 204, 204) in dark mode, rgb(41, 41, 41) in light, and grows 2% (no ink-drop circle). Pressed: 98%. Leave: back to the normal disc. Compare with Zen's capture at the same zoom. |
| 3 | Type in a field of the Glance page, Esc once (close turns red), hover it | Stays red with the white icon (Zen's `[waitconfirmation]`). |
| 4 | Shadcn look (Settings > Look and Feel): hover the buttons | `secondary` at 80% over the background; the icon stays readable. |
| 5 | Compact on (hide sidebar), real pointer to the left window edge; capture | Slide starts within one frame of the pointer reaching the edge, done in 250 ms; panel 240 pt wide (Zen: the docked width, 246 incl. its float margin; known difference). |
| 6 | Move off to the right | The sidebar stays 150 ms, then slides out in 150 ms: hidden < 0.5 s after leaving, as in Zen. |
| 7 | Page behind the Glance | Dimmed (the window background at 70% over it). Not scaled to 0.97 (known, see ZEN_PARITY). |
