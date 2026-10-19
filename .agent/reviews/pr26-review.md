# PR #26 review — Thin overlay scrollbars on Linux

Reviewer: opencode-go/glm-5.3-flash (variant high), 2026-10-19. Condensed
record: the original file was lost from the working tree after the review;
findings below are as reported.

## Checked (no issue)
- Web content: on Linux blink uses `ScrollbarThemeFluent`
  (core/scroll/scrollbar_theme_aura.cc:111-115); overlay is gated on
  `OverlayScrollbarsEnabled()`, set at renderer startup from the renderer's
  own NativeTheme (content/child/runtime_features.cc:439). With mode=overlay,
  `ShouldUseOverlayScrollbar` is true regardless of the OS pref, so web
  content gets overlay scrollbars; cc's fluent-overlay paths are the ones
  Windows uses.
- Browser Views UI already used `OverlayScrollBar` on Linux
  (ui/views/style/platform_style.cc:36-43); unchanged.
- `OsSettingsProviderSuppliesOverlayScrollbarPreference()` now true on
  Linux, activating `UpdateWebInstance` copying and
  `CalculateUseOverlayScrollbar` re-derivation; both converge on true in
  every process.
- Pages with `::-webkit-scrollbar` styles (DevTools, PDF viewer shell, most
  chrome:// pages) are exempt from overlay
  (paint_layer_scrollable_area.cc:1813-1815). PDFium paints its own.
- Not browser-only: the compiled defaults are linked into every process;
  command-line overrides propagate to children. kOverlay (not kDevice) is
  needed because renderers have no GTK settings provider.
- Patch applies to the pinned revision; the constinit FeatureParam with a
  preprocessor branch is valid; IS_LINUX is false on ChromeOS.

## Findings
- Should-fix 1: mode=overlay ignores GTK `gtk-overlay-scrolling=false`
  (GNOME "Always Show Scrollbars", an accessibility setting) that Zen
  follows. Document the `--disable-features=OverlayScrollbar` escape hatch;
  follow up to plumb the GTK value into renderers.
- Should-fix 2: e2e `evaluate()` has no timeout; a wedged renderer hangs CI.
- Nit 1: e2e picks the first page target; filter by the fixture URL.
- Nit 2: the comparison screenshot needs a caption.
- Nit 3: upstream comment "defaults to mode/device" is stale for Linux.

## Verdict
Approve with nits: no blockers.
