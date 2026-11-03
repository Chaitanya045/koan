# Zen's icons, surface by surface (for #106)

Zen 1.24b on macOS. Measured in the running app (DOM through Marionette:
`list-style-image` / `background-image` / `mask-image` / `<image src>`, computed
size, colour, opacity) and read from the app bundle's own stylesheet that maps
icons to elements, `zen-icons/icons.css`. Pt = CSS px on this Mac (2x).

## 0. Where the icons are, and what they are

- Extracted copy (read-only source, not in the repo):
  `~/work/koan/.cache/zen-src-1.24b/chrome/browser/skin/classic/browser/zen-icons/`
  - **160** top-level SVGs, **87** in `selectable/`, **9** in `library/`,
    plus `icons.css` (the selector -> icon map, 1160 lines).
  - `.../preferences/*.svg` and `zen-preferences.css` (Settings), and the
    toolkit set `chrome/toolkit/skin/classic/global/icons/` (107 SVGs, Firefox's
    own) were copied too.
- Resource URLs: `chrome://browser/skin/zen-icons/<name>.svg`.
- **How an icon gets its colour**: every SVG is `fill="context-fill"
  fill-opacity="context-fill-opacity"`; the element sets
  `-moz-context-properties: fill, fill-opacity; fill: currentColor`. So an icon
  has **no colour of its own**: it is the element's text colour (see
  zen-colors.json) and its opacity. A Koan port must tint monochrome SVGs with the
  same variables, not bake colours in.
- **Grid**: 152 of the 160 top-level icons are `viewBox 0 0 18 18` (18 x 18
  outline/solid glyphs with 0.75-1.5 pt strokes); the 87 `selectable/` icons are
  `viewBox 0 0 512 512`; the library sprites are strips of 18 x 18 frames
  (`width = 18 x frames`, e.g. 648 x 18 = 36 frames).
- **Render size** (macOS): the glyph size is a theme variable,
  `--zen-toolbar-button-size: 17px` on macOS (16 elsewhere). A toolbar button is
  31 x 29 with the icon `<image>` 29 x 29 and 6 pt padding, so the glyph is
  **17 x 17**. Other sizes are listed per surface.

### Licence (settled from Zen's own repo, tag 1.24b)

Source: `github.com/zen-browser/desktop` at 1.24b, cloned at
`~/work/koan/.cache/zen-desktop-1.24b/`, folder
`src/browser/themes/shared/zen-icons/`. Its `jar.inc.mn` builds the shipped
`zen-icons/` from two source folders on every platform:

| Source folder | Icons | What Zen says | Licence |
|---|---|---|---|
| `nucleo/` | **158** SVGs: every UI icon (back, reload, close, plus, sidebar, menu, window, history, downloads, settings, ...) | `nucleo/nucleo-copyright-notice.html`: *"The Nucleo icons are copyrighted. **Redistribution is not permitted.** Use in source and binary forms, with or without modification, is permitted only if you possess a Nucleo license."* (https://nucleoapp.com/license, version 1.2, 23 Nov 2023) | **Nucleo commercial licence. Koan must NOT copy these files or ship them without its own Nucleo licence.** |
| `common/` (98 SVGs) | `blank.svg`, `urlbar-arrow.svg`, `library/` (9: `library-*-sprite.svg` with MPL headers, `library.svg`, `library-tile.svg`), `selectable/` (87) | `common/selectable/CREDITS.md`: *"Credit to Ionicons by Ionic Framework, used under the MIT License."* | `selectable/`: **MIT (Ionicons)**, with that credit; the rest is Zen's own, **MPL-2.0** (the repo's LICENSE; the sprite files carry the MPL header) |
| `fluent/`, `sf/` | **empty** (only a `NOTE.md` each) | *"Windows icons are using Nucleo icons. This until we figure out how sizing works with Fluent icons."* / *"MacOS icons are using Nucleo icons. This until we figure out how sizing works with SF Pro icons."* | n/a: there is no Fluent or SF Symbols set to reuse |

So of the 160 top-level icons in the app, **158 are Nucleo** (copyrighted) and 2
(`blank.svg`, `urlbar-arrow.svg`) are Zen's. The built app's SVGs are the
repo's sources run through `svgo` (`update-resources.sh`), so no file in the app
or the repo is byte-identical; `icons.css` (the selector map) is identical and
MPL-2.0.

| Other icon groups | Licence |
|---|---|
| Settings category icons (`preferences/category-*.svg`, `customize.svg`, `window.svg`, `quickactions.svg`) and the toolkit set `chrome://global/skin/icons/*.svg` | Firefox's own: **MPL-2.0** (Mozilla) |
| `zen-images/favicons/*.svg` (onboarding Essentials: Obsidian, Discord, ...) | third-party brand marks (trademarks of their owners) |

**What Koan can do:**
1. **Take freely** (MPL-2.0 header + a THIRD_PARTY_NOTICES row, see
   CONTRIBUTING.md): `icons.css` as the map, the 7 library sprites, `library.svg`,
   `library-tile.svg`, `blank.svg`, `urlbar-arrow.svg`; the Firefox preference and
   toolkit icons where needed.
2. **Take with credit** (MIT): the 87 `selectable/` Space icons (Ionicons), keeping
   Zen's credit line.
3. **Do not take**: the 158 Nucleo icons. Options: buy a Nucleo licence for Koan
   (then use the files directly), or draw an **equivalent set** to the same 18 x 18
   grid and weights, or use an open set of the same style (MIT/ISC/Apache icon
   sets with a matching outline weight). The inventory below gives, for every
   surface, the icon's **name** (the file name tells the glyph), size, colour and
   opacity, so any set can be matched icon for icon.

## 1. Surfaces

Colours are the element's `currentColor`/fill in dark; for light use the
matching token in `zen-colors.json` (toolbar text `rgba(4,26,51,.9)`, accent
etc.). "op" = effective opacity including ancestors.

### 1.1 Sidebar top row (row 1)

| Control | Icon file | Glyph | Box | Colour / opacity |
|---|---|---|---|---|
| Sidebar toggle `#zen-toggle-compact-mode` | `sidebar.svg` (RTL / tabs on the right: `sidebar-right.svg`) | 17 x 17 | 31 x 29 | `rgb(251,251,254)` (toolbar text), op 1 |
| Back `#back-button` | `back.svg` | 17 x 17 | 31 x 29 | same; **op 0.5 when disabled** |
| Forward `#forward-button` | `forward.svg` | 17 x 17 | 31 x 29 | same; op 0.5 when disabled |
| Reload `#reload-button` | `reload.svg` (`close.svg` is `#stop-button` while loading) | 17 x 17 | 31 x 29 | same |
| (hidden) app menu button `#PanelUI-menu-button` | `menu.svg` | 16 | 28 x 28 | op 0 (hidden, Zen shows it only in other layouts) |
| Traffic lights | native macOS | 14 pt circles | | grey when inactive |

Hover/pressed fills: `--toolbarbutton-background-color-hover` (7%) and `-active`
(10%) of `--zen-branding-bg-reverse`.

### 1.2 URL pill and the floating bar

| Element | Icon file | Size | Colour / opacity |
|---|---|---|---|
| Pill: site/identity icon `#identity-icon` | the site favicon; fallbacks `info.svg` (internal), `security.svg` (https), `security-broken.svg` (not secure), `extension.svg`, `page-portrait.svg` (file), `search-glass.svg` (while the bar is open) | 16 x 16 | `rgb(251,251,254)`, op 1 |
| Pill: copy-URL button (`single-toolbar-show-copy-url`) | `link.svg` (`#zen-copy-url-button image`) | 16 | 70% on hover (as toolbar buttons) |
| Pill: site-data / permissions button `#zen-site-data-icon-button` | `permissions.svg` / `permissions-fill.svg` | 16 | |
| Floating bar: search glyph (identity) | `global/icons/search-glass.svg` | 16 x 16 | `rgb(251,251,254)` |
| Result row favicon | page favicon, else `global/icons/defaultFavicon.svg`; tab results `tab.svg`, history `history.svg` | **24 x 24 tile** (radius 4, 16 pt glyph) | row text colour, white 70% |
| "Switch to Tab" key hint | `urlbar-arrow.svg` as a CSS **mask** on the keycap | ~12 | row text, op .7 |
| Page-action icons in the pill (reader mode, bookmark, share) | `reader-mode.svg`, `bookmark-hollow.svg` / `bookmark.svg`, `share.svg` | 16 | |

### 1.3 Tab rows

| Element | Icon | Size | Colour / opacity |
|---|---|---|---|
| Favicon `.tab-icon-image` | site favicon (internal pages: `settings.svg`, `extension.svg`, `customize.svg`, `new-tab-image.svg` for new tabs) | **16 x 16**, radius 4, 10 pt gap to the label | favicon colours |
| Close `.tab-close-button` | `close.svg` | 24 x 24 box, 4 pt padding => **16** glyph | `rgba(204,212,221,.9)`, shown on hover |
| Reset pinned `.tab-reset-button` | `unpin.svg` (pinned tab changed), `close.svg` | 16 | |
| Audio state `.tab-icon-overlay` | `tab-audio-playing-small.svg`, `tab-audio-muted-small.svg`, `tab-audio-blocked-small.svg` | small (~12) | |
| "+ New Tab" row `#tabs-newtab-button` | `plus.svg` | **16 x 16** (row 190 x 36) | **`rgba(204,212,221,.63)`** (dim text) |
| Folder (tab group) | `folder.svg`; chevron `arrow-right.svg` (`.zen-current-workspace-indicator-chevron`, rotates) | 16 | label colour |
| Drag handle (rearrange) | `drag-indicator.svg` | 16 | |
| Loading / throbber | hidden (`zen.theme.hide-tab-throbber`) | | |

### 1.4 Essentials

Tile favicon 16 x 16 (radius 4) centred in the 50-high tile (59.5 / 62 / 70 wide);
the tile background is the 13.5% tint (zen-colors.json). Essentials have **no
close icon**; unload/pin state uses `unpin.svg` on hover (`.tab-reset-button`).
Add/remove from Essentials in menus: `essential-add.svg`, `essential-remove.svg`.

### 1.5 Space header, divider, bottom bar

| Element | Icon | Size | Colour / opacity |
|---|---|---|---|
| Space icon `.zen-current-workspace-indicator-icon` | an **emoji** (default) or one of the 87 `selectable/*.svg` | 16 x 16 (+2 pt top padding) | emoji colours; selectable glyphs use the label colour |
| Space name | text 13 / 600 | | `rgba(204,212,221,.63)` |
| "Clear" `.zen-workspace-close-unpinned-tabs-button` | `dart-down.svg` + label | **10 x 10** glyph, button 36.9 x 18.5 | `rgba(204,212,221,.9)`, **op 0 at rest, 0.5 on sidebar hover** |
| Space actions `.zen-workspaces-actions` | `menu.svg` | 16 | |
| Space reorder icon | `drag-indicator.svg` | 16 | |
| **Bottom bar**: Library `#zen-library-button` | `library/library-sprite.svg` (18 x 18 frame from a strip; scale 1.08 => 19.4) | **18 -> 19.4** | `rgb(251,251,254)` |
| Space buttons `#zen-workspaces-button` children | the Space's emoji / selectable icon (current Space full colour, others dot or dim) | 15-16 in a 17 x 32 button | |
| Create `#zen-create-new-button` | `plus.svg` | **20 x 20** glyph in a 32 x 32 button (6 pt padding) | `rgb(251,251,254)` |
| Compact-mode / collapsed sidebar | `#zen-expand-sidebar-button`: `expand-sidebar.svg` | 16-17 | |

### 1.6 Context menus (native) and the app menu

- **Native context menus and menu-bar menus have no Zen icons**: they are macOS
  `NSMenu`s (`widget.macos.native-context-menus`), text only (see main-17..20).
- **App menu** (`#appMenu-popup`, opened with `PanelUI.show()`): rows 309 x 34,
  icon **18 x 18** (zoom / fullscreen row buttons 16 x 16), text 13.75 pt,
  colour `rgb(251,251,254)`, op 1, sub-menu arrow `arrow-right.svg`:

| Row | Icon |
|---|---|
| New Tab | `new-tab-image.svg` |
| New Window / New Blank Window | `window.svg` |
| New Private Window | `private-window.svg` |
| History > | `history.svg` |
| Bookmarks > | `bookmark-star-on-tray.svg` |
| Downloads | `downloads.svg` |
| Passwords | `passwords.svg` |
| Extensions and Themes | `extension.svg` |
| Print... | `print.svg` |
| Save Page As... | `save.svg` |
| Find in Page... | `search-page.svg` |
| Zoom - / + | `zoom-out.svg` / `plus.svg` (16 x 16) |
| Full screen | `fullscreen.svg` (`fullscreen-exit.svg` when on) |
| Settings | `settings.svg` |
| More Tools > | `menu.svg` |
| Help > | `help.svg` |
| (panel back button) | `arrow-left.svg`; panel headings use `bookmark-star-on-tray`, `history`, `help`, `library`, `extension` |

Other tab/space menu icons (no icon is drawn in the native menus, but these
exist for the in-window panels): `duplicate-tab.svg`, `edit-copy/cut/paste/delete.svg`,
`share.svg`, `send-to-device.svg`, `screenshot.svg`, `print.svg`.

### 1.7 Glance and split view

| Element | Icon | Size |
|---|---|---|
| Glance close `.zen-glance-sidebar-close` | `close.svg` | 32 x 32 round button (44 pt pitch) |
| Glance "open as tab" `.zen-glance-sidebar-open` | `fullscreen.svg` | 32 x 32 |
| Glance split `.zen-glance-sidebar-split` | `split.svg` | 32 x 32 |
| Split pane unsplit `.zen-tab-unsplit-button` | `fullscreen.svg` | 16 |
| Split pane rearrange `.zen-tab-rearrange-button` | `drag-indicator.svg` | 16 |
| Split menu | `split.svg` (browser.xhtml) | 16 |

(Glyph size inside the 32 x 32 Glance buttons ~16-17; colour = `currentColor`
of the button, white-ish on the dark glass.)

### 1.8 Compact mode

Toggle `#zen-toggle-compact-mode` = `sidebar.svg` (see 1.1); the sidebar-hidden
toast and the background-tab toast are text with the Zen toast chrome (close
`close.svg` in `.zen-toast button`, 16, `rgb(251,251,254)`).

### 1.9 Downloads

| Element | Icon | Size |
|---|---|---|
| `#downloads-button` / `#downloads-indicator-anchor` / `#appMenu-downloads-button` | `downloads.svg` | 17 (toolbar) / 18 (menu) |
| Library downloads rail | `library/library-downloads-sprite.svg` | 28 x 28 tab icon (sprite frame 18, scaled) |
| Download animation box (arc end) | `library/library.svg` (background, right) | |
| Download list in the sidebar | `#zen-library-download-list` strip above the bottom bar (10 high) | |

### 1.10 Library

Rail items (`.zen-library-tab-icon-image`, 28 x 28 container, sprite scaled to
fit): `library-space-routing-sprite.svg` (Routing), `library-media-sprite.svg`
(Media), `library-downloads-sprite.svg` (Downloads), `library-boosts-sprite.svg`
(Boosts), `library-spaces-sprite.svg` (Spaces), `library-history-sprite.svg`
(History, the selected one is tinted `color(srgb .19 .25 .33)`). The sprite
frames switch (idle -> selected -> hover glow) by `background-position`. Header
controls: back `back.svg`, edit `paintbrush-fill.svg`, `heart-circle-fill.svg`
(28 x 28), search `search-glass.svg` (16 x 16, op **.5**), filter
`circle-bars-filter.svg` (16 x 16, op **.8**), delete `trash.svg` and restore
`u-turn-to-left.svg` (34 x 34). `library-tile.svg` is the mask for space cards.

### 1.11 Settings (about:preferences)

The category list does **not** use `zen-icons`: it uses Firefox's category icons
(`preferences/category-general.svg`, `category-privacy-security.svg`,
`category-search.svg`, `category-sync.svg`, `category-accessibility.svg`) and
`chrome://browser/skin/customize.svg` (Look and Feel), `window.svg` (Tab
Management), `quickactions.svg` (Keyboard Shortcuts), `mozapps/.../category-themes.svg`
(Zen Mods), all 16-17 pt, tinted with the text colour. The browser-layout
choices are PNGs `zen-images/layouts/{single-toolbar,multiple-toolbar,collapsed}.png`
(258 x 181.7). (Settings colours come from Firefox's in-content styles.)

### 1.12 Onboarding (welcome)

| Element | Icon | Size | Colour |
|---|---|---|---|
| Intro start button `#zen-welcome-start-button` | `forward.svg` | 100 x 42 button, arrow 17 | white (`fill: white`) on `rgb(62,115,62)` |
| Back `#zen-welcome-back` | `back.svg` (CSS `::before`) | **16 x 16**, gap 5.5 | accent text `oklch(.7 .0888 254.6)` |
| Option radio | CSS ring + dot (no icon) | 22 x 22 | |
| Import list logos | each browser's `brandImage` | in a 34% / 40% tile | brand |
| Search engines | each engine's own icon | 26.8 / 42 | brand |
| Essentials grid | `zen-images/favicons/{obsidian,discord,trello,slack,github,tuta,notion,calendar,figma}.svg` | 35% of the 112 tile (~39 pt) | brand colour on a tinted tile |
| Colours step | the theme picker icons: `sparkles.svg` (auto), `face-sun.svg` (light), `moon-stars.svg` (dark), `plus.svg`, `unpin.svg` (remove dot, `#...color-remove`), `algorithm.svg` (shuffle/harmony), `arrow-left/right.svg` (preset pages) | 30 x 30 buttons, ~17 glyph | `light-dark(rgba(0,0,0,.7), rgba(255,255,255,.9))`; disabled op .5 |

### 1.13 Media controller, permissions, misc (full list in `icons.css`)

Media cards: `media-play/pause/next/previous/mute/unmute.svg`, `close.svg`,
`screen.svg` (focus), `microphone(-blocked)-fill.svg`, `video(-blocked)-fill.svg`,
PiP `global/media/picture-in-picture-open.svg`. Permission popups: `geo`,
`camera`, `microphone`, `screen`, `xr`, `desktop-notification`, `popup`,
`autoplay-media`, `canvas`, `midi`, `persistent-storage` (+ `-blocked` and
`-fill` variants). Boosts: `boost.svg`, `bolt.svg`, `sliders.svg`,
`eyedropper.svg`, `hammer.svg`, `brackets-curly.svg`, `dice.svg`, `block.svg`,
`text-*.svg`. The Space/emoji picker offers the 87 `selectable/*.svg`.

## 2. What Koan needs to do (summary)

1. Keep the icons **monochrome and tinted by the element colour** (no baked colour).
2. Glyph sizes: toolbar 17, favicons/close/identity/plus-row 16, app menu 18, bottom-bar
   "+" 20, Library 18 (x1.08) in a 32 button, Clear 10, Glance buttons 32.
3. Opacities: disabled back/forward .5, "+ New Tab" glyph `.63` alpha colour,
   "Clear" .5 on hover, search/filter in Library .5/.8, library selected tinted.
4. **Do not copy the 158 Nucleo SVGs** (section 0): get a Nucleo licence or build an
   equivalent set; the names and metrics here are enough to match icon for icon.
5. Raw data: `~/work/koan/.cache/zen-ref/raw/icons-scan-*.json`,
   `~/work/koan/.cache/zen-src-1.24b/chrome/browser/skin/classic/browser/zen-icons/`.

## 3. How firm each part is

- **Measured live** (sizes, colours, opacities): 1.1 top row, 1.3 tab rows, the "+
  New Tab" row, the Space "Clear" button, the bottom bar, the app menu, the
  floating URL bar rows, the Library rail and header, the Settings layout images.
- **Read from `icons.css` / stylesheets, not measured on screen**: the permission
  and media icons, Boosts, page-action icons in the pill, Space reorder, Glance and
  split pane icons (the 32 x 32 Glance buttons are measured; their inner glyph size
  is assumed ~16-17), audio overlays, `essential-add/remove`.
- **Inferred**: light-mode colours (taken from the matching tokens, not sampled
  per icon). (The icon provenance and licences are NOT inferred any more: they
  are from Zen's repo, see section 0.)
- **Not found in `zen-icons`**: Settings category icons (Firefox's own set),
  favicons and brand marks.
