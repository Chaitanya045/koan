# Status

_Last updated: 2026-10-19_

## Done
- Project scaffold, scripts, GN args (`build/koan.gn`).
- Chromium checkout: 158.0.8111.0, main @ 67e29b8, 29 GB, hooks run.
- Upstream research → `docs/research-upstream.md` (key claims spot-checked
  against source and confirmed).
- Phase 1 design → `docs/phase1-design.md` (written by the lead agent from the
  real source after three subagent attempts failed).
- First full build of stock Chromium (`out/Koan`, target `chrome`, 4 jobs):
  started 2026-10-18 13:12, finished 2026-10-19 04:21 local (~15 h, ~70.5k
  steps). `chrome --version` → Chromium 158.0.8111.0; headless screenshot
  renders correctly.

- Phase 1 step 1 (skeleton): `//koan` symlink, `koan::features`,
  `KOAN_FEATURE_ENTRIES`, 2 upstream patches (4 lines), `apply_patches.sh`.
  Incremental rebuild: 5 steps, ~2 min. Verified with
  `scripts/check_flags.mjs`: both flags listed in chrome://flags.

- Phase 1 step 2: vertical tabs default = `IsSidebarEnabled()` (patch
  `tab_strip_prefs.cc` + `ui/tabs/BUILD.gn` dep). Verified on a fresh profile
  via chrome://prefs-internals and UI screenshots
  (`docs/screenshots/step2-*.png`); `--disable-features=KoanSidebar` gives
  horizontal tabs.

## Running
- Nothing.

## Next
1. Phase 1 step 3 from `docs/phase1-design.md` §6: hook plumbing
   (`koan_browser_view_hooks`, `browser_view.cc` patch), no visible change.
2. Don't apply patches under `chromium/src` while a build is running.
