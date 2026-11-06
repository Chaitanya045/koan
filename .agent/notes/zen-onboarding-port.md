# Porting Zen's welcome to a Chromium WebUI (`chrome://koan-welcome`)

Status: plan, 2026-11-03. Reference: **Zen 1.24b** (macOS arm64, `Zen.app`, build
ID 20261029123729), read from the installed app's `omni.ja` /
`browser/omni.ja`, and watched running in a fresh profile. What it looks like
and measures is in [zen-mac-spec.md](zen-mac-spec.md) (section 1).

Copies of the 1.24b sources (not committed) live in
`~/work/koan/.cache/zen-src-1.24b/`. The older `.cache/zen-src/` is **1.23.3b
and is out of date for the welcome**: 1.24b's import step lists installed
browsers, adds a profile picker, and the page list changed.

## 1. Verdict

**Yes. Reuse it nearly as-is.** Zen's welcome is plain HTML + CSS + JS
(`ZenWelcome.mjs`, 1041 lines; `zen-welcome.css`, 574 lines; 20 Fluent
strings), and everything that draws the screen is ordinary web platform:
flexbox, `light-dark()`, `oklch()`, `color-mix()`, `:has()`, CSS nesting,
`mask-image`, `will-change`. Chromium 158 supports all of it. Only the
*browser-integration* half of the script is Firefox-specific (reading the
import list, search engines, add-ons, pinning tabs, the theme-picker XUL
panel), and that half is exactly the part a WebUI replaces with a Mojo/message
handler anyway.

| Piece | Reuse | Notes |
|---|---|---|
| `zen-welcome.css` | **verbatim**, 8 small substitutions (section 4) | It is written in `rem`; Gecko's macOS root font size is 11px, so set `html { font-size: 11px }` and every size matches. |
| Page structure, `createOption`, `createImportSource`, `nsZenWelcomePages`, `animateInitialStage`, `getWelcomePages` page table | **verbatim**, minus the `commit()` bodies | ~600 of the 1041 lines. |
| The `commit()` / `render()` code that calls Firefox services | **rewrite** as `WelcomeHandler` messages (section 3) | ~250 lines. |
| Theme picker panel (colours step) | **do not port yet**: see section 5 | `ZenGradientGenerator.mjs` is 2137 lines of XUL-panel + canvas code. |
| Strings (`zen-welcome.ftl`) | convert to a JS table (section 6) | 20 strings, `<strong>` markup kept. |
| `motion.min.mjs` (Motion, MIT, 140 KB) | **vendor as-is** | Every animation is `animate()` with a spring (`visualDuration`, `bounce`); the Web Animations API has no equivalent. Add to THIRD_PARTY_NOTICES (MIT). |
| `welcome-background.mp4` (15.4 MB) | **do not copy**, see section 7 | |
| Essentials favicons `zen-images/favicons/*.svg` | check each SVG's origin before copying; otherwise reuse the ones Koan already ships | Brand marks, not Zen's own artwork. |

## 2. What Zen's welcome is (1.24b), in one paragraph

On first run `ZenStartup.#checkForWelcomePage()` sets
`zen.welcome-screen.seen = true` **immediately** (so a crash mid-welcome never
repeats it), loads `ZenWelcome.mjs` into the browser window, hides every
child of `#browser`, resizes the window to `min(1200, avail) x min(720, avail)`
centred and locks it (`min/max-width/height !important`; `:root[zen-welcome-stage]`
also sets `min-width: 875px; min-height: 560px`), then plays the intro on a
looping green gradient **video** that fills the window. The window keeps the
macOS title-bar treatment (rounded corners, no traffic lights visible: the
whole surface is `-moz-window-dragging: drag`, the card `no-drag`). After the
last page it fades `#zen-welcome` out, unlocks the window size, fades the real
browser UI in, pins the chosen Essentials, and puts the selected tab into a
folder called "zen basics".

Pages, in order (page `id` -> title key):

| # | id | Title | Controls | Commit side effect |
|---|---|---|---|---|
| 0 | (intro) | "Welcome to / a calmer internet" | arrow button (`#zen-welcome-start-button`) | none |
| 1 | `import` | Bring your stuff along. | "NO, start fresh." + one card per **installed** browser (Chrome, Safari, Brave...), each with profile rows if it has more than one | `MigrationUtils.showMigrationWizard({migratorKey, profileId})` if a browser is chosen |
| 2 | `colors` | Your space, your colors. | the theme-picker XUL panel, overlaid on the right pane | none (the picker edits the first Space live) |
| 3 | `search` | Pick your search engine. | radio cards (Google, Bing, DuckDuckGo; Wikipedia and eBay filtered out) | `SearchService.setDefault(engine)` **on change**, not on Next |
| 4 | `block-ads` | Block ads and trackers? | YES / NO radios | installs `uBlock0@raymondhill.net` if YES; **page is skipped** when AMO can't offer the add-on (it was skipped in my fresh run) |
| 5 | `essentials` | Choose the apps you use most. | 3x3 grid of 9 apps (Obsidian, Discord, Trello, Slack, GitHub, Tuta, Notion, Google Calendar, Figma) in a mock sidebar; Next + "Skip for now" | adds chosen apps as Essentials (lazy tabs with the bundled favicon); history rows added so they show in the URL bar |
| 6 | `default-browser` | Make Zen your default? | YES / NO radios | `shellSvc.setDefaultBrowser(false)` if YES |
| 7 | `finish` | All set? Let's get rolling! | "Dive in!" | applies Essentials + "zen basics" folder, fade out welcome, fade in browser |

The layout is a fixed 86% x 82% card (radius 1.4rem = 15.4px), left text
column 38% wide, right content pane; below 1000px wide it goes full-bleed.

## 3. Firefox-only APIs it uses, and what each maps to

Collected by grepping the live `ZenWelcome.mjs` (plus `ZenGradientGenerator.mjs`
for the colours step).

| Firefox API in the script | What it does | Chromium / Koan equivalent |
|---|---|---|
| `MozXULElement.parseXULToFragment`, `<html:...>` XUL wrapper | builds the DOM | plain `innerHTML` / `document.createElement`; drop the `html:` prefix |
| `MozXULElement.insertFTLIfNeeded`, `document.l10n.*` (Fluent) | strings | a JS string table or `loadTimeData` (section 6) |
| `document.getElementById("browser").children` hide/show | hides the real UI under the welcome | not needed: the WebUI is its own `WebView`; Views hides/shows the browser view |
| `window.resizeTo/moveTo/focus`, `docShell.treeOwner...nsIAppWindow.rollupAllPopups()` | centre + lock the window at 1200x720 | `views::Widget::SetBounds` + `SetMinimumSize` (+ `SetSize` constraint) from `KoanWelcomeController` before showing; unlock on finish |
| `-moz-window-dragging: drag / no-drag` | window drag handle | `-webkit-app-region: drag / no-drag` (Koan already drags windows from content in `koan_zen_window_drag`; app-region works inside a WebUI frameless host) |
| `-moz-osx-font-smoothing: grayscale` | text AA | `-webkit-font-smoothing: antialiased` |
| `list-style-image: url(chrome://browser/skin/zen-icons/forward.svg)` (XUL button) | arrow icon | inline `<svg>` / CSS mask |
| `window.requestAnimationFrame`, `video.play()` | | unchanged |
| `lazy.SearchService.getVisibleEngines/getDefault/setDefault`, `engine.getIconURL(96)` | search page | `TemplateURLService` (`GetTemplateURLs`, `GetDefaultSearchProvider`, `SetUserSelectedDefaultSearchProvider`) exposed by `WelcomeHandler`; favicons via `chrome://favicon2/` or bundled PNGs for the 3 shown engines |
| `MigrationUtils.availableMigratorKeys`, `getMigrator`, `getSourceProfiles`, `getLastUsedDate`, `brandImage` | list of importable browsers with profiles | `importer::ImporterList` (`DetectSourceProfiles`) -> `[{key, name, icon, profiles:[{id,name}], lastUsed}]` |
| `MigrationUtils.showMigrationWizard(window, {migratorKey, profileId, isStartupMigration})` | runs the import | Chromium's import flow: `ImportDataHandler`/`ExternalProcessImporterHost` (the code behind `chrome://settings/importData`) started from `WelcomeHandler::Import(key, profileId)` after "Next". Show Chromium's progress UI, or just run silently and let the Library show the history. |
| `AddonRepository`, `AddonManager.getInstallForURL`, `CustomizableUI` (unpin the add-on's button) | uBlock Origin install | none in-box. Chromium MV3 + CWS install needs the Web Store consent flow. **Drop the page** (it is already skippable) unless a bundled/side-loaded uBlock Origin Lite is added later (separate issue). |
| `window.getShellService().setDefaultBrowser(false)` | set default browser | `shell_integration::DefaultBrowserWorker` / `SetAsDefaultBrowser()` (the same call `chrome://settings/defaultBrowser` uses; on macOS it shows the system prompt) |
| `PlacesUtils.history.insertMany`, `TabStateCache.update`, `gBrowser.addTrustedTab/setIcon`, `gZenPinnedTabManager.addToEssentials` | create Essentials | `WelcomeHandler::AddEssentials(urls)` -> Koan's pinned-tab controller (`KoanPinnedTabController`, `koan_pinned_tabs.h`; Essentials are Chromium pinned tabs, max 12): create tabs, set the favicon from the bundled SVG, mark Essential |
| `gZenFolders.createFolder(tabs, {label:"zen basics"})` | puts the first tab into a folder | Koan's folder API; the label becomes "koan basics"? **Decision needed** (Zen text is "zen basics"; keep lowercase product word for parity, i.e. "koan basics") |
| `gZenWorkspaces.reorganizeTabsAfterWelcome()` | empties the placeholder tab, selects the first Space | `koan_workspaces.h` equivalent: close the empty NTP tab that the first run created, select Space 1 |
| `gZenUIManager.motion` (lazy `import("chrome://browser/content/zen-vendor/motion.min.mjs")`) | animation lib | `import "./motion.min.mjs"` bundled in the WebUI resources |
| `gZenThemePicker.panel` + `PanelMultiView.openPopup(..., {position:"overlap"})` | colours step (XUL panel anchored on a 1x1 div) | see section 5 |
| `AppConstants.platform == "macosx"` | one offset (-90 vs 20) for the panel anchor | irrelevant once the picker is in-page |
| `ChromeUtils.defineESModuleGetters`, `ChromeUtils.importESModule`, `Services.*`, `Ci`/`Cc` | module loading | not needed in a WebUI; imports become ES modules in the page |
| `chrome://browser/content/zen-images/...`, `zen-videos/...`, `skin/zen-icons/...` | resource URLs | `chrome://koan-welcome/<file>` via `WebUIDataSource` + `embed_resources.py` (same pipeline as `koan_settings`) |

Nothing in the **visual** part needs XUL or `Services`.

## 4. CSS substitutions (the whole list)

All of `zen-welcome.css` is reusable. Changes:

1. `-moz-window-dragging: drag|no-drag` -> `-webkit-app-region: drag|no-drag`.
2. `-moz-osx-font-smoothing: grayscale` -> `-webkit-font-smoothing: antialiased`.
3. `list-style-image: url(chrome://.../forward.svg)` on the start button -> an inline SVG (`fill: white`).
4. `chrome://browser/content/zen-images/grain-bg.png` -> bundled copy (4 KB-ish noise PNG; or regenerate with an SVG `feTurbulence` filter so no third-party asset is needed).
5. `--zen-accent-button-color` is not defined by the file: define it on `#zen-welcome` from the user's accent. Zen's default theme gives `oklch(0.55 0.0887922 254.602)` for the primary button background and `oklch(0.7 0.0887922 254.602)` for the selected-state text/border (measured). In Koan, derive from Koan's Space colour via a CSS variable pushed by the handler; start with those two constants.
6. `light-dark()` needs `color-scheme: light dark` on `:root` and the page to follow the OS. (Zen's welcome rendered dark on a dark-mode Mac; Chromium 158 supports `light-dark()`.)
7. `html { font-size: 11px }` (macOS Gecko default), so `rem` sizes are identical. On other platforms Gecko's default is 16px root but Zen's `--zen-` scale differs; port macOS values only (Koan welcome is macOS-first, #51).
8. `-moz-context-properties: fill` on the back arrow `::before` -> use a `mask` + `background-color: currentColor`.

`.no-squircles`, `.zen-big-accent-button` (a global Zen style: 12px radius,
`padding: 13.2px 11px`, 13.2px / 600) must be copied in from `zen-buttons.css`.
`--zen-squircle-value` / `corner-shape` is a Gecko-only squircle effect
(`cornerShape: squircle`); Chromium 158 has no `corner-shape` shipped, so
use plain `border-radius` (a ~1px visual difference).

## 5. The colours step

Zen's second page overlays its **theme picker**, a XUL `<panel>`
(`#PanelUI-zen-gradient-generator`) driven by `nsZenThemePicker`
(`ZenGradientGenerator.mjs`, 2137 lines): a 360x524 panel with a dot-pad on a
canvas, light/dark/auto toggles, swatch row, opacity dial, and `Services.prefs`
+ observer plumbing for the gradient. It edits the first Space live and the
sidebar behind it changes colour as you pick.

Koan does **not** have gradients (docs/ZEN_PARITY.md: Space colour is one
Material colour, #41), so a literal port would show a dot pad whose output Koan
can't render. Plan:

1. **Phase 1:** the page shows a swatch row (Zen's 9 default swatches) + light/auto/dark
   toggle + the "Click to add a color" empty pad, wired to the existing
   Space-colour setter in `koan_workspaces.h` (one colour). Same position and size as the
   panel (anchor box 360 x 524.4 at x=616.1, y=97.8 of a 1200 x 720 window; measured), same
   labels, so the screen looks identical and the first Space changes colour live.
2. **Phase 2 (only with #41 gradients):** port the canvas dot pad from
   `ZenGradientGenerator.mjs` into the page (it is DOM + canvas math; the
   `Services.prefs` calls become handler calls).

## 6. Strings

`zen-welcome.ftl` has 20 strings. Chromium WebUI has no Fluent. They are the
`kStrings` table in `koan_welcome.js` (same ids, `{ -brand-short-name }` ->
"Koan"; `<strong>` kept and built as an element, not with `innerHTML`, which
Trusted Types blocks). Keep the ids so the next sync of Zen's text is a diff.
They are MPL: the JS file carries the MPL header and a THIRD_PARTY_NOTICES
row.

## 7. The background video

`welcome-background.mp4` is 15.4 MB, a looping green gradient that plays
only on the intro and import pages (`removeVideoBackground()` fades it out
over 0.6s `easeOut` when the colours page renders, then removes it). Options:

- **A (recommended):** replace it with a CSS/Canvas "mesh gradient" animation
  that looks the same (soft green blobs drifting, see keyframes in
  zen-mac-spec.md 1.1). No 15 MB asset, no licence question, no codec.
- B: ship a smaller H.264/VP9 re-encode of Zen's file. Needs the maintainer's
  OK on redistributing the asset (it is Zen's artwork, not MPL code).

## 8. Hosting it as a full-window overlay

`chrome://koan-welcome` is a `WebUIController` registered like
`koan_settings_ui.cc` (`chrome_web_ui_configs.cc.patch` already adds one).
`KoanWelcomeController` replaces today's `koan_welcome.cc` (1522 lines of
Views) and does:

1. On first run (and `--force-first-run` for tests, see `scripts/e2e/welcome.flags`),
   create a `views::WebView` for the URL and add it as the **top** child of
   the `BrowserView`, filling the whole client area, above the sidebar,
   toolbar and Library overlay. A full-window *overlay in the existing
   window* is what the maintainer asked for; Zen instead resizes the window.
   Do both: resize/centre/lock to 1200x720 (min 875x560) like Zen when the
   window hasn't been customised, and size the overlay to the contents.
2. Hide the Chromium toolbar and everything else while it is up (fixes the
   "stock toolbar flashes" and "leaked strip above the card" bugs from the
   2026-11-03 Mac review: a WebView above everything leaves nothing to leak).
3. Set `-webkit-app-region` so the whole surface drags the window.
4. On finish: fade the WebView (0.3s, see timings), unlock the window, remove
   the view, then the handler runs the commits (pin Essentials, create the
   "basics" folder, import) and the real UI fades in.
5. Write the `seen` pref **before** showing the page (same as Zen).

`WelcomeHandler` (message handler, like `koan_settings_handler.cc`):
`getImportSources`, `import(key, profileId)`, `getSearchEngines`, `setDefaultSearchEngine`,
`setSpaceColor`, `setDefaultBrowser`, `addEssentials(urls)`, `finish`.
**Nothing runs until the user presses Next/Dive in on the relevant page**,
except `setDefaultSearchEngine`, which Zen applies on change; do the same.

## 9. Import safety (testing)

Zen's import page enumerates real browsers installed on the machine. When
testing on a shared Mac, **always select "NO, start fresh." before Next**; the
page must never import from the user's real Brave/Chrome/Safari. Do not commit
screenshots showing profile names (the page only shows profile rows after a
browser is selected). In tests, stub `getImportSources` with fixtures.

## 10. Tests and acceptance

- Replace `scripts/e2e/welcome.mjs`'s Views automation with CDP against the
  WebUI page: click `#zen-welcome-start-button`, then the `button.primary` in
  `#zen-welcome-page-sidebar-buttons` through each `[page=...]`, assert the
  `data-*` outcome (Essentials created, search default changed).
- **Pixel/number checks** against the spec: card 1032 x 590.4 at (84, 64.8) in a
  1200 x 720 window; sidebar 392.1 wide, padding 44 / 41.8 / 39.6; title 33px/700,
  letter-spacing -0.66px; option row 85px (search) / 102px (import) high; accent
  button 308.5 x 42.4. The measured JSON for every page is in
  `~/work/koan/.cache/zen-ref/raw/measure-*.json`.
- Animation checks: intro title characters rise 50px with a spring
  (`bounce 0.3, visualDuration 0.45s`, stagger 0.035s, start delay 0.8s);
  page text enters from `120*direction` px (`bounce 0.4, visualDuration 0.3s`,
  stagger 0.03s), exits `-80*direction` px in 0.12s `easeIn`; content fades in
  0.25s `easeOut`; the card fades in 0.45s.

## 11. Order of work and size

| Step | Work | Size |
|---|---|---|
| 1 | WebUI scaffold, resources, MPL headers, THIRD_PARTY row, `html{font-size:11px}`, intro + pages with stubs | ~1 day |
| 2 | `WelcomeHandler`: search engines, default browser, Essentials, folder, finish | ~1 day |
| 3 | Import list + start import (reuse Chromium importer) | ~0.5 day |
| 4 | Colours step phase 1 (swatches wired to the Space colour) | ~0.5 day |
| 5 | Background (CSS mesh), motion library vendoring, window lock, e2e rewrite, delete `koan_welcome.cc` | ~1 day |

## 12. Risks

- **Animation fidelity:** Motion's springs are part of the look. Vendor the
  same library and parameters; don't approximate with CSS easings.
- **Window-size lock:** Chromium has no `max-width/min-height !important`
  equivalent; use the widget's min/max size.
- **`corner-shape` squircles** (Zen) are not available; radii will be a hair
  rounder-looking. Accept.
- **uBlock page:** keep the page out until a real install path exists (it is
  skipped in Zen when unavailable, so the flow stays valid).
- **Licensing:** every ported file gets the MPL header and a row in
  THIRD_PARTY_NOTICES.md; Motion is MIT (add a row); Zen's mp4 and any
  third-party logos are not copied without a licence check.
