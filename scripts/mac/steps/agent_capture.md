# Mac steps: one capture path for every tab (Koan.captureScaled, 2026-11-20)

For the hands-on tester. `scripts/mac/suite.sh --only agent_cli` covers each size on an agent tab and on a taken-over
user tab, `--clip` on both, the page seeing no resize or devicePixelRatio change, and (with real input) a covered
window. `--only agent_capture_bench` measures wall time, bytes and tokens against #409's CDP clip and scale, and
`koan_browsertests --gtest_filter='KoanAgentCaptureBrowserTest.*'` checks the copy itself. These steps cover what a
script can't judge: whether the user sees anything at all while their tab is captured, other displays and Spaces.
Use a fresh test profile with Agent ready on and `koan` from `Koan.app/Contents/Resources/bin/`, paired with that
profile (`KOAN_USER_DATA_DIR=<profile> koan pair`).

**The page.** Serve `scripts/e2e/pages/agent_capture_watch.html` (`python3 -m http.server 8766` in
`scripts/e2e/pages`) and open `http://127.0.0.1:8766/agent_capture_watch.html` in a tab of your own. Its last line
says `resize=<n> dpr=<n>`: both must stay 0 throughout.

| # | Do | Expect |
|---|---|---|
| 1 | Bring the watch page to the front, `koan take-over`, allow it. Then, while you watch the tab: `for s in micro mini normal full; do koan screenshot <tab> --size $s -o /tmp/c-$s.png; done` | No flicker, no jump, no change of sharpness, nothing moves. `resize=0 dpr=0`. Each line says the same pixels as the same command on an agent tab of the same window (`koan open` the same URL): e.g. 320×200, 640×400, 1440×900 and about 2450×1531 for a 1440×900 window on a Retina display. |
| 2 | Same tab: `koan screenshot <tab> --clip 'loc:role=button[name="Press me"]' -o /tmp/c-clip.png`, then open the file | The button with a small margin, sharp (device pixels), no flicker in the tab. |
| 3 | Pinch-zoom the watch page in (trackpad), then `koan screenshot <tab> --size normal -o /tmp/c-pinch.png` | The image shows what the zoomed tab shows, at CSS pixels; `resize=0 dpr=0`. Pinch back out. |
| 4 | ⌘+ twice (110%, 125%), then step 2 again | The button, at its zoomed size; nothing in the tab moves. ⌘0 after. |
| 5 | Drag Koan's window to an external 1× display, then step 1 | Normal is the window's CSS pixels; full is 1× there (normal and full the same size). No flicker. |
| 6 | `koan open http://127.0.0.1:8766/agent_capture_watch.html` (an agent tab, in the background), switch to another Space with another app full screen, wait 5 s, `koan screenshot <tab> --size mini -o /tmp/c-hidden.png` | The page (blue box, heading), not blank and not stale; the answer comes in well under a second. |
| 7 | `koan release <tab>` the user tab, then step 1 on it | `Koan.unknownTarget` (it isn't the agent's any more); nothing captured. |
| 8 | Console.app, filter `Koan`, during steps 1 to 6 | No GPU or viz errors, no "CopyFromSurface" failures. |
