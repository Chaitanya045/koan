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
- `pkill -f <pattern>` from the agent shell can kill the shell itself (exit
  144) because the pattern is in its own command line.
- Long runs must be detached (`setsid nohup ... & disown`): background Bash
  tasks are capped at 2 h, and a T3 Code restart kills child processes.
- Foreground `sleep` is blocked in the agent shell; wait with a background
  `while pgrep ...; do sleep 60; done` loop instead.
- Ubuntu's `install-build-deps.sh` doesn't apply (host is CachyOS/Arch); the
  Debian sysroot (`use_sysroot = true`) covers the target libs.
