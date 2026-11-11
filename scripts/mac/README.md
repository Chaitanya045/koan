# Mac bench drivers (real keys, real pointer, DCHECK builds)

## `suite.sh <Koan.app>`: the scripted Mac cycle (start here)

For the non-AI Mac queue: no model watches screenshots. It builds nothing, runs on the Mac itself, and prints one line
per test (`PASS name` or `FAIL name: <first failing assertion or crash signature>`), then a `TOTAL` line. The same lines
go to `<logs>/summary.txt`, and an agent reads only that file. Every test's full log is next to it.

1. **C++:** every `koan_*unittests` and `koan_browsertests` in the app's out dir (`--gtest_brief=1`; 15 min each, 60 for
   the browser tests).
2. **e2e:** `suite/e2e.list`, each against a fresh `--user-data-dir` with `--use-mock-keychain`. The new Mac checks are
   `window_teardown` (#193), `window_sync` (#75), `space_drag` (#224), `welcome_input` (#174) and `agent_cli` (#180,
   through the `koan` CLI). Then the existing scripts that send no keys.
3. **Visual:** `visual/capture.mjs` takes `screencapture -l<window>` shots in fixed states, and `visual/diff.py` compares
   the named regions (`visual/regions.json`) with `docs/screenshots/reference/`. `--record` writes those references,
   which is the first run's job.
4. **Crashes:** new `.ips` reports from this app or out dir since the run started, and a grep of every Koan log for
   `FATAL`, `Check failed` and `DCHECK`.

`--only a,b`, `--skip-cxx|e2e|visual|crash`, `--all-e2e` (every `scripts/e2e/*.mjs`, for porting more of them) and
`--input real|synthetic` are explained in the script's header. **Real input** (the bench's `keypress2` and `ptr`, for the
AppKit checks in `welcome_input` and `space_drag`) is used only when `$KOAN_MAC_TOOLS` (default `~/work/koan-mac/tmp`)
has the tools, the screen is granted (`grantok.sh`) and nobody has used the Mac for 600 s (`usergate.sh`). Each event
also checks that Koan's exact pid is the frontmost app (`inputok.sh`). Otherwise those checks print `SKIP` and the
summary line says so. The suite stops only its own Koans, by exact PID (fresh profiles under `/tmp/ksuite.*`, the
executable inside the given app), and never attaches to a debugging port another process already listens on.

After the suite passes, the cycle's one computer-use walk-through is `full-flow.md`.

**Agent control and pairing:** there is no auto-approve switch, and none is added. `agent_cli` runs `koan pair` and
then presses Allow in Koan's own approval dialog through UI DevTools, the same click a person makes. The test profile
turns Agent ready on with `scripts/e2e/local_state/agent_cli.json`. The CLI gets a throw-away `HOME`, so the user's
`~/.koan` is never read or written. If UI DevTools can't reach the dialog on a build, that run needs the one manual
approval: run `suite.sh --only agent_cli` with someone at the Mac and click Allow within 20 seconds.

The e2e suites in `scripts/e2e` need Linux (headless Ozone, `run.sh`). On the Mac bench
(a MacBook Air reached over ssh) the same checks run through these drivers. They drive a
**test** Koan (fresh profile under `~/work/koan-mac/tmp/pass-R-profile`) with posted key events
and read the view tree through UI DevTools (`--enable-ui-devtools=9340`) and the page
side through CDP (`--remote-debugging-port=9341`), both forwarded by an ssh ControlMaster
(`/tmp/koan-cm.sock`, see `vtabs.sh`).

## Where the Mac is (not in the repo)

`vtabs.sh` and `drv/vtabs.mjs` read `KOAN_MAC_HOST`, `KOAN_MAC_USER` and
`KOAN_MAC_KEY` (the ssh private key's path) from the environment or from the
git-ignored `scripts/mac/local.env` (copy `local.env.example`; the environment
wins). If one is missing they stop with a message. Nothing about the Mac (host
name, address, user name, key file) may be committed: CI's "Private details"
check (`scripts/ci/check_private_info.py`) fails on a `.local` host name, a
private-range IP address or an ssh key path in any tracked text file; notes use
`<mac-host>`, `<mac-ip>`, `<user>` and `<ssh-key>`.

## `vtabs.sh <label> <app>`: the vertical-tabs lockdown checks (#154, #155)

| env | meaning |
|---|---|
| `MODE=main` | T13 the compact key x6 (Cmd+S with KEYSET=cmd, Ctrl+S otherwise), T13b Shift+Cmd+L opens the inspector and leaves the sidebar alone (#90 owns that key), T14 Shift+Cmd+A, T21 rapid compact toggles (2 / 3 / 20 within 250 ms .. 10 s, then close mid-animation), T22 fullscreen in/out with compact off and on, T24 log greps |
| `MODE=old` | T1/T2/T6: start from a seeded old-Chrome profile (`PREFS=<json>`, e.g. `scripts/e2e/prefs/vertical_tabs_old_profile.json`), check the start state, then quit gracefully and read the three prefs back |
| `MODE=pinrev` | T27: compact on, the pin key during and after the collapse, again 100-250 ms later (reversal), compact off; 30 rounds with the pointer away and 30 with it at the left edge (`ptr move`, wiggled 1 px during the collapse), checking the strip (out only at the edge, hidden after, expanded with compact off). T28: the Create-a-Space form open from the pinned sidebar while compact goes off and on (a lock held from before the collapse). T29: native fullscreen, the pointer at the left edge as compact goes on (#171). Fails if Koan dies, a lock leaks, or `Check failed: current_motion_` is in the log (#176) |
| `MODE=nocompact` | T20: `FLAGS='--disable-features=MacAppCodeSignClone,KoanWelcome,KoanCompactMode'` (a later `--disable-features` replaces the launcher's own list, so pass all of it); Ctrl+S and Shift+Cmd+L must do nothing |

On a base without #90 the compact key is Ctrl+S (`COMPACT` = flags `k`, key code 1); with #90 it is Cmd+S.
The shared invariant is `scripts/e2e/lib/vertical_tabs.mjs` `checkKoanSidebar()`: strip 163..500 wide
(expanded) or 0 (compact), never 1..119 (Chromium's rail), no horizontal `TabStripRegionView`, no
Chromium `ToolbarView` drawn. Every run also fails on a "Koan: the vertical tab strip" log line, a
`:FATAL:` line or a `Spaces:` line.

Example: `MODE=old PREFS="$(cat scripts/e2e/prefs/vertical_tabs_old_profile.json)" scripts/mac/vtabs.sh build1 '/Users/<user>/work/koan-mac/apps/KoanCheck-x.app'`.

## `remote_debugging.sh <app>`: the default-profile remote-debugging block

Behaviour test of `patches/chrome/browser/devtools/remote_debugging_server.cc.patch` (Chrome's refusal of
`--remote-debugging-port` / `--remote-debugging-pipe` on the default user data directory, on in Koan too). It runs
on the Mac itself and starts Koan with `--no-startup-window`, so nothing is drawn.

* Leg A: a fresh `--user-data-dir` still gets its server (`DevToolsActivePort` and `/json/version`).
* Leg B: the **default directory** under a **throw-away `HOME`**, so the real default directory is never touched. It
  first probes that CoreFoundation honours the `HOME` override (and SKIPs rather than run on the real profile if not),
  then checks after each run that the throw-away default dir was the one used and that the real profile's `Local State` is
  unchanged. `--remote-debugging-port=9222` and `--remote-debugging-pipe` (started with fds 3 and 4 open, which the
  browser requires) must both be refused with the stderr message, and nothing may answer on 9222. Runs by default; the Mac
  loop runs it once per build.
* Leg C (INFO only): the default dir spelled in lower case, to report whether the case-insensitive-APFS gap
  (see `SECURITY.md`) is present.

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
`ptr pos|move x y ms|click|rclick|altclick|cmdclick x y|down x y|dragto x y ms|up x y` (`down`/`dragto`/`up` make a
held-button drag, used by `scripts/e2e/space_drag.mjs`), `winid <pid>` (that pid's on-screen CGWindowIDs and bounds,
for `screencapture -l`; `suite.sh` builds it into its run dir if it isn't there), `crashdlg list|dismiss`, `idle.sh`,
`front.sh`, `frontpid.sh`.
