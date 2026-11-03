# Zen on macOS: what it looks like and does (reference spec for Koan)

Captured 2026-11-03 from a real **Zen 1.24b** (`Zen.app`, build ID
20261029123729, Gecko-based) in a fresh, separate profile, on a MacBook Air
(M4), macOS 27.0.1, display 1470 x 956 pt @2x (2940 x 1912 px). Everything
below is **measured** (DOM geometry from the running app, or pixels from
native-resolution screenshots) or **read from Zen's source** (`omni.ja`,
same version); each number says which. Screenshots are in
[screenshots/zen-mac/](screenshots/zen-mac/) (downscaled; Zen's UI only).
The WebUI plan for the welcome is in [zen-onboarding-port.md](zen-onboarding-port.md).

Units: **pt** = CSS px on this Mac (devicePixelRatio 2). "Koan now" compares
with the 2026-11-03 Mac review (`~/work/koan/.cache/mac-review/2026-11-03/`:
`preview-A`, `strip`, `S-stills`, `bugs.md`) and the user's photos of Koan
on the Mac; those Koan builds pre-date #90, #91, #97, #98, so "now" may
already be fixed by an open PR; re-check against the next Mac pass.

## 0. Method and caveats

- Zen was launched `-no-remote -marionette -profile <fresh dir>` with
  `MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1`; the user's own Zen (a different
  process and profile) was never touched. UI state was driven through
  Marionette (DOM/JS in the browser's chrome context), screenshots with
  `screencapture -l<windowid>`; a window recording at 60 fps covers the
  onboarding. **No import was run**: "NO, start fresh." was chosen on the
  import page (checked in the profile afterwards: no migration prefs, no
  logins, no imported bookmarks).
- A robot emoji appears at the left of the URL pill in the screenshots: that is
  Firefox's *remote-control (Marionette) indicator*, not part of Zen's UI.
  The purple "sharing" pill at the top-left of recording-time stills was
  cropped (macOS's own screen-recording indicator).
- Windows in the screenshots are inactive (the capture tool doesn't
  activate them), so traffic lights are grey ("grey out inactive windows").
- **Not captured yet** (screen slot ran out; marked *PENDING* below):
  native fullscreen and the notch, the *look* of native context menus and
  menu-bar menus (their **contents** are listed in section 11 from the DOM),
  the compact-mode hover reveal (the reveal itself, done by command, is in
  section 5), Glance (didn't open from a scripted call; behaviour read from
  source), and a second small-window pass.

## 1. Onboarding (welcome)

Zen 1.24b. Full source analysis, API mapping and the port plan:
[zen-onboarding-port.md](zen-onboarding-port.md).

### 1.1 Flow, in order

| # | Screen | Screenshot |
|---|---|---|
| 0 | **Intro**: "Welcome to / a calmer internet", arrow button, over a looping green gradient video | `onboarding-01-intro.jpg`, `onboarding-01b-intro-title-anim.jpg` |
| 1 | **Import**: "Bring your stuff along." | `onboarding-02-import.jpg` |
| 2 | **Colours**: "Your space, your colors." (the theme picker panel overlaid on the right pane) | `onboarding-03-colors.jpg` |
| 3 | **Search engine**: "Pick your search engine." Google, Bing, DuckDuckGo | `onboarding-04-search.jpg` |
| (4) | **Block ads**: "Block ads and trackers?"; skipped when the uBlock Origin add-on isn't available (skipped in this capture) | not captured |
| 5 | **Essentials**: "Choose the apps you use most." + "Skip for now" | `onboarding-05-essentials.jpg`, `...05b-essentials-selected.jpg` |
| 6 | **Default browser**: "Make Zen your default?" | `onboarding-06-default-browser.jpg` |
| 7 | **Finish**: "All set? Let's get rolling!" + "Dive in!" (right pane is empty) | `onboarding-07-finish.jpg` |
| 8 | **After**: the card fades out, the browser fades in | `onboarding-08-finish-transition.jpg`, `main-01-small-window-after-onboarding.jpg` |

Strings (en-US, `zen-welcome.ftl`): title lines "Welcome to" / "a calmer
internet"; Back; Next; Skip for now; Dive in!; import: "Bring your stuff
along." / "Pick a browser to import your bookmarks, history and passwords
from and pick up right where you left off." / **NO**, start fresh.;
colours: "Your space, your colors." / "Personalize your browser by giving each
space its own unique color identity."; search: "Pick your search engine." /
"Choose your default search engine. You can always change it later!";
block-ads: "Block ads and trackers?" / "{brand} can install uBlock Origin for
you, so pages load cleaner and faster. You can remove it any time." /
**YES**, block ads for me. / **NO**, show me everything.; essentials:
"Choose the apps you use most." / "Select your favorite apps to keep them
handy in your sidebar."; default browser: "Make {brand} your default?" /
"Links from other apps will open in {brand}. You can always change this
later." / **YES**, make {brand} default. / **NO**, still exploring my
options.; finish: "All set? Let's get rolling!" / "You're all set up and
ready to go. Click the button below to start browsing with {brand}."
(`<strong>` = the bold word in the option.)

### 1.2 The window

- First run opens a **1200 x 720 pt window, centred** on the screen
  (`min(1200, availWidth) x min(720, availHeight)`), size **locked** until the
  welcome ends (`:root[zen-welcome-stage]` also sets min 875 x 560). Window
  corners are the OS rounded corners; the whole surface drags the window
  (`-moz-window-dragging: drag`), the card does not.
- A fresh Zen window only paints once the app is activated (a Marionette
  quirk, not Zen design).
- The window has **no visible traffic lights or toolbars** during the
  welcome: every child of `#browser` is `display:none` except the
  background and toast container.
- Root font size on macOS is 11 px, so Zen's `rem`s are 11 pt.

### 1.3 Intro (measured, window 1200 x 720)

- Background: `welcome-background.mp4` (15.4 MB), a slowly drifting
  light-green / white gradient, `object-fit: cover`, looping, muted.
  The look is the *only* thing on screen besides the title and the button.
- Title: 2 lines, centred, **57.2 pt / 700**, line-height 60.07, letter-spacing
  -1.716 (-0.03em), white, `max-width: 50%`, translated -5% up; box
  411.9 x 120.1 at (394, 270.4).
- Button `#zen-welcome-start-button`: **100 x 42** at (552.8, 439.9),
  radius 8, fill `rgb(62,115,62)` (#3e733e), white arrow icon, padding 1em.
  Hover: scale 1.03 + brightness 1.1 (0.15 s ease-out); active: scale 1.
- **Animation (source; confirmed in the 60 fps recording):** each character
  `opacity 0->1, y 50->0` as a spring (`bounce 0.3, visualDuration 0.45 s`),
  stagger **0.035 s per character, start delay 0.8 s** (the whole title is in
  by ~1.3 s after paint); then the button fades/rises in
  (`opacity 0->1, y 20->0, blur 2px->0`, delay 0.1 s, spring stiffness 300,
  damping 20, mass 1.8), settled by ~1.6 s. The page then waits for a click.
  Exit on click: characters and button `opacity 1->0, y 0->-14`, 0.3 s
  `easeIn`, stagger 0.012 s, then the card appears.

### 1.4 The card (all pages; measured on import page)

| Item | Value |
|---|---|
| Card (`#zen-welcome-pages`) | **1032 x 590.4** at (84, 64.8) = 86% x 82% of the window, radius **15.4** (1.4rem), `rgba(43,43,48,0.93)`, shadow `0 3px 8px rgba(0,0,0,.05)`; below 1000 px wide: full-bleed, no radius |
| Left column | 392.1 wide (38%), fill `rgb(28,28,30)` (#1c1c1e), padding **44 / 41.8 / 39.6** (4rem 3.8rem 3.6rem), shadow `4px 0 24px rgba(0,0,0,.05)` |
| Back button | at (125.8, 108.8), 12.1 pt / 600, accent colour, 16 px arrow, gap 5.5; hidden (visibility) on the first page |
| Title `h1` | **33 pt / 700**, line-height 36.3, letter-spacing -0.66, white, margin-bottom 26.4 (2.4rem) |
| Description `p` | **13.75 pt / 400**, line-height 22 (1.6), white at 60%, margin-bottom 13.2 |
| Primary button | **308.5 x 42.4**, radius **12**, fill `oklch(0.55 0.0888 254.6)`, white 13.2 pt / 600, padding 13.2 / 11; sits at the bottom of the column (y 573.2); Skip for now (Essentials only) is a second, plain button below it (Next moves up to y 519.8); gap 11 between buttons |
| Right pane | 639.8 wide, `overflow: hidden`, with the film-grain PNG overlay (`opacity .3`, `mix-blend: overlay`); each page is centred with gap 27.5 and padding 33 / 44 |
| Option row (`.zen-welcome-option`) | width 100%, max-width **330**, radius **20**, border 1 `rgba(255,255,255,.16)`, fill `rgb(28,28,30)`, shadow `0 2px 10px rgba(0,0,0,.05)`, text **14.3 pt**, padding 19.8 / 20.9, gap 11, hover scale 1.03 (spring `0.25 s cubic-bezier(.34,1.56,.64,1)`) |
| Radio | 22.7 circle, border 1 `rgba(255,255,255,.5)`; checked: border = text colour, 3 pt inner dot (`inset: 3px`) |
| **Selected** option | scale 1.03, border and text colour `oklch(0.7 0.0888 254.6)` (Zen's accent, a periwinkle blue in the default theme), radio dot filled |

Per page:

- **Import**: option rows are 271.9 x **102** (`min-height 9rem`), radius **24**,
  gap 22, left padding 27.5, font 14.3 / 600. List: "NO, start fresh." first
  (selected by default), then **one card per installed browser** sorted by last
  used (Chrome, Safari, Brave on this Mac), each with its logo in a tile on the
  right (40% of the row, tile fill `rgb(51,51,56)` with a blurred, 3x-scaled
  copy of the logo behind it at 20%). A browser with more than one profile
  expands (grid-rows 0fr -> 1fr, 0.3 s `cubic-bezier(.34,1.2,.64,1)`) to a list
  of profile rows with a 22 px check circle. The list scrolls with a 6rem
  fade mask top and bottom. **Never click Next with a browser selected on a
  shared machine: it starts the import.**
- **Colours**: the right pane is empty except for the **theme-picker panel**
  (360 x 524.4 at (616.1, 97.8)): a dot-pad canvas with "Click to add a
  color", three buttons (auto / light / dark) at the top, +/-/ shuffle at the
  bottom of the pad, a row of 9 swatches with arrows, a waveform slider
  (opacity/grain) and a knob. The background video has been faded out
  (0.6 s `easeOut`) when this page appears, so the window background turns
  flat dark grey (see screenshot). Picking colours tints the real sidebar
  behind it live.
- **Search**: rows 271.9 x **85**, radius 20, icon tile 84.8 x 82.9 (34% of the
  row) with a 26.8 x 26.8 logo (radius 10); order Google, Bing, DuckDuckGo
  (Wikipedia/eBay are filtered out). Selecting one changes the default **at
  once** (not on Next).
- **Block ads** (only when offered): two text options (330 x 63.6).
- **Essentials**: a mock browser window (**527.9 x 557.4** at (588.1, 97.8),
  top-left radius 26.4, fill `rgb(35,35,38)`, shadow `0 8px 40px rgba(0,0,0,.12)`)
  pinned to the pane's bottom-right; inside, a mock sidebar (422.3 wide,
  `#1c1c1e`, padding 28.6 / 26.4, gap 17.6) with three 22 px grey dots (traffic
  lights, gap 11), a **3 x 3 grid** (369.5 square, gap 16.5) of 112.2 x 112.2
  tiles (radius 22, 3 px transparent border, fill `rgb(51,51,56)`, the app's
  logo at 35%), then two empty 49.5-high rounded bars (radius 17.6; the mock
  tabs; they are cut off by the card's bottom edge on purpose). Apps: Obsidian,
  Discord, Trello, Slack, GitHub, Tuta, Notion, Google Calendar, Figma. A
  tile toggles on click: scale 1.04, fill = 22% of the app's brand colour over the
  surface, border = 60% of the brand colour.
- **Default browser**: two text options, "YES, make Zen default." / "NO, still
  exploring my options." (NO is selected by default).
- **Finish**: nothing on the right (flat), primary button "Dive in!".

### 1.5 Page transitions (source; timings from `ZenWelcome.mjs`)

Next/Back: the old text block and content fade out in **0.12 s `easeIn`**
(text also slides `-80 * direction` px); the new text block enters with each
child (h1, p...) `opacity 0->1, x 120*direction -> 0`, spring
(`bounce 0.4, visualDuration 0.3 s`), **stagger 0.03 s**; the new right pane
fades in **0.25 s `easeOut`**; buttons are swapped without animation. The
card itself fades in 0.45 s when the first page starts. Option hover/selection
spring 0.25 s as above.

### 1.6 After finishing

`#zen-welcome` fades out (Motion default ~0.3 s), the window size lock is
removed, then **everything in the browser fades in** (`opacity 0->1`, ~0.3 s)
and the main UI is in `main-01-small-window-after-onboarding.jpg`: the
window **stays 1200 x 720** (it does not maximise); the sidebar shows the chosen
Essentials, a folder named "zen basics" holding the tab that was open, and
"+ New Tab". The first page of the content area is empty (the new-tab page
is not shown; the URL bar is not focused).

Koan now: green gradient behind a card, old Yes/No import, letters in tiles
instead of logos, blue Next, a macOS "preferences corrupt" alert on first
launch, the Chromium toolbar flashing, the browser visible behind the overlay
(`bugs.md` #44; photos of the user's runs show the same). The welcome must
become the WebUI of [zen-onboarding-port.md](zen-onboarding-port.md).

## 2. Main window layout (maximised 1470 x 923, below a 33 pt menu bar)

Screenshots: `main-02-maximized-expanded.png` (full), `main-01-...jpg` (small).

### 2.1 The top area: there is no toolbar

**Zen's default (single toolbar, `zen.view.use-single-toolbar = true`) has
no top toolbar and no tab strip along the top of the window.** The top 8 pt of
the window is just the frame colour; the page card starts at **y = 8**. Back /
forward / reload, the sidebar toggle and the traffic lights are in the
**sidebar's top row**, and the URL bar is a pill under that row (section 4).
Settings > Look and Feel offers three layouts: **Only Sidebar** (default),
**Sidebar and Top Toolbar** (a normal toolbar over the page), **Collapsed
Sidebar**; plus "Show New Tab Button on Tab List", "Move the new tab button
to the top" (both on).

### 2.2 Geometry (DOM, pt)

| Element | x, y | w x h | Notes |
|---|---|---|---|
| Window | 0, 33 | 1470 x 923 | maximised (zoom) below the menu bar; window corner radius is the OS's |
| Sidebar (`#navigator-toolbox`) | 0, 0 | **230** x 923 | `zen.view.sidebar-expanded`; width is saved, user-resizable, max `zen.view.sidebar-expanded.max-width` = 500; padding 0 6 6 6 |
| Splitter | 224, 8 | **6** x 907 | drag handle; shows an accent pill (radius 4) when hovered/dragged (opacity 0 at rest) |
| Content card (`#tabbrowser-tabbox`) | **230, 8** | **1232 x 907** | right and bottom gap **8** (`--zen-element-separation`); no gap between the sidebar and the card (the 6 pt splitter sits in the sidebar) |
| Card radius | | | **7 pt** (`.browserSidebarContainer`, `--zen-border-radius` = 11, `--zen-native-inner-radius` = 9.1); soft shadow `rgba(0,0,0,.24) 0 3px 8px` (`--zen-big-shadow`) |
| Sidebar top row (`#zen-sidebar-top-buttons`) | 6, 3 | 218 x 38 | margin 3 0; contains, left to right: native window buttons in a **70 x 20** box at (6, 12); then sidebar toggle, back, forward, reload |
| URL bar (`#urlbar`) | 7, 43.5 | 216 x 38 (pill 36 high) | radius 4 at rest |
| Essentials / tab list | 6, 81... | 218 wide | see section 3 |
| Bottom bar (`#zen-sidebar-foot-buttons`) | 6, 885 | 218 x 32 | library button 32 x 32 at left; "+" (new space) at right; gap 4 |

`--zen-toolbar-height` is 42 on Mac (Zen's "Mac metrics": 42 px toolbar,
32 px Space buttons).

### 2.3 Traffic lights (pixels, native 2940 x 1846 capture)

- Circles **14 pt in diameter**, centres at x = 19 / 42 / 65 (pitch **23 pt**),
  y = **22**; left edge of the first at x = 12. They are the OS's own buttons
  placed in the 70 x 20 box above (macOS 27 draws them larger than 12 pt).
- Always visible in the sidebar's top row (`zen.view.hide-window-controls`
  is for other platforms), grey when the window is inactive.
- Sidebar toggle at x 139.5-153, back/forward/reload follow to x ~223.
- In the small (1200 x 720) window the layout is identical.

Koan now: lights centred at x ~ 24 / 45 / 66 (pitch 21), y ~ 18.5; the sidebar
toggle overlapped the red light in earlier builds (`bugs.md` #91);
`docs/ZEN_PARITY.md` says Zen hides the lights until the pointer reaches the
top edge: **that is wrong for Zen on Mac**, which shows them always.

### 2.4 Typography and colour (computed)

| Role | Font | Colour |
|---|---|---|
| Tab / folder / sidebar text | system (`-apple-system`, SF), **13.75 pt**, 400 (folder label **600**) | `rgba(204,212,221,.9)` |
| Space name | **13 / 600**, `color(srgb .8 .83 .87 / .63)` | |
| URL pill text | 13.75 / 500, white at 59% | |
| Page UI (settings) | system 13 | |
| Default theme | primary colour `rgb(2,41,84)`; sidebar over a translucent blue-grey background; text `rgb(251,251,254)` | |

## 3. Sidebar content

Screenshots: `main-02`, `main-07-pinned-and-folder.png`.

| Element | Size / position (pt) | Detail |
|---|---|---|
| **Essentials** | tiles **51.5 x 50** (4 per row; x = 6, 61.5, 117, 172.5; y 87) | gap 4; tile bg radius **14** (inner 47.5 x 46), 16 x 16 favicon centred (radius 4) over a 13.5% tint `oklch(0.866 0.015 251 / .135)`; max 12 per Space; shown in every Space (`zen.workspaces.separate-essentials` true) |
| **Space header** (`.zen-current-workspace-indicator`) | full width x **44** at y 139 | icon + name "Space", 13 / 600, dim; padding 17 / 12 / 17 / 17, gap 9 |
| **Folder** (`zen-folder`) | 218 x **40** | label 13.75 / **600**, 32 pt left padding (icon + chevron), children indented 14; collapsed shows its tab icons; sub-folders up to 5 |
| **Pinned tab** | 218 x 40 | same as a normal tab; pinned section above the "+ New Tab" row |
| **Tab** | **218 x 40** (background 214 x 36 with 2 pt margin, radius **14**), pitch 40 | 16 x 16 favicon at x = tab + 11.3 (icon margin 0 10 0 3.3), label 13.75 / 400 ellipsised, close button 24 high appears on hover (right); selected tab: lighter fill + `--zen-active-tab-scale` 0.98 feel; padding 0 8 |
| **"+ New Tab"** | 214 x **36**, radius 14, at (8, 407) | dim text, "+" icon, sits between pinned and normal tabs (`zen.view.show-newtab-button-top`) |
| Folder / tab drag | | drag to reorder, onto a folder after 20% threshold, to the space dots after 500 ms |
| **Bottom bar** | 218 x 32 at y 885 | Library button (left, 32 x 32), Space dots (middle, only with 2+ Spaces), "+" create-space (right) |

Hover on the sidebar shows a **Clear** control at the top of the unpinned
list (`zen.view.show-clear-tabs-button`).

Koan now (preview-A `01-first-window`): a bolder "Space" heading with a
divider, "New Tab" row above one tab, no Essentials shown, bottom controls
tiny; the URL pill has no site icon/placeholder. Re-check against current
builds before filing.

## 4. URL bar

Screenshots: `main-02` (pill), `main-03-urlbar-focused-floating.png`,
`main-04-cmd-t-floating-urlbar.png`.

- At rest it is the **pill in the sidebar** (216 x 38, radius 4) showing the
  site's domain only (`zen.urlbar.show-domain-only-in-sidebar`), 13.75 / 500,
  with a copy-URL button on hover (`single-toolbar-show-copy-url`). The
  site's favicon sits at its left.
- `zen.urlbar.behavior = "floating-on-type"` (default): clicking the pill,
  **Cmd+L** or **Cmd+T** opens a **floating** card in the middle of the
  window: **750 pt wide**, x = 360 (centred), top y = **295** (about 1/3
  down), 15 pt radius, 16.5 pt text; the first row is the input with the
  search glyph; below it suggestions (Wikipedia, YouTube, Reddit, Add-ons for
  Firefox as defaults) with site icons, ~218 pt of results. Esc / click
  outside closes it. Cmd+T does **not** open a tab: the new-tab page is the
  URL bar (`zen.urlbar.replace-newtab`).
- `zen.urlbar.open-on-startup` true: the bar is open on a fresh window.

Koan now: Cmd+T / URL typing opens a similar floating bar (preview-A `13`),
but the card is ~450 pt wide on a 1470 pt window in the capture, the
suggestions use Google rows, and the new-tab page leaks Google's NTP
(`bugs.md` #96).

## 5. Compact mode

Screenshots: `main-05-compact-mode-on.png`, `main-06-compact-sidebar-shown.png`.

- Toggle: **Cmd+S** on macOS (key `accel`+S, verified in the running app;
  Ctrl+S elsewhere); **Cmd+Opt+S** shows/hides the floating sidebar.
- Compact on: the sidebar leaves the layout. Measured: toolbox at
  x = **-241** (246 wide incl. its float margin), page card spans
  **x 8 to 1462** (8 pt left gap equal to the right one); the splitter becomes
  a hot-zone at x -10. Window attribute `zen-compact-mode=true`.
- Reveal: moving the pointer to the left edge shows the sidebar **floating**
  over the page: toolbox x = **-4**, 246 wide, **10 pt** float margin
  (`--zen-compact-float`) on top/bottom/left, rounded and shadowed (the
  page is not pushed). It hides again `zen.view.compact.sidebar-keep-hover.duration`
  = **150 ms** after the pointer leaves; the toolbar (if shown) hides 1000 ms
  after hover (`toolbar-hide-after-hover.duration`). The edge hot-zone
  extends `outside-window-edge-offset` 200 pt (horizontal) / 100 pt
  (vertical) outside the window.
- Animation: `translate 0.25 s` with a spring-shaped `linear()` easing
  (reaches 0.50 at 20% of the time, overshoots to 1.007 and settles; i.e.
  `--zen-compact-mode-func`).
- Also: toast "Sidebar hidden! Use Ctrl+S ..." when the sidebar is dragged
  closed; background-tab-opened toast in compact mode
  (`show-background-tab-toast`).
- *PENDING: capture of the pointer-driven reveal and the toast.*

## 6. Spaces

Screenshots: `main-10-create-space.png`.

- The first Space is "Space" (unnamed default). `Spaces` menu: Create Space,
  Theme..., Change Name..., Change Icon..., Next / Previous Space.
- **Create Space** replaces the tab area with a form: "Create a Space" title,
  "Spaces are used to organize your tabs and sessions.", a name field with
  an icon button, "Theme..." , **Create Space** (disabled until named) and
  **Cancel**; the bottom bar shows the dots and "+".
- Switching: **Cmd+Opt+Right / Left** (next / previous), **Ctrl+1 .. Ctrl+0**
  (Space 1..10), or trackpad swipe (`zen.workspaces.swipe-actions`, edge actions), wrap-around,
  **250 ms** switch animation (`switch-animation-duration`).
- Essentials are shared across Spaces; each Space has its own theme colour
  and icon.

## 7. Glance, split view, pinned and folders (behaviour)

- **Glance** (from source; scripted call didn't open one so no capture):
  activation `zen.glance.activation-method` = **alt** (Opt-click a link);
  the page opens in a card at **80% width x 100% height of the content area**
  with the page behind it scaled to **0.97**, animation **350 ms**
  (`zen.glance.animation-duration`), radius = `--zen-native-inner-radius`;
  the left of the card shows small circular buttons (close, open in new tab,
  split) 56 pt column; Esc or clicking outside closes it.
- **Split view** (`main-08-split-view.png`): two panes of **612.5 x 907** at
  x = 230 and 850.5: **8 pt gap**, 7 pt radius each, active pane outlined;
  tabs joined in the sidebar by a split group; grid / vertical (`vsep`) /
  horizontal (`hsep`) / unsplit.
- **Pinned tabs and folders** (`main-07`): pinned tabs live above "+ New
  Tab", folders collapse/expand with chevron; a folder created with
  `createFolder` holds the tab(s) (label "Reading" in the capture).

## 8. Library

Screenshot: `main-09-library.png`. `cmd_zenToggleLibrary` (button at the
bottom-left of the sidebar). Opens as a **418 pt wide panel over the left
of the window** with a **left rail** of section icons (Routing, Media,
Downloads, Boosts, Spaces, History; History is selected) and the History list
(search box "Search History..." with a Filter button; items grouped "Today");
open animation `--zen-library-open-duration` **0.3 s**
`cubic-bezier(.22,1,.36,1)`; entry stagger easing
`linear(0, .34 12%, .7 24%, .93 38%, 1.03 55%, 1)`; radius 12; Spaces tab card
width 242, gap 24.

## 9. Settings (`about:preferences`)

Screenshots: `main-11`, `main-12`, `main-13`. Opens as a normal tab. Left list:
**Look and Feel, Tab Management, Keyboard Shortcuts, Zen Mods, Account and
sync, Search, Privacy and security, Passwords and autofill, Appearance,
Downloads, Tabs and browsing** | **Accessibility, Languages, Permissions and
data, About Zen** | **Extensions and themes**; a "Find in Settings" search
box on top with a focus ring. Look and Feel: Browser Layout (three preview
images: Only Sidebar, Sidebar and Top Toolbar, Collapsed Sidebar), Vertical
Tabs checkboxes, Theme Settings, compact view options. Tab Management:
Workspaces and Pinned Tabs groups.

## 10. Private window

Screenshot: `main-14-private-window.png` (**Cmd+Shift+P**; title "Zen
Browser — Private Browsing"). Same layout as a normal window, **darker
(near-black `#111`) sidebar and card**, the Space header reads "**Incognito**"
with a mask icon (no workspace name), only one empty "New Tab", **no
Essentials, no top toolbar, no banner**; the new tab is the URL bar. There is
nothing at the top of the window except the sidebar's own top row.

Koan now (user's photo): the stock Chromium incognito window: a normal Chromium
toolbar across the top, "You've gone incognito" page. Should use the same
sidebar layout as normal windows with the Incognito label.

## 11. Menus (contents, from the DOM; macOS menu bar)

**Menu bar** (after the native Zen menu): **File** (New Tab, New Window,
**New Blank Window**, New Smart Window, New Classic Window, New Private
Window, Open File..., Close Tab, Close Window | Save Page As..., Print... |
Import From Another Browser... | Work Offline), **Edit** (Undo ... Select All,
Find in Page..., Find Again), **View** (Appearance > Automatic / Light / Dark;
Toolbars >; Sidebar >; Collapse Pinned Tabs | Zoom >; Page Style >; Repair
Text Encoding | Enter Full Screen), **Spaces** (Create Space, Theme...,
Change Name..., Change Icon... | Next Space, Previous Space), **History**
(Show All History, Clear Recent History..., Restore Previous Session, Search
History, Search Tabs, Recently Closed Tabs), **Bookmarks**, **Profiles**
(New Profile, Manage Profiles), **Tools** (Downloads, Extensions and Themes,
Sign In, Browser Tools, Page Info), **Window** (Minimize ...), **Help**.

**Tab context menu** (visible subset): New Tab to Right, Move to Folder >,
Add Split View..., | Reload Tab, Mute/Play Tab, **Add to Essentials** / Remove
from Essentials ("n / 12" badge), | Change Label..., Change Icon..., | **Pin
Tab**, **Unload Tab**, Duplicate Tab, | Bookmark Tab..., Move Tab >, Select
All Tabs, | Send to Device >, | **Close Tab**, Close Duplicate Tabs, Close
Multiple Tabs >, Reopen Closed Tab, | **Add Route for Domain**, Hide Toolbars.
Pinned tabs add Edit Pinned URL >, Reset Pinned Tab.

**Sidebar (toolbar) context menu**: **Enable compact mode**, | New Tab, **New
Folder**, Live Folder >, | **Theme picker**, Set Profile >, | Reload/Bookmark/
Select All Tabs, Undo Close Tab, | Customize Sidebar, Turn on Vertical Tabs,
| Tabs on the right, Customize..., Full-screen autohide.

*PENDING: how the native menus look (they are NSMenus; Zen uses default
context menus: `zen.view.context-menu.refresh` false).*

### Zen's own shortcuts on macOS (read from the running app's `<key>` elements)

| Action | Keys |
|---|---|
| Toggle compact mode | **Cmd+S** |
| Show/hide the floating sidebar | **Cmd+Opt+S** |
| Next / previous Space | **Cmd+Opt+Right / Left** |
| Switch to Space 1..10 | **Ctrl+1 .. Ctrl+0** |
| Open the Library | **Opt+Shift+L** |
| Split: grid / vertical / horizontal / unsplit | **Cmd+Opt+G / V / H / U** |
| New empty split | Cmd+Shift+* |
| Close all unpinned tabs | Cmd+Shift+K |
| Pin / unpin tab | Cmd+Shift+D |
| Copy URL / as Markdown | Cmd+Shift+C / Cmd+Opt+Shift+C |
| Expand Glance into a tab | Cmd+O |
| New Blank (unsynced) window | Cmd+Shift+N |
| Open the URL bar / new tab | Cmd+L / Cmd+T; private window Cmd+Shift+P |

Koan's `koan_shortcut_table.cc` binds compact mode to **Ctrl+S** (`kCtrl`, the
physical Control key on a Mac), but **Cmd+S is Zen's key on macOS** (`accel`),
so that one is a Mac difference (and the 2026-11-03 review script used
Ctrl+S). Koan's Space and Library bindings already match. Check against #90.

## 12. Fullscreen and the notch

*PENDING.* From source: `zen.view.borderless-fullscreen` = true; Zen enters
native macOS fullscreen (`macnativefullscreen=true`); the toolbars hide and
reappear on pointer at the top edge (`full-screen-autohide`). The notch is on
this Mac (menu bar is 33 pt tall), so a captured fullscreen window shows the
notch strip; measure the safe-area inset in a second pass.

## 13. Animation table (source; all verified in the code, onboarding also in video)

| Thing | Duration / easing |
|---|---|
| Intro title characters | spring `bounce .3, visualDuration .45 s`, stagger .035 s, start delay .8 s |
| Intro button | spring stiffness 300, damping 20, mass 1.8, delay .1 s |
| Welcome page text enter / exit | spring `bounce .4, visualDuration .3 s`, stagger .03 s / `.12 s easeIn` |
| Welcome content fade | `.25 s easeOut`; card fade-in `.45 s` |
| Welcome video fade-out | `.6 s easeOut` |
| Option hover/select | `.25 s cubic-bezier(.34,1.56,.64,1)` (overshoot) |
| Compact mode slide | `.25 s` spring-shaped `linear()` |
| Hidden toolbar fade | `.15 s ease-in-out` (`--zen-hidden-toolbar-transition`) |
| Glance | 350 ms (pref) |
| Space switch | 250 ms (pref) |
| Library open | `.3 s cubic-bezier(.22,1,.36,1)` |
| Download arc | 1000 ms (`zen.downloads.download-animation-duration`) |
| Folder / tab indent | `margin-inline-start .1s ease-in-out` |

## 14. Where Koan differs today, ranked

(Evidence is the 2026-11-03 Mac pass and the user's photos; confirm against
the current build before filing issues.)

1. **Welcome**: not Zen's (green card, old import options, letter tiles,
   blue buttons, stock toolbar flashing, preference-corrupt alert). Replace
   with the WebUI port ([zen-onboarding-port.md](zen-onboarding-port.md)).
2. **Traffic lights**: Zen keeps them in the sidebar top row at centres 19/42/65, y 22
   (pitch 23). Koan: ~24/45/66, y 18.5; ZEN_PARITY.md's "hidden until hover"
   row is wrong for Mac.
3. **Private window**: Zen = sidebar layout, "Incognito" label, darker, no
   toolbar. Koan = stock Chromium incognito window.
4. **New tab page / Cmd+T**: Zen = the floating bar (750 wide, y 295),
   nothing else. Koan leaks Google's NTP in places.
5. **Essentials / Space header / tab rows**: sizes in section 3 (51.5 x 50
   tiles, 44-high Space header, 218 x 40 tab rows, 36-high New Tab row,
   14 pt radius backgrounds). Compare Koan's rows pixel by pixel after #97.
6. **Compact mode**: 150 ms keep-hover, 250 ms spring slide, 10 pt float
   margin, hot-zone 200 / 100 pt outside the window (#98).
7. **Theme picker**: Zen's gradient panel vs Koan's single colour (#41).
8. Fullscreen/notch and native menus: capture before judging.
