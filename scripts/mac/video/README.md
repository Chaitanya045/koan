# Mac video harness: record a flow, review it as contact sheets

For UI glitches that a view-tree check misses (a flash, a row jumping, an empty row, a flicker): a scripted flow drives
our own Koan with the real pointer while ffmpeg records the window. Then the video is cut around the flow's marked
actions into timestamped contact sheets. A model reads only the sheets where pixels changed, plus the flow's counts.

Everything runs on the Mac bench (`<user>@<mac-host>`, key `<ssh-key>`; see `../README.md` for `local.env`). The bench
tools (`ptr`, `hold`, `keypress2`, `activate`, `inputok.sh`, `usergate.sh`, `screen-granted`) are in `$KOAN_MAC_TOOLS`
(default `~/work/koan-mac/tmp`). ffmpeg and node are Homebrew's (`/opt/homebrew/bin`). `hold` (a held-button drag,
one step per call, for `flows/essentials_drag.mjs`) is built from `scripts/mac/tools/hold.m` like `ptr`:

```sh
clang -fobjc-arc -framework Cocoa -o "$KOAN_MAC_TOOLS/hold" scripts/mac/tools/hold.m
```

| file | what |
|---|---|
| `launch.sh [<Koan.app>]` | starts **our own** Koan: a fresh profile in `~/work/koan-mac/rec-<stamp>/profile`, UI DevTools on 9740 and CDP on 9741, the e2e `test_helper` extension, and the window at 60,40, 1280x820 points. Prints and writes `<rec dir>/koan.env`. |
| `quit.sh <pid>` | quits that Koan by its exact pid. It refuses any process without a `--user-data-dir` under `rec-`. |
| `record.sh start <out.mp4> --cdp 9741` / `record.sh stop <out.mp4>` | records the window's region from the screen (avfoundation, 30 fps, H.264 with the hardware encoder). Writes `<out>.rec.json` with the video's time 0 on the wall clock. |
| `flows/clear.mjs` | the Clear button flow (below). Writes its marks and counts to `$MARKS`. |
| `flows/essentials.mjs` | tabs becoming Essentials and going back, then compact mode on, the sidebar revealed, and off (below). Built on `flows/common.mjs` (the gates, the marks, the pages, the rows), which new flows share. |
| `flows/essentials_drag.mjs` | Space tabs dragged into the Essentials with the real pointer (`hold`): straight up over the URL pill, over the Space name, above a grid, and released over the page (below). |
| `flows/pin_drag.mjs` | pinning and unpinning by dragging (`hold`): normal tabs onto the separator and among the pinned tabs, a pinned tab onto + New Tab, Essentials onto the normal tabs, + New Tab and the separator, pinned tabs onto the empty-Essentials promo and onto a tile (its header says each round's check). |
| `frames.sh <video> <out dir> --marks <marks.json>` | builds contact sheets around the action marks, plus `diff.tsv` / `sheets.tsv` / `changed.tsv` (below). `sheet.mjs` is its node half. |

## The gates (do not bypass)

* **Never touch the user's Koan.** Don't touch its app copy, its profile, or `~/work/koan-mac/user-profile`. Our Koan
  has its own profile under `rec-*` and its own ports. `launch.sh` refuses ports that are already in use. The flow
  checks that the CDP port is answered by `$KOAN_PID` and that this process's profile is under `rec-`. Quit only with
  `quit.sh <pid>`.
* `record.sh start` refuses unless `usergate.sh` says ≥ 600 s with no input and `screen-granted` exists. The flow
  checks the same gates at start. Before every pointer event it also checks that our Koan's pid is frontmost
  (`inputok.sh`) and that no input came after our own last event (HIDIdleTime vs. `ptr`'s `own-input.ts`). If either
  check fails, it stops with `STOPPED: <why>` and keeps the marks so far.
* `record.sh` stops by itself after `--max` seconds (900 by default).

## Running it

Copy the scripts over first: the Mac's checkout is the build tree, so leave it alone.

```sh
rsync -a --delete scripts/ <user>@<mac-host>:work/koan-mac/rec-harness/scripts/
```

Then on the Mac (`ssh -i <ssh-key> <user>@<mac-host>`), with the screen granted and the user away:

```sh
cd ~/work/koan-mac/rec-harness
export PATH=/opt/homebrew/bin:$PATH
H=scripts/mac/video
env_out="$($H/launch.sh ~/work/koan-mac/koan/chromium/src/out/Koan/Koan.app)"; echo "$env_out"
set -a; eval "$env_out"; set +a                        # KOAN_PID, UI_PORT, CDP_PORT, REC_DIR, ...
$H/record.sh start "$REC_DIR/clear.mp4" --cdp "$CDP_PORT"
MARKS="$REC_DIR/clear.marks.json" node $H/flows/clear.mjs | tee "$REC_DIR/clear.log"   # or: ... clear.mjs 6,7
# or the Essentials flow (same steps; its marks and log in essentials.*):
# MARKS="$REC_DIR/essentials.marks.json" node $H/flows/essentials.mjs | tee "$REC_DIR/essentials.log"
# or the Essentials drag flow (needs `hold`; a fresh launch.sh Koan, it starts from no Essentials):
# MARKS="$REC_DIR/essentials_drag.marks.json" node $H/flows/essentials_drag.mjs | tee "$REC_DIR/essentials_drag.log"
# or the pin/unpin drag flow (needs `hold`; a fresh launch.sh Koan):
# MARKS="$REC_DIR/pin_drag.marks.json" node $H/flows/pin_drag.mjs | tee "$REC_DIR/pin_drag.log"
$H/record.sh stop "$REC_DIR/clear.mp4"
$H/quit.sh "$KOAN_PID"
$H/frames.sh "$REC_DIR/clear.mp4" "$REC_DIR/sheets" --marks "$REC_DIR/clear.marks.json"
$H/frames.sh "$REC_DIR/clear.mp4" "$REC_DIR/sheets-sidebar" --marks "$REC_DIR/clear.marks.json" --crop sidebar
```

Then copy `$REC_DIR` (without `profile/`) back to read it.

## `flows/clear.mjs`: the sidebar's Clear button, 10 rounds

Each round starts from the same state: only the anchor tab in each Space. Before Clear, the anchor is the selected
tab, a loaded `127.0.0.1` page, which `test_helper` is asked from. The round then opens 6 tabs in the Space and clicks
**Clear with the real pointer**. It moves the pointer over the button first, so the button shows "Clear" instead of
the line, at the screen bounds from the view tree (UI DevTools' box model, else the root view's origin measured with
the pointer). Then it counts the Space's tabs right after the click, 1 s later and 3 s later:

* `model`: from `chrome.tabs`. `space` is the Space's unpinned tabs (pinned-in-Space tabs included). `closable` is
  those that Clear should have closed: not the selected tab, not audible, not pinned in the Space, and not Koan's
  fresh `chrome://` tab. `loading`, `essentials` and `byGroup` (tabs per Space) are also recorded.
* `rows`: the sidebar's shown `KoanTabRow` views by kind (`normal`, `pinned`, `essential`). `rowsAll` counts the
  hidden ones too.

| round | variant |
|---|---|
| 1 | plain: 6 loaded tabs |
| 2 | some-loading: 3 of the 6 still loading (`/slow`: title, body never ends; `/hang`: no response yet) |
| 3 | all-loading |
| 4 | pinned: plus a tab pinned in the Space (Cmd+Shift+D) |
| 5 | essential: plus an Essential (`test_helper` pins it) |
| 6 | compact: compact mode on (Cmd+S), the sidebar revealed at the window's left edge, then Clear |
| 7 | compact-loading |
| 8 | two-spaces: Space "Video B" has 3 tabs; Clear in the first Space must leave them |
| 9 | two-spaces-clear-in-second: Clear in Space B, the first Space keeps its 3 |
| 10 | everything: loading + pinned + Essential + compact + the other Space |

Every round's record in `$MARKS` has `before`, `after` (three snapshots) and these results:

* `leftBehind`: `model.closable` 3 s after Clear. **Anything above 0 is a Clear that left tabs behind.**
* `rowsVsModel`: shown normal + pinned rows minus `model.space`. Non-zero means the sidebar disagrees with the model:
  a stale row or a missing one.
* `otherSpaceTabs`: the other Space's tabs after the Clear (expected: 3, plus that Space's anchor).

The flow ends with one line per round. Run some rounds only with `node clear.mjs 6,7,10`.

The marks are `{t (epoch ms), round, label, action, ...}`. Action marks (`round-start`, `space-switch-*`,
`pin-in-space`, `make-essential`, `compact-on`, `reveal-sidebar`, `hover-clear`, `clear-click`, `clear-key` (Cmd+Shift+K when no Clear button shows, as in compact mode), `compact-off`,
`calibrate`) are where `frames.sh` cuts. The `count-*` marks hold the counts at those moments.

## `flows/essentials.mjs`: a tab becoming an Essential, and compact mode

It re-checks what the first recording (2026-11-11) caught. From the anchor tab and 7 more tabs (one selected):

| round | change (action mark) | what to look for in the sheets |
|---|---|---|
| 1 | the first Essential, the selected tab (`make-essential`; the grid was empty) | no frame with both the tile and the old row at full opacity; the Space header and the list slide down, never jump a row |
| 2 | a second, beside it, selected | the same; the grid's height doesn't change |
| 3-4 | a third and fourth, in the background | the same |
| 5 | a fifth: a new row of tiles | the Space slides down a row |
| 6-7 | two back to the Space (`remove-essential`, test_helper's `unpin-tab`, as an extension or a drag does it) | the tile fades out as the row comes in at the top of the Space's pinned tabs, with no gap and no row crossing the others; the last one out lets the Space slide up |
| 8 | Remove from Essentials, the user's way: Toggle Pin (Cmd+Shift+D) on the selected Essential (`remove-essential`, `action: toggle-pin`) | the row comes in at the top of the Space's tabs as the tile leaves |
| 9 | compact mode on (`compact-on`), the sidebar revealed (`reveal-sidebar`), compact mode off with it out (`compact-off-revealed`), on again with it hidden, and off (`compact-off`) | the revealed sidebar keeps the Essentials a grid of tiles (`tiles`, `tileRows`, `tileMaxW` vs `gridW`) and has Clear above "+ New Tab"; off with the sidebar out, it doesn't move; off with it hidden, the page is laid out once and the sidebar slides in over the gap, never over the page |
| 10 | a real Clear click in the reveal, compact mode off, then Ctrl+Alt+N three times (`new-space-key`) | the new-Space form opens (the lead on #295) |

Each round records `after` (right away) and `after-1s`: `essentials` (the model's), `views` (shown rows of the tab: 1 once the
transition is over), `inGrid`, `gridH`, `headerBelowGrid`, `clearShown`, `compact`; `ok` is the end state. The summary
prints one line per round.

## `flows/essentials_drag.mjs`: dragging tabs into the Essentials

The bug the user hit (2026-11-12): with no Essentials the grid is 0 px tall, and the promo only came over the top half
of the Space name, so a tab dragged straight up over the URL pill lit nothing and dropped nothing. From the anchor tab
and 5 more, each round picks up the last Space row wholly in the visible list (never the anchor; the list may scroll),
moves it past the drag threshold, takes it to the target in 700 ms, holds there and releases. Its marks are
`drag-down`, `drag-over` and `drop` (action marks), `count-over` while held and `count-after` 1.5 s after the release.

| round | drag (target) | expected (`ok`) |
|---|---|---|
| 1 `pill` | no Essentials, straight up over the URL pill's middle | the promo shows and is lit; the tab is the 1st Essential |
| 2 `band` | back to no Essentials (test_helper unpins it), then over the Space name's top half | the promo, lit; one more Essential |
| 3 `above` | with an Essential, straight up over the URL pill (above the grid) | no promo; Zen's gap (the placeholder tile) in the first slot, the tiles moved aside, no line; one more Essential, the first (Koan's choice: the slot above the grid's first row, as above a Space's first row) |
| 4 `page` | released over the page | no new Essential, the tab still in the window, no new window |

Screen points come from `screenRect()`: UI DevTools' box model returns nothing for these views on the Mac, so it falls
back to the root view's origin measured in `setup()` (and to the window's origin plus the view's `ax`/`ay` if that
failed too: the window has no title bar). The summary prints one line per round, then `PASS` or `FAIL` (exit 1). If a
gate stops it with the button held, it releases the button where it was.

## `frames.sh`: contact sheets and the changed-pixels list

The ranges are [mark − 1 s, mark + 3 s] around every action mark, merged where they overlap. `--before` and `--after`
change them; `--from s --to s` picks one range by hand. Each range is decoded at one pixel per point (`--full` for
device pixels) and at every frame (`--fps 30`), so a one-frame flash is kept. Every 16 frames make a 4×4 sheet
(`--tile`), at most 1500 px either way (`--width`). Each frame is stamped `12.345s r03 +0.43`: the video time, then
the round and the seconds since its latest action mark. Homebrew's ffmpeg has no `drawtext`, so `sheet.mjs` draws the
stamp; ffmpeg's `tile` filter builds the sheets. `--crop sidebar` cuts the sidebar first (its rect is in the marks),
so the rows are big enough to read. `--frames` also keeps every frame as a JPEG.

Out, per range, in `<out>/<from>-<label>/`:

* `sheet_NNN.png`
* `diff.tsv`: per frame, `n`, `t` (video s), `wall_ms`, `changed_px` (pixels off by more than `DIFF_THRESHOLD`=24 in
  any channel since the previous frame), `changed_pct`, and `bbox` (x,y,w,h in the decoded frame)
* `sheets.tsv`: per sheet, its time span, `max_changed_px`, `changed_frames` (frames with ≥ `MIN_CHANGED_PX`=1200
  changed), and the action marks inside

`<out>/sheets.tsv` lists all the sheets. **`<out>/changed.tsv` lists only those where something changed: the ones to
look at.**

## Reviewing (for the agent)

1. Read the flow's summary and `rounds` in the marks first. Report every round with `leftBehind > 0`, with
   `model.space`/`closable` before and after and the rows. Also report every non-zero `rowsVsModel`, and any
   `otherSpaceTabs` that is not what the round expects. Those are counts; they need no pictures.
2. Open **only the sheets in `changed.tsv`**, starting with the ones whose `marks` column has `clear-click`,
   `reveal-sidebar` or `compact-*`. Prefer the `--crop sidebar` set; use the full-window set when a change's `bbox` is
   outside the sidebar. A sheet is 16 consecutive frames, 33 ms apart, left to right, top to bottom.
3. Ignore the pointer: a small bbox (about 20×30 px) that moves is the cursor. The capture draws it, along with a
   click ring.
4. Look for:
   * a **flash**: one or two frames brighter, darker or a different colour, then back
   * **rows jumping**: a row's y position changes and then changes back, or rows move twice for one Clear
   * an **empty row**: a gap or a blank or iconless row that stays or lingers after the tabs close
   * **flicker**: a region changing back and forth across several frames
   * a **late update**: rows still listed in the frames after `+0.5` s while `count-after-click` already shows them
     gone
5. Report each glitch with the sheet's path, the stamp's time (`12.345s`, the round, `+s`), the `diff.tsv` line(s)
   (`changed_px`, `bbox`) and a one-line description. Say which round's variant it was.

The stamps are video time. `wall_ms` in `diff.tsv` and `t` in the marks are on the same wall clock. `t0` in
`<video>.rec.json` comes from ffmpeg's progress reports, so it can be up to about 0.1 s late. A mark can therefore
land a few frames before what it caused, never after.
