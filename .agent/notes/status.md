# Status

_Last updated: 2026-10-18_

## Done
- Project scaffold, scripts, GN args (`build/koan.gn`).
- Chromium checkout: 158.0.8111.0, main @ 67e29b8, 29 GB, hooks run.
- Upstream research → `docs/research-upstream.md` (key claims spot-checked
  against source and confirmed).
- Phase 1 design → `docs/phase1-design.md` (written by the lead agent from the
  real source after three subagent attempts failed).

## Running
- First full build of stock Chromium: `out/Koan`, target `chrome`, started
  2026-10-18 13:12 local. Detached with `setsid nohup nice -n 10`, 4 jobs.
  Log: `logs/build.log` (siso prints little; use
  `siso ps -C out/Koan` from `chromium/src` with depot_tools on PATH).
  Check alive: `pgrep -f scripts/build.sh`.
  Progress 2026-10-18 ~15:15 local: 19,113 / 76,235 steps (25%) after ~2 h.
  Background watcher tasks are capped at 2 h and are not restarted, so
  check progress manually with `siso ps`.

## Next
1. When the build finishes: launch
   `chromium/src/out/Koan/chrome --user-data-dir=/tmp/koan-test` and confirm it runs.
2. Phase 1 step 1 from `docs/phase1-design.md` §6: `src/` tree, `chromium/src/koan`
   symlink, `scripts/apply_patches.sh`, `//koan/browser:features`, BUILD.gn +
   about_flags patches.
3. Don't apply patches under `chromium/src` while a build is running.
