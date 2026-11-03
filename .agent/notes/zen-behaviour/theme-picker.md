# Theme picker (per-Space colours, opacity, grain, colour scheme)

Zen 1.24b. Source: `src/zen/spaces/ZenGradientGenerator.mjs` (2137 lines, class `nsZenThemePicker`, global
`gZenThemePicker`), `src/browser/base/content/zen-panels/theme-picker.inc` (panel markup), `zen-gradient-generator.css`,
`src/zen/common/zen-sets.js` (command), `locales/en-US/browser/browser/{zen-workspaces,zen-general,zen-command-palette}.ftl`,
`prefs/zen/theme.yaml`. The **rendering model** (glass layer, alpha, blend formulas, per-surface translucency, measured
RGB values, inactive-window flatness) is in `.agent/notes/zen-mac-spec.md` section 15 and `zen-colors.json`; this file is
the **interaction** side. Related: `spaces.md` (the theme is stored per Space), `fullscreen-and-notch.md`.

## 1. Model

- Each Space has its own `theme`: up to **3 colour dots** (`MAX_DOTS = 3`), an **opacity** (0.30..0.80 on macOS; default state 0.5),
  a **texture / grain** (0..15/16 in 16 steps; 0 = none) and the algorithm name (`harmony`) used to place the dots.
  Stored in the session file with the Space (not in prefs, spec 15). The **colour scheme** (auto / light / dark) is
  **global**: the pref `zen.view.window.scheme` (2 auto, 1 light, 0 dark).
- Editing is **live**: every change re-renders the Space's theme layer immediately but is only **saved when the panel
  closes** (`handlePanelClose` -> `updateCurrentWorkspace(false)` -> `saveWorkspace`). Closing without a change saves nothing
  (`updated` flag).

## 2. Opening the picker

| Input | Zen does |
|---|---|
| Right-click the sidebar's toolbar area -> **Theme…** (`zenToolbarThemePicker`, inserted before "Set profile") | opens for the **active** Space |
| Space "more actions" menu (the `...` on the Space header, `zenWorkspaceMoreActions`): Change Name, Change Icon, **Theme…**, … | same |
| Menu bar **Spaces** menu (`zen-spaces-menubar`): Create Space, **Theme…**, Change Name, Change Icon, … | same |
| Command palette / URL-bar action **"Open Theme Picker"** (`cmd_zenOpenZenThemePicker`) | same |
| Create-Space form's "edit theme" button (`zen-workspace-creation-edit-theme-button`) | same panel, anchored 160 px higher (`y: -160`) |
| Library -> Spaces -> a Space's theme button (`openThemePickerForWorkspace`) | edits **that** Space (not the active one), anchored `bottomleft topleft` with the popover tail (`hidepopovertail` removed), live preview applies to that Space's theme layer only |

Placement for the active-Space case (`openThemePicker`): `PanelMultiView.openPopup(panel, #TabsToolbar, { position:
"topright topleft", x: -10, y: 0 })`, flipped to `topleft topright` with a right-hand sidebar. Live-measured placement (spec section 4/15): panel left = sidebar width + 7, vertically centred on `#TabsToolbar`; no tail
(`hidepopovertail="true"`). Panel type arrow, role
document, frosted-glass background (spec 15.4).

## 3. Panel anatomy (top to bottom)

1. **Colour wheel** (`.zen-theme-picker-gradient`, a round gradient pad, radius = half its width). Overlaid on it:
   - top-left: three **scheme buttons** (auto / light / dark; `PanelUI-zen-gradient-generator-scheme-*`);
   - bottom: three **colour action buttons**: **add** a colour, **remove** a colour, **toggle algorithm**
     (the algorithm names in the locale are Complementary, Split, Analogous, Triadic, Floating; whether the buttons show text or icons: LIVE CHECK 5);
   - the hint label **"Click to add a color"** (`zen-panel-ui-gradient-click-to-add`), absolutely centred on the wheel, 600 weight, small, no pointer events (whether it hides after the first dot: LIVE CHECK 9).
2. **Colour pages**: a row with a left chevron, a pager of 5 pages, a right chevron. Pages (each a row of 8-9 swatches):
   1. light single colours (lightness 90..50), 2. light **analogous trios**, 3. dark single colours (10..20), 4. dark analogous trios,
   5. **greys**: 9 swatches from `rgb(224,224,224)` to black (`explicit-black-white` type).
   Chevrons: the left one starts disabled and is disabled again on page 1; the right one is disabled on the last page
   (the index arithmetic uses modulo, the buttons' disabled state tracks the ends); mouse-wheel scrolling over the pages is blocked.
3. **Controls row**: the **opacity slider** drawn as a wavy line (`PanelUI-zen-gradient-slider-wave`, SVG path that morphs
   between a straight line and a sine wave with the slider value; thumb is the range input; min **0.30**, max **0.80**,
   step 0.001 on macOS, 0.25 min elsewhere) and the **texture dial** (`...texture-wrapper`): a ring of **16 small dots** with
   a draggable handle.
4. **Custom colours**: label "Custom Color", a colour input, an opacity number (0..1, step .01, default 1), an **add**
   button, and a list of added colours (each: swatch, hex label, remove button). The whole section is hidden unless
   `zen.theme.gradient.show-custom-colors` is true (default **false**).

## 4. Interactions

### 4.1 Wheel

- **Click empty wheel, no dots yet**: places the **primary dot** there (clamped to the circle), colour from position
  (hue = angle, saturation = distance; lightness = current lightness 50 default). 
- **Click empty wheel with dots**: **moves the primary dot** there (spring `0.4 s`, bounce 0.3) and recomputes the other
  dots by the current harmony (they follow, each animated 0.4 s spring).
- **Click a non-primary dot**: it becomes the **primary** (IDs swap, z-index raised, harmony recalculated).
- **Drag a dot**: mouse-down on a dot starts dragging (class `dragging`); it follows the pointer clamped to the wheel,
  the other dots follow live with the harmony **without animation** while dragging; mouse-up ends it. A click right after
  a drag is ignored for 100 ms (`recentlyDragged`). Right mouse button on a dot does not start a drag.
- **Right-click a dot (mouse-up)**: **removes** it; remaining dots are renumbered, the first becomes primary, the harmony
  is recomputed for the new count.
- **Add colour button**: ignored at 3 dots; otherwise adds a dot at the wheel centre and re-lays out by the next harmony
  (1 -> 2: *complementary* 180 deg; 2 -> 3: *analogous* 50/310 deg; or whatever matches the selected algorithm +1).
- **Remove colour button**: removes the **last** dot (highest ID), renumbers.
- **Toggle algorithm button**: cycles among harmonies whose angle count equals `dots - 1`:
  - 2 dots: complementary (180), singleAnalogous (310);
  - 3 dots: splitComplementary (150, 210), analogous (50, 310), triadic (120, 240);
  - 1 dot: floating (no partner).
  Dots are re-placed at the same distance from the centre as the primary, at the harmony's angles from the primary's angle.
- All dot moves update the preview live; the primary dot's colour is the Space's accent.

### 4.2 Colour pages (presets)

Click a swatch: sets the harmony (`float` = single dot, `analogous` = 3 dots), the current lightness, removes extra dots
(`data-num-dots`), places the primary dot at the swatch's `data-position` and derives the other dots by the harmony. Greys use
the black-and-white type (no hue; position along a line decides the grey). Updates live.

### 4.3 Opacity (wavy slider)

`input` event: sets `currentOpacity`, re-renders live; a **haptic tick every 0.1** step (`Math.round(opacity*10)` changes).
Opacity affects the layer's alpha and the blend with white (`blendWithWhiteOverlay`, spec 15.5): more opacity = more colour,
less glass.

### 4.4 Texture dial

Mouse-down on the dial handle, drag: the handle follows the pointer around the ring; the value is the angle snapped to
**16 steps** (`round(angle/360*16)/16`, 12 o'clock = 0, clockwise); one haptic tick per step change; 16 back to 0. Re-renders the grain
overlay live (`--zen-grainy-background-opacity`, noise texture `zen-noise`).

### 4.5 Scheme buttons

Click: sets `zen.view.window.scheme` immediately (auto 2, light 1, dark 0) and saves nothing else; the UI switches light/dark at
once; the click does not close the panel. A very dark/light colour still flips the foreground (spec 15.5, `shouldBeDarkMode`).

### 4.6 Custom colour (if enabled)

Type or pick a colour (hex with or without `#`; 3-6 digits get `#` added), optional opacity < 1 (appended as `AA` hex when the
input is `#rrggbb`), press add: a hidden "custom" dot is prepended to the list, the theme updates. The list item's x removes it
(`removeCustomColor`). Custom colours are used verbatim (not through the wheel).

### 4.7 Closing

Click outside / Esc / re-invoke closes the popup; if anything changed, it saves; then the preview is reset to the **active**
Space's saved theme (relevant for the Library case where another Space was being edited). The panel's "popuphidden" removes
the wheel listeners.

## 5. What changes visually

See spec section 15 and `zen-colors.json`. In short: the whole-window theme layer `#zen-browser-background` is repainted with
the dots' colours at alpha = opacity (single colour: `rgba(c*0.92 + white*0.08, opacity)`; 2-3 colours: stacked linear/radial
gradients), the sidebar accent (`--zen-primary-color`) follows the primary dot, the UI foreground flips light/dark by luminance,
and the Space cross-fade (`spaces.md` 3) uses the per-Space gradient cache (`getGradientForWorkspace`). Theme edits on a
non-active Space (Library) refresh that Space's gradient cache and notify observers (`zen-space-gradient-update`).

## 6. Prefs and constants

| Name | Value |
|---|---|
| `zen.view.window.scheme` | 2 (auto) |
| `zen.theme.gradient.show-custom-colors` | false |
| `zen.theme.gradient-legacy-version` | 1 (legacy 0 = old dot placement; set to 1 on first edit) |
| `zen.theme.dark-mode-bias` | 0.3 (yaml) / 0.5 (code fallback) |
| `zen.theme.acrylic-sidebar` | false |
| `zen.theme.use-system-colors` / `zen.theme.accent-color` | false / `AccentColor` |
| Max dots | 3; dot move spring 0.4 s, bounce 0.3; opacity range 0.30-0.80 (mac); texture steps 16; harmony angles above |

## 7. LIVE CHECK list

1. Panel position and size (spec 15 has them; recheck with a window-only capture), wheel diameter, dot size (primary vs others).
2. Slider default thumb position (markup says `value=0.4`; state default 0.5), wave animation while dragging, haptics not testable.
3. Texture dial look: dot ring size, handle look, which angle is 0.
4. What the swatches page indicator looks like and whether chevrons wrap visually.
5. Whether the add/remove/toggle buttons show labels or only icons; tooltips.
6. Scheme buttons: current-choice highlight, effect on a light colour.
7. Right-click on a dot removes it: confirm, and that the context menu does not appear.
8. Save timing: change, close the panel, relaunch: theme persists; change without closing the panel, quit: lost?
9. "Click to add a color" label position and whether it hides after the first dot.

## 8. Test script (Mac loop; Zen and Koan)

1. Open a Space with no theme: Theme… from the toolbar menu: the panel opens beside the sidebar; wheel is empty with "Click to add a color".
2. Click the wheel at the top-right: a dot appears, the window tint changes at once (alpha 0.5). Click elsewhere: the dot springs there (0.4 s).
3. Add colour: a second dot opposite (complementary); add again: three dots (analogous); add again: nothing. Toggle algorithm: dots re-lay out (split, triadic, analogous cycling).
4. Drag the primary dot: others follow live without animation. Right-click a dot: removed, dots re-lay out.
5. Page through the swatch pages; click a swatch from each: dots/tint change; greys page gives neutral tints.
6. Slide opacity from min to max: tint strengthens, glass shows less; texture dial: 16 snap positions, grain appears.
7. Click Light then Dark scheme buttons: UI flips immediately; Auto restores follow-system.
8. Close the panel: theme persists on Space switch and on relaunch; the other Space is unaffected.
