# Mac bench drivers (real keys, real pointer, DCHECK builds)

The e2e suites in `scripts/e2e` need Linux (headless Ozone, `run.sh`). On the Mac bench
(a MacBook Air reached over ssh) the same checks run through these drivers. They drive a
**test** Koan (fresh profile under `~/work/koan-mac/tmp/pass-R-profile`) with posted key events
and read the view tree through UI DevTools (`--enable-ui-devtools=9340`) and the page
side through CDP (`--remote-debugging-port=9341`), both forwarded by an ssh ControlMaster
(`/tmp/koan-cm.sock`, see `vtabs.sh`).

## `vtabs.sh <label> <app>`: the vertical-tabs lockdown checks (#154, #155)

| env | meaning |
|---|---|
| `MODE=main` | T13 the compact key x6 (Cmd+S with KEYSET=cmd, Ctrl+S otherwise), T13b Shift+Cmd+L opens the inspector and leaves the sidebar alone (#90 owns that key), T14 Shift+Cmd+A, T21 rapid compact toggles (2 / 3 / 20 within 250 ms .. 10 s, then close mid-animation), T22 fullscreen in/out with compact off and on, T24 log greps |
| `MODE=old` | T1/T2/T6: start from a seeded old-Chrome profile (`PREFS=<json>`, e.g. `scripts/e2e/prefs/vertical_tabs_old_profile.json`), check the start state, then quit gracefully and read the three prefs back |
| `MODE=nocompact` | T20: `FLAGS='--disable-features=MacAppCodeSignClone,KoanWelcome,KoanCompactMode'` (a later `--disable-features` replaces the launcher's own list, so pass all of it); Ctrl+S and Shift+Cmd+L must do nothing |

On a base without #90 the compact key is Ctrl+S (`COMPACT` = flags `k`, key code 1); with #90 it is Cmd+S.
The shared invariant is `scripts/e2e/lib/vertical_tabs.mjs` `checkKoanSidebar()`: strip 163..500 wide
(expanded) or 0 (compact), never 1..119 (Chromium's rail), no horizontal `TabStripRegionView`, no
Chromium `ToolbarView` drawn. Every run also fails on a "Koan: the vertical tab strip" log line, a
`:FATAL:` line or a `Spaces:` line.

Example: `MODE=old PREFS="$(cat scripts/e2e/prefs/vertical_tabs_old_profile.json)" scripts/mac/vtabs.sh build1 '/Users/me/work/koan-mac/apps/KoanCheck-x.app'`.

## Rules the drivers follow (do not remove)

* A run starts only after the hands-on tester granted the screen (`tmp/screen-granted`,
  `tools/grantok.sh`) and after 600 s without user input (`tools/usergate.sh`, `tools/usertrack.sh`).
* Every posted key checks that the **frontmost pid is the test Koan's pid** (`tools/inputok.sh`);
  otherwise the run ends at once. Never match the app by name: any Koan build has that name.
* The run cleans up only its own `pass-` profile Koans (`tools/cleanup.sh`, `tools/killkoan.sh <pid>`),
  and dismisses only crash dialogs that name Koan (`tools/crashdlg.m`). It never touches the user's
  own Koan or Zen.

## Tools (`tools/`, copy to `~/work/koan-mac/tmp/` on the Mac and compile the `.m` files with `clang -framework ...`)

`keypress2 <pid> <keycode> '<flags>'` (flags: `c` cmd, `s` shift, `k` ctrl, `a` alt; CGEventPostToPid),
`ptr pos|move x y ms|click|rclick|altclick`, `crashdlg list|dismiss`, `idle.sh`, `front.sh`, `frontpid.sh`.
