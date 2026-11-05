# Zen's glass on macOS, and how Koan does it (#79)

Source: Zen 1.24b (`.cache/zen-src-1.24b/`, `.cache/zen-src/omni/modules/zen/ZenGradientGenerator.mjs`).
Gecko's own window code (widget/cocoa) isn't in those trees; what the
window material is can only be read off the captures
(`.agent/notes/screenshots/zen-mac/`).

## What Zen does

- `zen-theme.css`: on macOS (`-moz-platform: macos`), `#main-window` is
  `background: transparent` and `--zen-themed-toolbar-bg-transparent:
  transparent`; `body`, `#browser`, `#tabbrowser-tabpanels`,
  `#navigator-toolbox` are transparent (`zen-browser-ui.css`). The window is
  a transparent window; the OS blur comes from Gecko's vibrancy.
- The paint over it is `.zen-browser-generic-background::after`: the theme
  gradient with `opacity: var(--zen-background-opacity)`.
- `ZenGradientGenerator` with `canBeTransparent` (macOS, Mica, Linux with
  `zen.widget.linux.transparency`):
  - slider opacity `currentOpacity`, default 0.5, range 0.25-0.8 (0.30-0.80
    on macOS);
  - each colour: `blendWithWhiteOverlay(color, opacity)`: on macOS blended
    toward white by `min(1, o + MIN + 0.6 * (1 - (o + MIN)))` (0.9 at the
    defaults), then drawn at alpha `o`;
  - no theme: `getBrowserBg()` is `rgba(0,0,0,0.4)` in dark mode and
    `transparent` in light mode.
- `--toolbar-background-color: light-dark(rgba(255,255,255,0.6),
  rgba(255,255,255,0.15))`: toolbars are translucent too.
- Pref `zen.theme.acrylic-sidebar` (default false, with
  `prefers-reduced-transparency` respected): adds `backdrop-filter:
  blur(42px)` on `.zen-toolbar-background`, and the base colour alpha 0.6.
  `zen.theme.acrylic-urlbar` (default false): the floating URL bar at
  rgba(255,255,255,.93) / rgba(20,20,20,.92) with `backdrop-filter:
  underlay blur(40px)`. By default the URL bar is opaque
  (`light-dark(white, rgb(24 24 24))`), as the capture shows.
- Popups on macOS are native menus (`-moz-default-appearance: menupopup`,
  `background-color: Menu`, no CSS shadow): macOS draws them translucent.
- The page: `.browserSidebarContainer` is an opaque rounded card with
  `--zen-big-shadow`.
- No toggle in the UI in 1.24b except the theme picker's opacity slider.

## What Koan does

Chromium 158 has a glass frame for macOS 26 (`GlassFrameService`,
`BrowserNativeWidgetMac::UpdateGlassEligibility`, the tabbed layout's glass
mode, `BrowserFrameViewMac::OnPaint`): a non-opaque window and compositor, an
`NSGlassEffectView` below the content view, the frame and sidebar painted at
alpha 0. Koan turns it on for macOS (still 26 or later: the compositor path
below it was never run by Chromium) and changes what differs from Zen, with
small hook patches (docs/REBASE.md):

1. every window, not one (`kMaxGlassWindows`); Battery Saver's rule dropped
   (Zen has none), the two opt-in/out promos disabled;
2. an `NSVisualEffectView` over the whole window instead of
   `NSGlassEffectView`: Zen's `ZenWindowMaterialView`, the HUD window
   material, behind-window, `appearance = nil`, not emphasized, following the
   window's active state (flat when inactive);
3. a tint view over it: Zen's default theme (`kColorKoanGlassTint`: dark
   black at 40%, light none), clear while the window is inactive (a Space
   theme that paints the window will hide it too: #62);
4. the toolbar and top container backgrounds at alpha 0 in glass mode (Zen's
   `#nav-bar` and toolbox are transparent), set once (the top container's
   upstream colour is skipped, not set and undone on every layout); in compact
   mode the floating sidebar, toolbar and top container keep their colours as
   a backing;
5. no `opaque_background_view_` (it would sit under the whole-window blur).
   Going from glass to opaque (fullscreen, Reduce Transparency, setting off,
   capture) still waits for the compositor's first opaque frame before the
   blur view goes (`RemoveGlassBackground`, unpatched).

The setting is Chromium's local-state pref `glass_frame.enabled` ("Translucent
window", Zen's `window-vibrancy`); Reduce Transparency
(`accessibilityDisplayShouldReduceTransparency` and its notification) turns it
off through `GlassFrameService::IsGlassFrameAllowed`. Zen has no other
control: the opacity is each Space theme's slider (#41).

The floating URL bar's glass pane is the local-state pref
`koan.look.acrylic_urlbar` (#105). Not done: Zen's in-view `acrylic-sidebar`
blur, bubbles and arrow panels (Chromium's are opaque Views; native menus are
already translucent).

## Adjusting the glass

Zen: one slider in the theme picker, per Space (`theme.opacity`, saved with
the Space's theme; default 0.5, range 0.25-0.80, 0.30-0.80 on macOS), next to
the grain dial (`theme.texture`, 16 steps). The slider is how opaque the
gradient is over the blur, so it is also how much blur shows. It applies live
(the picker previews while dragging and saves on close). There is no global
pref.

Koan: the slider, its persistence and the paint are #41's (the picker and
`KoanThemeController`); this side is only the blur, which is always there
while the window is in glass mode. Nothing toggles the window between opaque
and translucent when the value changes, so there is no flicker: the paint's
alpha is the whole control, and an opaque paint hides the blur. When #62
lands it brings what it needs from this side (a glass-mode query, a way to
hide the standalone tint, the observer, `EnsureTranslucentLayers`): see
`~/work/koan/.cache/glass-api-for-62/` for the code that was removed from #103
because nothing called it yet.

## Re-check against 1.24b (2026-11-03)

- `zen.view.grey-out-inactive-windows` (default true): an inactive window's
  wrapper is painted `InactiveCaption`; the captures are of inactive windows
  (hence flat grey, no blur). Koan: the effect view follows the window's
  active state (`NSVisualEffectStateFollowsWindowActive`). #80 has the rest
  of the greying (traffic lights etc.).
- (Reverted after review: `--toolbar-background-color` is Firefox's generic
  toolbar variable; Zen's `#nav-bar` and toolbox are `background:
  transparent`, so Koan's toolbar paints nothing in glass mode.)
- Still unknown without the Mac: Gecko's exact material (sidebar material
  chosen), the opaque wash colour of `InactiveCaption`.

## Defaults (checked 2026-11-03)

- `.cache/zen-src/zen-default-prefs.js` is **1.23.3b**; the 1.24b extraction
  has no prefs file. In it: `zen.theme.acrylic-sidebar` false,
  `zen.theme.acrylic-urlbar` false, `zen.view.grey-out-inactive-windows`
  true, `zen.view.window.scheme` 2 (system), `zen.widget.linux.transparency`
  false, `zen.theme.dark-mode-bias` 0.3. There is **no**
  `zen.widget.macos.window-material` (or any macOS window-material pref) in
  either tree: on macOS the window is always transparent
  (`canBeTransparent` is true for `-moz-platform: macos`, no pref), so
  Koan's "Translucent window" setting (default on) is an addition, with the
  default Zen has.
- 1.24b `getBrowserBg()`: dark `rgba(0,0,0,0.4)`; light `transparent` on
  macOS and Windows (`rgba(255,255,255,0.4)` only on Linux). Same as 1.23.3b
  on macOS.
- Asked the zen-ref thread for the real 1.24b pref dump and the window
  material (what Gecko's vibrancy is).

## Measured on real Zen 1.24b (zen-ref thread, ACTIVE window over striped backdrop)

Data: `~/work/koan/.cache/zen-ref/raw/` (`prefs-glass-theme.json`,
`glass-*.json`, screenshots `s/*.png`); prefs file copied to
`.cache/zen-src-1.24b/defaults/preferences/firefox.js`.

- Prefs: `zen.widget.macos.window-vibrancy` true (Koan's "Translucent
  window"), `zen.widget.macos.window-material` 1 (int, live),
  `widget.transparent-windows` true, `acrylic-sidebar`/`acrylic-urlbar`
  false, `zen.view.window.scheme` 2, `grey-out-inactive-windows` true.
- Material by pref value (mean RGB of a sidebar patch, dark, no theme):
  1 `44,61,78` (default, most see-through), 2 `41,57,72`, 3 `39,52,65`,
  4 `37,48,58`, 6 `35,38,42` (darkest); 0, 5, 7-12 all `37,43,50` (a
  fallback); vibrancy off `28,29,29` (flat). (Superseded by the source
  below: value 1 is the HUD window material.)
- Glass surfaces: the whole window background (sidebar, the 8pt ring, the top
  row). The page card is opaque for real pages and transparent for blank
  pages. URL pill and Essentials are ~13.5% light tints. Cmd+T panel opaque
  rgb(24,24,24) by default.
- Tint: dark no theme `rgba(0,0,0,.4)`; light none; colour `c*0.92+white*0.08`
  at the theme opacity (0.5; slider 0.30-0.80).
- Inactive: flat, no blur, no tint, opaque dark grey (so the Space theme's
  paint must not draw while the window is inactive).
- Reduce Transparency: Zen's CSS only gates the acrylic URL bar; the window is
  the system's behaviour.

## How Zen adjusts the glass (measured, zen-ref)

- Opacity slider (sine-wave, 250 wide): min 0.30, max 0.80, step 0.001,
  default 0.5. Grain dial: 66x66, 16 steps (k/16, 0..0.9375, 0 = off).
- Stored per Space in the theme object (`opacity`, `texture`), written when
  the picker closes; live while dragging; not in prefs. The only global piece
  is `zen.view.window.scheme` (0 dark, 1 light, 2 auto).
- macOS colour: `blendedAlpha = min(1, op + 0.30 + 0.60 * (1 - (op + 0.30)))`
  (0.84 at 0.3, 0.92 at 0.5, 1.0 from 0.7); layer alpha = `op`. The blur is
  the fixed system material. The default theme ignores the slider.
- Grain: tiled grain image above the colour layer at opacity `texture`.

## The canonical source (zen-desktop 1.24b, `.cache/zen-desktop-1.24b/`)

Replaces what was measured:
- `src/widget/cocoa/nsCocoaWindow-mm.patch`: `ZenWindowMaterialView :
  NSVisualEffectView`, blending behind window, `appearance = nil`,
  `emphasized = NO`, state `FollowsWindowActiveState`
  (`zen.view.grey-out-inactive-windows`, else Active), as the content view's
  wrapper. `zen.widget.macos.window-material` (default 1) maps 1 HUDWindow,
  2 FullScreenUI, 3 Popover, 4 Menu, 5 ToolTip, 6 HeaderView, 7 or other
  UnderWindowBackground. So Koan's material is `NSVisualEffectMaterialHUDWindow`
  and `--koan-glass-material` is gone.
- `prefs/zen/macos.yaml`: `zen.widget.macos.window-vibrancy` true.
- `prefs/zen/theme.yaml`: `acrylic-urlbar` and `acrylic-sidebar` are
  `@IS_TWILIGHT@`: true on the Twilight channel, false in releases.
- `src/zen/spaces/ZenGradientGenerator.mjs`: the no-theme tint (`rgba(0,0,0,.4)`
  dark; `transparent` light on macOS) and the macOS white overlay 0.6, as used.

## The opaque-layer aborts (root cause)

`CustomCornersBackground` CHECKs that its view's layer doesn't fill its
bounds opaquely (when it paints and in `SetUseBackgroundBlur`). Nothing in
Koan's glass code or upstream's views sets the sidebar region's layer opaque
(upstream creates it with `SetFillsBoundsOpaquely(false)` and never changes
it). The flip was in #41's `KoanThemeController::InvalidateBackground`: it set
the layers of the views it paints (frame, toolbar, sidebar region, gutter) to
`!translucent`, i.e. opaque whenever its paint was opaque (fullscreen, compact
mode, an inactive window, glass off, which includes switching apps), and the
region and the top container have a `CustomCornersBackground`.

The rule, kept by the CHECKs staying in upstream (a patch that turned them
into a quiet reset would leave an opaque layer rendering black over the blur,
with no log): views with a `CustomCornersBackground` never get an opaque
layer, and whatever paints them repaints on every change.
The helper for it, `koan::EnsureTranslucentLayers()` (makes any layer of the
views it is given translucent and repaints a view it changed), lands with #62,
the painter that needs it: call it on every invalidation (theme, opacity,
activation) and for a painter given a new layer (`View::SetPaintToLayer()`
makes an opaque one).
