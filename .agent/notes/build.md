# Build notes

## Machine
- 8 cores, 15 GB RAM, zram 15.5 GB + 16 GB swapfile, ~780 GB free disk.
- Other apps typically hold 8–10 GB RAM, so only ~5–7 GB is free for the
  build. Use `JOBS=4` (`scripts/build.sh` defaults to 5). Raise only if
  `free -g` shows more headroom.
- First full build took ~15 h at 4 jobs (expected 10–15 h). Incremental: minutes (component build).

## Gotchas
- **Full-rebuild trap:** changing anything that alters every compile command —
  `build/config/BUILDCONFIG.gn` `default_compiler_configs`, global
  `include_dirs`, or most `args.gn` values (`is_debug`, `symbol_level`,
  `dcheck_always_on`, ...) — invalidates every object and costs another
  10–15 h. Decide such things before a long build, never casually.
- `enable_nacl` no longer exists in this Chromium; GN warns if set. Removed.
- Builds use **siso**, not ninja; autoninja passes `-j` through.
- `pgrep -f scripts/build.sh` run from the agent shell matches its own command
  line, so it always reports "alive". Check `siso ps` or `ps ... | grep -v grep`.
- Headless Chrome won't open `chrome://` URLs passed on the command line (it
  shows the NTP). Use `--remote-debugging-port` + `scripts/check_flags.mjs`
  (Node 24 has a global WebSocket).
- **UI screenshots without touching the desktop:** `scripts/shot_ui.sh out.png
  [flags] -- urls...` runs Koan on the headless Ozone platform with
  `--ozone-dump-file`, which writes each window frame (browser UI included)
  as PNG. Needs `--in-process-gpu --disable-gpu` so the unsandboxed browser
  process writes the file. No Xvfb/xdotool on this host; session is GNOME
  Wayland.
- `scripts/cdp_eval.mjs <port> <url> <expr>` evaluates JS on any page incl.
  `chrome://prefs-internals` (pref values with default/user metadata).
- **Native view tree with bounds:** start Koan with
  `--enable-ui-devtools=<port>` (plus the headless Ozone flags) and run
  `node scripts/ui_tree.mjs <port> [regex]`. The UI DevTools server is
  WebSocket-only (`ws://127.0.0.1:<port>/0`; HTTP `/json` hits NOTREACHED and
  just logs). Bounds come from `CSS.getMatchedStylesForNode` (`X/Y/Width/
  Height/Visible`). Never query the root node: `RootElement::GetBounds()` is
  NOTREACHED and aborts the browser.
- Don't start a background process and `sleep` in the same foreground Bash
  call; it hung until timeout. Start long-lived test browsers with
  `run_in_background: true`, then probe in separate calls.
- **/tmp is a 7.8 GB tmpfs.** Fresh test profiles grew to ~700 MB each
  (component/model downloads) and filled it; the shell then silently
  truncates heredocs (it buffers them in /tmp) and tool output is lost.
  Test scripts now source `scripts/koan_test_env.sh`: profiles under
  `.test-profiles/` (git-ignored), `--disable-component-update`, cleanup
  traps. siso also leaves ~1 GB of logs in /tmp per full build; delete old
  `siso.*.log.*` files after big builds.
- **Interactive UI tests off-screen:** `scripts/ui_session.sh <port> <secs>
  [flags] -- urls` (run in background) + `node scripts/ui_tree.mjs <port>`
  + `node scripts/ui_input.mjs <port> <class-regex> <nth> mouse|key ...`.
  Synthetic events need the `ui-debug-tools-enable-synthetic-events`
  feature (ui_session.sh enables it). `IsMouseHovered()` uses the real
  cursor, which headless never moves, so hover-out paths need a desktop.
- **Field trial testing config:** developer builds apply
  `fieldtrial_testing_config.json` (enables e.g. `InitialWebUI`, i.e. a
  WebUI back/forward/reload toolbar). Koan sets
  `disable_fieldtrial_testing_config = true`; flipping it rebuilt ~1.9k
  steps.
- **Shared out dir:** agent-build.sh points `//koan` at each worktree's
  `src/` in turn and now recompiles every file that differs from the last
  build (siso alone goes by mtime, so it used to link another worktree's
  objects). Never date files in the future: siso aborts on a future mtime,
  which breaks the shared build for every agent.
- Never leave a test browser running across a build: regenerated .pak files
  make the running one abort ("Unable to find resource").
- Changing `BRANDING`/string .grd files rebuilds ~3.6k steps (~45 min).
- **Browser keyboard shortcuts off-screen:** UI DevTools key events go
  straight to a view and skip accelerators. Instead send keys to the page
  with `scripts/cdp_key.mjs <cdp-port> <keycode> <modifiers>`; unhandled
  keys bubble to BrowserView::HandleKeyboardEvent and the FocusManager,
  like real input.
- **Real desktop check without a screenshot portal:** run Koan with
  `--ozone-platform=x11` (Xwayland, DISPLAY=:0), find the window with
  `xprop -root _NET_CLIENT_LIST` + `xprop -id <w> WM_CLASS` (class "Koan"),
  capture with `import -window <w>`, drive it with `cdp_key.mjs`. Shows a
  real window briefly; use a throwaway profile.
- `pkill -f <pattern>` / `pgrep -f` from the agent shell match the shell
  itself (its command line contains the pattern): pkill kills the whole
  command (exit 144), pgrep always "finds" something. Kill by PID instead.
- Long runs must be detached (`setsid nohup ... & disown`): background Bash
  tasks are capped at 2 h, and a T3 Code restart kills child processes.
- Foreground `sleep` is blocked in the agent shell; wait with a background
  `while pgrep ...; do sleep 60; done` loop instead.
- Ubuntu's `install-build-deps.sh` doesn't apply (host is CachyOS/Arch); the
  Debian sysroot (`use_sysroot = true`) covers the target libs.

## Desktop screenshots (X11 via Xwayland)
- `import -window <id>` can return a stale frame for a window that isn't
  focused (GNOME/Xwayland): content updates (late images, a just-opened
  Glance) may be missing. Off-screen dumps (headless Ozone) are the real
  compositor output. Any CDP call on the page (e.g. Runtime.evaluate) made a
  fresh frame show up in captures.
- Trim the window's black border with
  `magick in.png -bordercolor black -border 1 -fuzz 2% -trim +repage out.png`.
- Wikipedia's link previews (`.mwe-popups`) appear behind an Alt+click
  Glance; hide them with injected CSS for screenshots.
- Glance only opens http(s) links (file:// opens a popup window), so serve
  test pages with `python3 -I -m http.server` for Glance tests.
- Reliable fresh desktop captures: force a full repaint first by resizing
  the window by 2px and back over CDP (`Browser.setWindowBounds`), then
  `import -window`. A Glance page needs its own poke (any Runtime.evaluate).
- Xvfb without installing: the `xorg-server-xvfb` package extracted into
  `.cache/xvfb` runs (`.cache/xvfb/usr/bin/Xvfb :113`); XTest pointer moves
  via ctypes (`libXtst`). Without a window manager Chrome draws no caption
  buttons, so it can't test those. Display :99 belongs to someone else.
- The Linux frame has two kinds of caption buttons: `ImageButton`s in the
  GTK-native frame, `FrameCaptionButton`s in Chrome's own frame (used once
  a theme applies, e.g. a workspace tint). The frame is recreated on theme
  changes.

- **Off-screen http loads hang after a reboot** (#37): without
  `--password-store=basic` the network service asks gnome-keyring (D-Bus)
  for the cookie encryption key, and while the login keyring is locked every
  http(s) navigation hangs (data:/file:// still load). Symptoms: pages never
  commit, copy-URL shortcuts copy "", keys sent to a Glance page get lost.
  `KOAN_HEADLESS_UI_FLAGS` (scripts/koan_test_env.sh) now passes it.
- **Context menus off-screen:** Ozone/headless has a 1x1 screen unless
  `--ozone-override-screen-size` is given, so menus were clamped to nothing
  (UI DevTools showed MenuItemViews 0px wide). Also in
  `KOAN_HEADLESS_UI_FLAGS`. Menu items are clicked by sending the mouse
  event to the menu's `MenuHostRootView` at the item's position (the menu
  controller only takes events through its root view); a tab's context menu
  likewise opens from a right-click sent to the browser's root view.
