# Decisions

- **Chromium fork with native C++ Views UI**, not Electron/CEF. Electron lacks
  full extension support, `chrome://` pages, profiles and sync, and the user
  wants maximum UI performance. Rarely used screens (settings, editors) may be
  Lit WebUI, never React.
- **Build on upstream features**: launched vertical tabs
  (`VerticalTabStripRegionView`) and split view (`MultiContentsView`).
  Glance, workspaces, Essentials and Linux compact mode are Koan-native.
- **Hook patches, not `chromium_src` shadowing** (for now): global include
  changes force a full rebuild; our patch set is ~30 lines. Revisit past ~30
  patched files. See `docs/phase1-design.md` §1.3.
- **Compact mode is not an `ImmersiveModeController`**: that path is tied to
  fullscreen and ChromeOS/macOS frame code. Koan uses its own controller,
  reusing vertical-tab expand-on-hover with collapsed width 0, plus a
  layer-transform slide for the toolbar.
- **Track Chromium main for now** (158.0.8111.0); pin a milestone before the
  first release, and re-audit hook points on each rebase.
- Name: **Koan** (not "Zen"/"Chrome" — trademarks).
- **Koan feature names must be globally unique and `kKoan`-prefixed.**
  `BASE_DECLARE_FEATURE` is `extern "C"` (`base/feature.h:48`), so the C++
  namespace doesn't mangle the symbol. Today all users are in
  `//chrome/browser` source_sets (one link unit), so no export macro is
  needed; if a real `component()` ever references one, declare it
  `COMPONENT_EXPORT(...) BASE_DECLARE_FEATURE(...)`.
- **Flag metadata deferred:** `koan-*` flags aren't in
  `chrome/browser/flag-metadata.json` / `enums.xml` (`LoginCustomFlags`), so
  `AboutFlagsTest.EveryFlagHasMetadata` and `AboutFlagsHistogramTest` will
  fail. Add the entries before running those tests (Phase 4).
- **Zen look via colours + MultiContentsView card, not new views.** The
  toolbar takes the frame colour (Koan colour mixer, runs after Chrome's
  mixers, before custom themes), and the single contents become a card using
  the same mechanism as upstream split view (insets + rounded active
  contents). Only the active contents are rounded: rounding
  MultiContentsView's background too leaves unpainted (black) corners
  because nothing is drawn behind it.
- **Zen's shortcuts, not Chrome's, where they collide** (#11): Koan follows
  Zen's defaults (ZenKeyboardShortcuts.mjs v21) in one table,
  `KoanShortcutController`, registered after BrowserView's accelerators so
  it wins; contextual ones fall through.
- **Glance on Alt+click via blink** (#17): with Koan Glance on, an Alt-only
  click becomes `kNavigationPolicyNewPopup`, which link clicks never produce
  otherwise; synthesized Alt+clicks and window.open() keep upstream
  behaviour. Constraint: blink core links its own copy of
  `//koan/browser:features` (component build), so a Koan feature must only
  be read in one process type (browser *or* renderer) unless the target is
  made a proper exported component.
