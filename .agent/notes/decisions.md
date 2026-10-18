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
