# Build notes

## Machine
- 8 cores, 15 GB RAM, zram 15.5 GB + 16 GB swapfile, ~780 GB free disk.
- Other apps typically hold 8–10 GB RAM, so only ~5–7 GB is free for the
  build. Use `JOBS=4` (`scripts/build.sh` defaults to 5). Raise only if
  `free -g` shows more headroom.
- First full build: expect 10–15 h. Incremental: minutes (component build).

## Gotchas
- **Full-rebuild trap:** changing anything that alters every compile command —
  `build/config/BUILDCONFIG.gn` `default_compiler_configs`, global
  `include_dirs`, or most `args.gn` values (`is_debug`, `symbol_level`,
  `dcheck_always_on`, ...) — invalidates every object and costs another
  10–15 h. Decide such things before a long build, never casually.
- `enable_nacl` no longer exists in this Chromium; GN warns if set. Removed.
- Builds use **siso**, not ninja; autoninja passes `-j` through.
- Long runs must be detached (`setsid nohup ... & disown`): background Bash
  tasks are capped at 2 h, and a T3 Code restart kills child processes.
- Foreground `sleep` is blocked in the agent shell; wait with a background
  `while pgrep ...; do sleep 60; done` loop instead.
- Ubuntu's `install-build-deps.sh` doesn't apply (host is CachyOS/Arch); the
  Debian sysroot (`use_sysroot = true`) covers the target libs.
