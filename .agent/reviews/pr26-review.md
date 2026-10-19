# PR #26 review — Thin overlay scrollbars on Linux (feat/overlay-scrollbars)

Reviewed: 2026-10-19, against Chromium 158.0.8111.0 (CHROMIUM_VERSION pins
67e29b8287f1). All file:line references are into
`/home/chaitanya/work/koan/chromium/src` unless prefixed `patches/` or
`scripts/`.

## What the PR does

- `patches/ui/native_theme/features/native_theme_features.cc.patch`: adds
  `BUILDFLAG(IS_LINUX)` to the feature-state branch so
  `features::kOverlayScrollbar` is `FEATURE_ENABLED_BY_DEFAULT` on Linux, and
  changes the `mode` param default to `ScrollbarMode::kOverlay` on Linux
  (upstream default is `kDevice`).
- e2e test `scripts/e2e/scrollbars.mjs` + fixture `scripts/e2e/pages/scrollbars.html`.
- Docs: ZEN_PARITY.md row, before/after screenshot.

## (1) Does the flag actually produce thin overlay scrollbars on Linux?

**Web content: yes, and it goes through the well-travelled Fluent path.**

- On Linux, blink selects `ScrollbarThemeFluent`
  (core/scroll/scrollbar_theme_aura.cc:111-115 — `ScrollbarTheme::NativeTheme()`
  returns Fluent whenever `IsFluentScrollbarEnabled()`, which is `true` on
  Linux per ui/native_theme/features/native_theme_features.cc:74-80). Overlay
  behavior inside Fluent is gated only on `OverlayScrollbarsEnabled()`
  (core/scroll/scrollbar_theme_fluent.cc:116-119).
- That blink-side global is set at renderer startup from the renderer's own
  `NativeTheme`: content/child/runtime_features.cc:439
  (`WebRuntimeFeatures::EnableOverlayScrollbars(
    ui::NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar())`).
- In the renderer, `GetInstanceForWeb()` on Linux returns `NativeThemeFluent`,
  whose constructor computes
  `ShouldUseOverlayScrollbar(OsSettingsProvider::Get().PrefersOverlayScrollbars())`
  (ui/native_theme/native_theme_fluent.cc:77-80). With `mode=kOverlay`,
  `ShouldUseOverlayScrollbar` returns `true` regardless of the OS pref
  (ui/native_theme/features/native_theme_features.cc:90-99), so web content
  gets overlay scrollbars. ✓
- cc is wired accordingly: `enable_fluent_overlay_scrollbar` and
  `scrollbar_animator = AURA_OVERLAY` come from
  `ShouldUseDesktopOverlayScrollbars()` →
  `NativeTheme::GetInstanceForWeb()->use_overlay_scrollbar()`
  (blink/renderer/platform/widget/compositing/layer_tree_settings.cc:60-66,
  471-472, 491-497), and the fluent-overlay compositor fade paths
  (cc/trees/layer_tree_impl.cc:2499-2510,
  cc/layers/scrollbar_layer_impl_base.cc:139-147,
  cc/layers/painted_scrollbar_layer_impl.cc:190-215,
  ui/native_theme/overlay_scrollbar_constants.cc:11-19) are the same code
  Windows has been exercising for years. No Linux-only compositor gaps found.

**Browser Views UI: already overlay, unchanged by this PR.**

- On Linux, `views::ScrollView` builds its scrollbars via
  `PlatformStyle::CreateScrollBar`, which unconditionally returns
  `OverlayScrollBar` on Linux/ChromeOS
  (ui/views/style/platform_style.cc:36-43; called from
  ui/views/controls/scroll_view.cc:308-311). This is upstream code, not gated
  on the feature flag — so the browser chrome already had overlay scrollbars
  before this PR. The PR's Views-visible effect is limited to
  `NativeThemeAura` painting metrics (ui/native_theme/native_theme_aura.cc:86-88,
  95-103), which now also report overlay=true consistently.

**Linux code paths that previously assumed the flag was off — checked, none break:**

- `OsSettingsProviderSuppliesOverlayScrollbarPreference()`
  (ui/native_theme/native_theme.cc:106-118) now returns `true` on Linux
  (`IsFluentOverlayScrollbarEnabled()` becomes true). This *activates* two
  code paths that were dead on Linux:
  - `NativeTheme::UpdateWebInstance()` (native_theme.cc:458-467) starts
    copying `use_overlay_scrollbar` from the native-UI instance (Linux:
    `NativeThemeAura`) to the web instance (Linux: `NativeThemeFluent`).
  - `NativeTheme::CalculateUseOverlayScrollbar()` (native_theme.cc:634-640)
    stops returning the stored member and instead re-derives via
    `ShouldUseOverlayScrollbar(pref)`.
  Both converge on `true` with `mode=kOverlay` in every process
  (browser: the GTK provider's `gtk-overlay-scrolling` value is overridden by
  mode/overlay, ui/gtk/os_settings_provider_gtk.cc:63-74; renderer: the base
  `OsSettingsProvider` returns `false`, ui/native_theme/os_settings_provider.cc:231,
  but mode/overlay ignores it). I traced the initialization order
  (`GetInstanceForWeb()` → `SetAssociatedWebInstance` → `UpdateWebInstance()`,
  native_theme.cc:187-200, 460-467; `GetInstanceForNativeUi()` →
  `BeginObservingOsSettingChanges()` → `UpdateVariablesForToolkitSettings()`,
  native_theme.cc:176-184, 260-269, 568, 609) and there is no ordering where
  the copy can flip the web instance back to classic. ✓
- `ScrollbarThemeAura::SupportsDragSnapBack()` has a
  `#if BUILDFLAG(IS_LINUX)` = false (core/scroll/scrollbar_theme_aura.cc:126-134),
  but it only applies to the Aura theme, which Linux web content doesn't use
  when Fluent is enabled. Unchanged by this PR.
- No other `IS_LINUX`-gated scrollbar code exists under ui/views, ui/gtk,
  cc/, or chrome/ that assumes classic scrollbars (grepped
  `IsFluentOverlayScrollbarEnabled`, `IsOverlayScrollbarEnabledByFeatureFlag`,
  `use_overlay_scrollbar`).

**Could it break anything?**

- **`::-webkit-scrollbar`-styled pages: safe.** Pages with custom scrollbar
  CSS are explicitly exempt from overlay mode —
  third_party/blink/renderer/core/paint/paint_layer_scrollable_area.cc:1813-1815:
  `will_be_overlay = UsesOverlayScrollbars() && !has_custom_scrollbar_style`.
  So DevTools (docked or undocked), the PDF viewer shell, and most
  `chrome://` WebUIs (settings, bookmarks, history…) keep their custom
  classic scrollbars exactly as before.
- **PDF content**: PDFium paints its own scrollbars inside the plugin; the
  NativeTheme flag doesn't reach it. Unaffected.
- **Views ScrollView**: already `OverlayScrollBar` on Linux (above). Unaffected.
- **Accessibility**: see Should-fix #1 below — the one real regression
  scenario.

## (2) Is a browser-process-only default enough? Yes — it isn't browser-only.

The patch changes the *compiled-in* defaults
(`BASE_FEATURE(kOverlayScrollbar, …)` and the `constinit` `FeatureParam`),
which every process links from the same `ui/native_theme` component. There is
no dependency on the browser pushing the value over the command line or mojo:

- Renderer's blink bit: set from the renderer's own `NativeTheme` at startup
  (content/child/runtime_features.cc:439), which derives from the same
  compiled default. Consistent.
- `NativeTheme` mojo/preferences traffic (color scheme, contrast, forced
  colors) does not carry overlay-scrollbars; it doesn't need to, and
  `UpdateWebInstance()`'s copy path converges on `true` in both processes
  (traced above).
- Command-line overrides (`--disable-features=OverlayScrollbar`,
  `--enable-features=OverlayScrollbar:mode/classic`) are propagated to child
  processes by content, so the escape hatches work end-to-end too.

Forcing `kOverlay` rather than keeping `kDevice` is in fact *necessary* for
renderer consistency: with `kDevice`, the renderer has no GTK settings
provider, `OsSettingsProvider::Get()` falls back to the base impl whose
`PrefersOverlayScrollbars()` is `false` (ui/native_theme/os_settings_provider.cc:102-126,
231), so `NativeThemeFluent`'s ctor (native_theme_fluent.cc:77-80) would make
web content classic even when `gtk-overlay-scrolling` is `true`. The patch
comment ("also where no GTK settings are available (Qt, headless)") is
accurate.

## (3) Patch quality

- Preimage verified against the pinned revision: upstream
  158.0.8111.0@67e29b8287f1 matches the patch context byte-for-byte, and the
  postimage matches the applied tree (`git apply --reverse --check` passes).
  `scripts/ci/check_patches.sh` will validate this in CI as well.
- The `constinit const base::FeatureParam<ScrollbarMode>` initializer with a
  preprocessor branch inside the braced initializer is valid C++: each branch
  yields a constant expression, and `FeatureParam`'s constructor is
  `constexpr`, so `constinit` still holds. Both branches pass the same
  argument count/types.
- One real correctness nit: **`BUILDFLAG(IS_LINUX)` is false on ChromeOS in
  this tree** (build/build_config.h:92-97 — `OS_LINUX` is deliberately not
  defined when `OS_CHROMEOS`), so the second hunk does *not* leak `kOverlay`
  onto ChromeOS. I verified this rather than assuming; the first hunk adding
  `|| BUILDFLAG(IS_LINUX)` to a condition that already contains
  `IS_CHROMEOS` is therefore a no-op there too. No action needed, but worth
  knowing because the common folklore is the opposite.
- Style: upstream usually hoists the platform-conditional value into a
  separate constant (as the file already does for
  `kOverlayScrollbarFeatureState`) rather than branching inside the
  definition; either compiles, current form is fine and keeps the diff small.

## (4) E2E test (`scripts/e2e/scrollbars.mjs`, `scripts/e2e/pages/scrollbars.html`)

Sound overall, and it exercises the real default (run.sh launches Koan with
no scrollbar-related flags — `--enable-features=ui-debug-tools-enable-synthetic-events`
doesn't touch this feature), against a real window on headless Ozone.

- The overlay checks are correct and meaningful:
  `innerWidth - document.documentElement.clientWidth` is ~15px with classic
  scrollbars and 0 with overlay; same arithmetic for the inner scroller via
  `offsetWidth - clientWidth`. The fixture guarantees both scrollers overflow
  (150 / 30 lines), and the third check (`scrollHeight > innerHeight`) guards
  against the page not overflowing and trivially passing.
- Consistent with the existing harness style (glance.mjs uses the same
  global `WebSocket`/`fetch` pattern, so the Node-version dependency is a
  repo-wide assumption, not new).
- Robustness nits:
  - `evaluate()` has no timeout (glance.mjs's `send()` has a 5s one); if the
    target tab is wedged the test hangs until CI kills it. Consider reusing
    glance.mjs's connect/send helper with its timeout.
  - `evaluate()` attaches `onmessage` but not `onerror`/`onclose` before the
    first send, and picks the first `type === 'page'` target; if a second
    tab ever exists (future run.sh changes), it could test the wrong tab.
    Filtering by `url.includes('scrollbars.html')` would be cheap insurance.
  - The test verifies "takes no layout space", not "thin"; the doc comment
    says "thin overlay scrollbars". That's fine (thinness is the Fluent
    overlay look), just noting the claim it can't falsify.

## Findings

### Blocker

None.

### Should-fix

1. **`mode=kOverlay` ignores the user's GTK `gtk-overlay-scrolling=false`
   setting (accessibility scenario).** A GNOME user who enables
   "Always Show Scrollbars" (the low-vision/pointer-accessibility setting,
   which maps to `gtk-overlay-scrolling=false`) gets thin, auto-hiding
   scrollbars in all web content anyway, because mode/overlay bypasses
   `OsSettingsProvider::PrefersOverlayScrollbars()`
   (ui/native_theme/features/native_theme_features.cc:90-99,
   ui/gtk/os_settings_provider_gtk.cc:63-74). Zen follows the GTK setting, so
   this is a parity *and* a11y regression for those users. It is a deliberate,
   documented deviation (docs/ZEN_PARITY.md:25, "Koan always overlays"), and
   honoring the pref needs real cross-process plumbing (the renderer has no
   GTK provider — os_settings_provider.cc:111-126 — so `kDevice` alone would
   break web-content overlay for *everyone*). Options: (a) document the
   `--disable-features=OverlayScrollbar` / `mode/classic` escape hatch in
   ZEN_PARITY.md or README next to the new row, and (b) file a follow-up to
   plumb the GTK pref into the renderer (e.g. as a blink setting or via the
   NativeTheme mojo prefs) so mode/kDevice can become the default later.
2. **e2e hangs instead of failing** if the CDP target stops responding
   (`scrollbars.mjs:12-22` — no timeout on the Runtime.evaluate round-trip,
   unlike glance.mjs's 5s guard). Scenario: a broken or wedged renderer in CI
   burns the whole job timeout. Fix: add a 5s timeout resolve like
   `scripts/e2e/glance.mjs:18-20`.

### Nit

1. `scrollbars.mjs:14` picks the first `type === 'page'` target; filter on
   the fixture URL so the test can't silently evaluate a different tab.
2. The screenshot docs/screenshots/scrollbars-zen-vs-koan.png (360×2100) is
   an unlabeled crop of two window edges; a caption or alt text in
   ZEN_PARITY.md would help the next reader.
3. The upstream comment block above `kOverlayScrollbar` ("Enabled defaults to
   mode/device", native_theme_features.cc:37-44) is now stale for the Koan
   build on Linux; the Koan comment nearby covers it, so at most extend the
   Koan comment to say "upstream defaults to mode/device".

## Verdict

**Approve with nits — no blockers; ship it (two should-fix follow-ups: respect/document the GTK classic-scrollbars escape hatch, add a timeout to the e2e CDP round-trip).**
