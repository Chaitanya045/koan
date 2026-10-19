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

- Phase 1 step 3: `koan::OnBrowserViewCreated/Destroying` hooks at the end
  of the BrowserView ctor / start of the dtor; `KoanBrowserViewController`
  per window, owned by the BrowserView via an owned ui::ClassProperty
  (`KoanBrowserViewController::From(browser_view)`). Verified: 3 windows
  opened/closed via CDP, balanced create/destroy logs
  (`--enable-logging=stderr --vmodule=koan_browser_view_controller=1`), clean
  exit, no DCHECKs.

- Phase 1 step 4a (Zen surfaces): Koan colour mixer (toolbar = frame colour,
  no content/sidebar separators) and the content card (MultiContentsView
  insets the single contents by 8px right/bottom, 8px radius on the active
  contents only, separators hidden). Screenshots: `docs/screenshots/step4-*`.
  Light, dark (`--force-dark-mode`) and flag-off all checked.

- Phase 1 steps 5-7 (compact mode), first cut: `KoanCompactModeController`
  per window. Sidebar: strip collapsed to 0 width (hook in
  `CalculateHorizontalLayout`) + upstream expand-on-hover overlay, opened by
  a leading-edge `KoanHotZone` taking an `ExpandOnHoverLock`. Toolbar: top
  container reparented into the NonClientView's `TopContainerOverlayView`
  and slid with a layer transform; top-edge hot zone + omnibox focus reveal,
  400 ms hide delay. Card gutter on all sides in compact. Ctrl+Alt+C via
  FocusManager accelerator (no upstream patch); pref
  `koan.compact_mode.enabled` (registered from tab_strip_prefs.cc). Verified
  off-screen: hidden state, sidebar reveal, toolbar reveal
  (`docs/screenshots/step5-*`).
- Google API keys infobar suppressed (`koan::kShowGoogleApiKeysInfoBar`).

- Phase 2: workspaces (focus mode), switcher, Ctrl+Alt+Left/Right/N, new
  tabs adopted, session restore default LAST (workspaces survive restart),
  per-workspace tint via ThemeService user colour (saved/restored).
- Phase 3: Glance on Shift+click (child widget overlay; expand / split /
  close); upstream split view.
- Phase 4: branding (`scripts/apply_branding.sh`: strings, BRANDING, logos;
  profile `~/.config/koan`, desktop id `koan.desktop`, icon `koan`),
  `scripts/install_desktop.sh`, `scripts/update_chromium.sh` +
  `docs/REBASE.md`, `docs/USAGE.md`.

- Keyboard shortcuts verified off-screen through the real key path
  (`scripts/cdp_key.mjs`: page -> unhandled -> FocusManager): Ctrl+Alt+C,
  Ctrl+Alt+N, Ctrl+Alt+Left/Right.
- Focused workspace hides upstream's "Unfocus" pill (hook in
  `tab_group_view_layout.cc`); the switcher's ∞ replaces it.

- Phase 1b single toolbar: KoanCompactModeController has modes (compact /
  single toolbar); KoanSidebarNavView in the sidebar; Ctrl+L / pill reveal
  the toolbar as a floating URL bar. Verified off-screen.
- WebUI `cr:chrome-product` icon replaced with the ensō by branding.py.

## Known issues
- Compact mode: Koan draws its own min/max/close buttons in the revealed
  toolbar's trailing corner when the frame reports caption buttons there
  (frame-drawn ones are under the page in compact mode). Headless has no
  caption buttons, so this path is untested until a desktop run.
- Compact toolbar spans the full width and overlaps the expanded sidebar's
  header.

## Running
- Nothing.

## Next
1. Finish compact mode: keyboard toggle test, desktop check, caption
   buttons; then Phase 1b (single toolbar) or Phase 2.
3. Don't apply patches under `chromium/src` while a build is running.
