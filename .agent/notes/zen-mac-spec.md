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
- The second capture pass (sections 2A, 5, 7, 11, 12) reproduced the states in
  the **user's two photos of their real Zen** (windowed and fullscreen). The
  photos are *not* committed (they show the user's tabs and mail); everything
  below was re-measured in the fresh profile with the same content shape
  (7 Essentials, a "General" Space with an emoji, ~10 tabs, 3 Spaces).
- Still not captured: the pill's host-and-port text in a URL like
  `host:port` (Zen shows the host only when `show-domain-only-in-sidebar`
  is on; a port is part of the host in the photo), and the page-action icons
  that appear inside the pill while it is hovered or focused (seen in the
  fullscreen photo).

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

## 2A. Ground truth: the user's windowed and fullscreen Zen, and the matching capture

Reference photos (user's own Zen, not committed): windowed and fullscreen.
Reproduced in a fresh profile as `ref-01-windowed-fresh-profile.png` and
`ref-02-fullscreen-notch-strip.png`; geometry in
`~/work/koan/.cache/zen-ref/raw/measure-ref-{windowed,fullscreen}.json`.
Both states use the **default single-toolbar layout**: **no toolbar or URL bar
across the top**, the whole chrome is the sidebar.

**Windowed (maximised 1470 x 923; sidebar 262 wide as in the photo)**

| Item | Value (pt) |
|---|---|
| Row 1 (`#zen-sidebar-top-buttons`) | x 6, **y 3, h 38** (44 with its 3 pt margins); items left to right: traffic lights (box 70 x 20 at x 6, y 12), **sidebar toggle** (right after the lights, x ~76-94), then **back / forward / reload right-aligned** as 31 x 29 buttons at x = sidebar - 99 / -68 / -37 (163, 194, 225 here), y 7.5 |
| Row 2: URL pill (`.urlbar-background`) | **246 x 36** at (8, 44.5), radius **14**, fill = the same 13.5% tint as the tiles (`oklch(0.866 0.015 251 / .135)`: slightly lighter than the sidebar), no border or shadow; shows the host (and port) only, 13.75 / 500, favicon at the left |
| Essentials grid | container 262 x 112 at y 81, padding 6, **gap 4**; **4 per row** here (7 tiles = 4 + 3), tile **59.5 x 50** (inner background 55.5 x 46, 2 pt margin), tile radius **14**, favicon 16 x 16 centred; row pitch 54 (y 87, 141) |
| Space header | full width x **44** at y 193 (after the grid), emoji (16 pt, x 17) + name "General" (13 / 600, dim, x 42) |
| Divider row (`.pinned-tabs-container-separator`) | 250 x **22** at y 237: a **1 pt line** `rgba(255,255,255,.1)` from x 15 to ~214, and the **"Clear" button** (36.9 x 18.5, 10 pt / 500, right-aligned) which appears while the pointer is over the sidebar (hidden otherwise: absent in the windowed photo, present in the fullscreen one) |
| "+ New Tab" row | 246 x **36** at (8, 261), radius 14, dim text, "+" icon; it comes **first**, above the tabs (`show-newtab-button-top`) |
| Tab rows | **218 x 40** at x 6 (inner pill 214 x 36, radius **14**), pitch 40: favicon + title, no close button until hovered; **selected** = a lighter rounded pill (`color(srgb .776 .81 .848 / .22)`, i.e. 22% light blue-grey) |
| Bottom bar | **32 high** (y 885-917, 6 pt above the window bottom), padding 6: Library button 32 x 32 at x 6 (the app/menu icon), **Space switcher** in the middle (one 17 x 32 button per Space starting at x 105.5: the current Space shows its emoji, the others show a dot until hovered), **"+" 32 x 32** at the right (x 224) |
| Content card | at (**sidebar width**, **8**), right/bottom gap **8**, radius **7**, no gap on its left side (the 6 pt splitter belongs to the sidebar); its top edge is **above** the traffic-lights row (card y 8, lights centre y 22). In the photo it looks level with the lights only because of the camera angle |

At the default 230 pt sidebar the same 7 Essentials wrap **3 + 3 + 1**: the
grid rule is `repeat(auto-fit, minmax(max(23.7%, tile-min), 1fr))` with a fixed
4 pt gap, so four tiles fit only when the container is >= ~231 pt wide (the
user's sidebar is wider than the default). The count also changes the rule
(`data-hack-type` in `ZenSpaceManager.mjs`): exactly 6 or 9 tiles use 30% columns
(3 per row); an even count below 8 (2, 4) uses paired columns; 5 uses 25% / min
48; everything else is 23.7% (4 per row).

**Fullscreen (native macOS fullscreen)**

| Item | Value |
|---|---|
| Window | the **notch strip stays black above the window** (33 pt, the OS safe area); the Zen window sits below it at (0, 33) with the same 1470 x 923 size; nothing is drawn under the notch, the sidebar starts below it |
| Traffic lights | **gone** (`.titlebar-buttonbox-container` has no size) |
| Row 1 | starts with the **sidebar toggle at the far left** (x 6), then back / forward / reload right-aligned exactly as windowed (x 163 / 194 / 225); row height unchanged (38) |
| Everything else | unchanged: URL pill, Essentials, Space header, divider, tabs, bottom bar, and the **content card keeps its 8 pt margins and 7 pt radius** |
| Menu bar / toolbars | the menu bar auto-hides (reappears at the top edge); `zen.view.borderless-fullscreen` = true |
| Attributes | `inFullscreen=true sizemode=fullscreen` |

Koan to match: no top toolbar; row 1 = lights + toggle + right-aligned
back/forward/reload; 36-high pill at radius 14; the Essentials rule above;
44-high Space header; divider with Clear; 36-high "+ New Tab" first; 40-pt
rows; 32-high bottom bar with the Space switcher in the middle.

## 3. Sidebar content

Screenshots: `main-02`, `main-07-pinned-and-folder.png`.

| Element | Size / position (pt) | Detail |
|---|---|---|
| **Essentials** | tiles **51.5 x 50** for 4 tiles in a 230 sidebar (x = 6, 61.5, 117, 172.5; y 87); with 7 tiles and a 262 sidebar **59.5 x 50**, 4 + 3 (see 2A for the column rule) | gap 4; tile bg radius **14** (inner 47.5 x 46), 16 x 16 favicon centred (radius 4) over a 13.5% tint `oklch(0.866 0.015 251 / .135)`; max 12 per Space; shown in every Space (`zen.workspaces.separate-essentials` true) |
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

## 4. URL bar and New Tab (the floating panel)

Screenshots: `main-02` (the pill at rest), `main-21-cmd-t-panel.png` (Cmd+T),
`main-22-cmd-l-panel.png` (Cmd+L), `main-23-cmd-t-panel-native-crop.png`
(native pixels), `main-03` / `main-04` (earlier takes). The robot glyph in the
panel's first row is Marionette's remote-control indicator; in a normal
window it is the site/identity icon. Measured in the running app (DOM
geometry and computed styles, `~/work/koan/.cache/zen-ref/raw/urlbar-*.json`),
a 60 fps window recording, and real key events (`postToPid`), in a 1470 x 923
window. Source: `zen-omnibox.css`, `ZenUrlbarController.mjs`, `ZenUIManager.mjs`.

### 4.1 The pill at rest (sidebar)

246 x 36 at (8, 44.5) in a 262 sidebar (sidebar width - 16 wide), radius **14**, the
13.5% tint fill; text 13.75 pt / 500, the host only
(`zen.urlbar.show-domain-only-in-sidebar`), favicon at the left, copy-URL
button on hover (`single-toolbar-show-copy-url`). While the floating panel is
open the pill is **blank** (`pageproxystate=invalid`; no text, no icon).

### 4.2 The floating panel (same for Cmd+T and Cmd+L)

| Property | Value |
|---|---|
| Position | **centred in the window** (not in the page card): x = (window width - width) / 2 = 360, **top = window height / 2 - 333/2 = 295**: Zen centres it as if it were always 333 pt tall (`FLOATING_URLBAR_MIN_HEIGHT`), so its top stays fixed whatever the result count |
| Width | `min(window width / 1.5, 750)` = **750** here (`FLOATING_URLBAR_MAX_WIDTH`); min-width `min(90%, 62rem)` |
| Height | **62** input row + results: 279.6 with 4 results (the list is capped at **244** pt: `#urlbar-results { max-height: 244px }`); hugs its content |
| Radius | **15** (both the container and its background) |
| Fill | dark: `rgb(24,24,24)`; light: white. **Opaque, no blur** by default (`backdrop-filter: none`); only with `zen.theme.acrylic-urlbar` on does it become 92% / 93% alpha with `blur(40px)` |
| Edge | `0.5 pt` outline `rgb(75,75,75)` (dark) / `rgba(0,0,0,.2)` (light) |
| Shadow | `0 30px 140px -15px` at **60%** black (dark) / **80%** (light): a long soft shadow, mostly below the panel |
| z-order | above the page, the sidebar and the splitter (z-index 1000) |
| Input row | 62 pt high, padding 12 / 14, a 38-high input box starting at x +81 (identity icon + search glyph before it), text **16.5 pt / 500** (`1.5em` of the 11 pt root on macOS), `rgb(251,251,254)`, placeholder **"Search..."** |
| Results box | x +12 (726 wide), top +62 (the first row starts 11 pt below the input row, 10.6 pt of padding), side padding 1.4 |
| Row (`.urlbarView-row`) | **49 pt high** (45 min + 2 + 2 border), 728.8 wide, radius **8**, text colour white at 70%; hover = 5% tint; **selected** = fill `color-mix(accent-button-bg, transparent 40%)` with white text |
| Row content | favicon **24 x 24** (radius 4, padding 4, i.e. a 16 pt icon in a tile) at x +8; title **14 pt / 500**; then " — " and the URL in **14 pt / 500 `rgb(170,170,170)`** (e.g. "YouTube - youtube.com"); open tabs end with a **"Switch to Tab"** label: 14 pt / **600**, in a pill 100 x 23, radius 8, padding 4 / 8, opacity .7, followed by a keycap showing the Tab key |
| Default results (empty input) | the open-tab match(es) first (as `switchtab` rows), then top sites: here Wikipedia (switch to tab), YouTube, Reddit, Add-ons for Firefox; as you type they become matching open tabs, history, bookmarks and search suggestions |

### 4.3 Open and close animation

**None.** In a 60 fps window recording the panel region changes from the page to
the finished panel **in one frame** (<= 17 ms) on both Cmd+T and Cmd+L, and
disappears in one frame on Esc. There is no fade, scale or slide, and no
sidebar animation. (Source agrees: the only urlbar animations are the
search-mode switch, `scale 1 -> .98 -> 1` over 0.25 s, and a one-second
`zen-urlbar-searchmode` gradient sweep when a search engine chip is chosen.)

### 4.4 Cmd+T (New Tab) versus Cmd+L

| | **Cmd+T** | **Cmd+L** |
|---|---|---|
| Panel | identical size, position and look | identical |
| What it is | a **New Tab session** (`zen-newtab`): nothing is created yet | edits the current tab's address |
| Input | **empty**, placeholder "Search..." | the current URL, **fully selected** (`https://www.debian.org/`, 0-23) |
| Sidebar | the **"+ New Tab" row is highlighted** (`in-urlbar` on every new-tab button); the **current tab loses its selected pill** (`_visuallySelected = false`); the URL pill is blank | the current tab stays selected; the pill is blank; "+ New Tab" is not highlighted |
| Results | open tabs + top sites (a fresh-tab list) | the same default list while the text is selected; typing filters it |
| Enter on a result | opens it in a **new tab** (tab count +1, the new tab is selected); a "Switch to Tab" result is meant to switch to the existing tab (not tested; the Enter I sent opened google.com in a new tab, tab count 20 -> 21) | navigates the **current tab** |
| Esc | closes the panel in **one press, even after typing; no tab is created** (tab count unchanged, the previous tab's pill returns) | closes the panel and **reverts** the pill to the URL |
| Clicking outside | closes it; per `docs/ZEN_PARITY.md` the click also reaches what was clicked (**not re-tested here**) | same (not re-tested) |
| Pressed again | **Cmd+T while the panel is open closes it** (toggle, no tab; verified, tab count unchanged) | not tested |
| Memory | the typed text is kept for **45 s** (`zen.urlbar.wait-to-clear` = 45000): Cmd+T within 45 s reopens with the same text and results; afterwards it is empty | no memory (always the current URL) |
| Pref | `zen.urlbar.replace-newtab` = true (default): with it off, Cmd+T makes a real blank tab | `zen.urlbar.behavior` = `floating-on-type`: the bar floats only while editing (others: always-floating, normal) |
| New window | `zen.urlbar.open-on-startup` = true: a window opens with the panel up | |

Clicking the sidebar **pill with the mouse does NOT float the bar**: Zen's own test
`test_Click_Shoudnt_FLoat_Urlbar` (`src/zen/tests/urlbar/browser_floating_urlbar.js`) asserts
`zen-floating-urlbar` is absent after a click; only keyboard focus (Cmd+L, Cmd+T)
floats in the default `floating-on-type` mode (corrected from the earlier text;
see `zen-behaviour/cmd-t-cmd-l-session.md`). The "+ New Tab" row is highlighted
while a New Tab session is open.

### 4.5 Koan now vs Zen

Koan (preview-A `13`): a floating bar that is ~450 pt wide on a 1470 pt window
(Zen 750), suggestion rows with Google search first, "AI Mode" chip, and the
new-tab page leaking Google's NTP (`bugs.md` #96). For #95: build exactly
4.2 (750 / 295 / radius 15 / opaque dark #181818 / the long shadow / 0.5 pt
outline / 49-pt rows / "Switch to Tab" pill), no animation, and 4.4's
Cmd+T-versus-Cmd+L rules (empty input and highlighted "+ New Tab" row,
Enter = new tab, one Esc closes with no tab created, 45 s text memory,
Cmd+T toggles).

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
- **Captured** (`main-15-compact-hover-reveal.png`): with the pointer warped to
  the left edge (x 2) nothing has appeared **0.18 s** later; **~0.7 s** later the
  sidebar is a **floating rounded panel over the page** (246 wide, ~10 pt inset,
  darker and more opaque than the docked sidebar, "Clear" and the copy-URL
  button visible because the pointer is over it); moving the pointer away hides
  it again in **< 0.5 s**. The page does not move. (Timings include the ~0.3 s
  capture latency; the source's `0.25 s` slide and `150 ms` keep-hover apply.)

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

- **Glance** (captured with a real Option-click on a link:
  `main-16-glance.png`): measured: the **page behind** is scaled to **0.97**
  (1164 x 879.8 from 1200 x 907, still radius 7, same shadow) and dimmed;
  the glance card is centred at ~80% of the content width and the full
  height, same 7 pt radius; **three 32 x 32 round buttons** sit to the right
  of the card at x 1354 (just outside it), y 35 / 79 / 123 (pitch 44): close,
  expand to a full tab, split view. Opening grows the card from the clicked
  link (the first frame is a dark card with a "Transferring data..." status).
  The glance tab appears in the sidebar as a child of its parent tab.
  From source: activation `zen.glance.activation-method` = **alt** (Opt-click a link);
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

**How they look** (all native macOS NSMenus with the system's translucent
glass, 12-13 pt text, no custom styling since `zen.view.context-menu.refresh`
is false): `main-17-tab-context-menu.png` (tab menu: *New Tab Below*, Move to
Folder >, Add Split View... | Reload Tab, Mute Tab, **Add to Essentials** with
a "7 / 12" badge | Change Label..., Change Icon... | Pin Tab, Unload Tab,
Duplicate Tab | Bookmark Tab..., Move Tab >, Share >, Open in New Container Tab
>, Select All Tabs | Send to Mobile > ... with a scroll arrow at the bottom),
`main-18-menubar-zen.png` (About Zen, Settings... Cmd+, Services >, Hide Zen
Cmd+H, Hide Others Opt+Cmd+H, Show All, Quit Zen Cmd+Q), `main-19-menubar-view.png`
(Appearance >, Toolbars >, Sidebar >, Collapse Pinned Tabs, Zoom >, Page Style
>, Repair Text Encoding, Enter Full Screen Ctrl+Cmd+F), `main-20-menubar-spaces.png`
(Create Space, Theme..., Change Name..., Change Icon... | Next Space
Opt+Cmd+Right, Previous Space Opt+Cmd+Left | one row per Space with its icon and
Ctrl+1, Ctrl+2...). The menu bar reads: Apple, Zen, File, Edit, View, Spaces,
History, Bookmarks, Tools, Window, Help.

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

Captured (`ref-02-fullscreen-notch-strip.png`, details in section 2A). Native
macOS fullscreen: the menu bar and notch area stay a **black 33 pt strip**
above a Zen window of the normal 1470 x 923 size, so nothing is cropped by the
notch and nothing sits under it. Traffic lights disappear; the sidebar toggle
becomes the first item of row 1; the page card keeps its 8 pt margins and
rounded corners. Reaching the top edge reveals the menu bar. `Enter Full
Screen` is Ctrl+Cmd+F.

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
8. Fullscreen: no traffic lights, toggle first in row 1, black notch strip above
   an unchanged 923-high window, card margins kept (2A, 12).
9. Native menus (Zen / View / Spaces / tab context): see section 11 for the
   exact contents and shortcuts, and the screenshots.

## 15. Zen's own look: glass and the theme picker (supersedes any shadcn look)

Captured later on 2026-11-03 for the glass (#103/#105) and theme picker (#41)
threads. Zen 1.24b, macOS 27.0.1, same Mac. To see translucency I put my own
bright, high-detail striped window (`backdrop`: 70 pt colour stripes, a 6 pt
black/white checker band, 90 pt black text) behind a smaller Zen window and
captured regions that contained only those two windows (every capture was
checked against the on-screen window list before and after and discarded
otherwise). **The window must be ACTIVE**: an inactive Zen is flat (15.6).
Raw data: `~/work/koan/.cache/zen-ref/raw/` (`prefs-glass-theme.json`,
`glass-*.json`, `picker-open.json`); shipped defaults:
`~/work/koan/.cache/zen-src-1.24b/defaults/preferences/firefox.js`.

### 15.1 The model: native vibrancy under a tint layer under the UI

1. The macOS window is **non-opaque** and its content has a native
   `NSVisualEffectView` (the XUL binary calls `setMaterial:`,
   `setBlendingMode:`, `setState:`). `#main-window` (and `#browser`,
   `#zen-main-app-wrapper`, `#navigator-toolbox`) are `background: transparent`
   on macOS (`@media (-moz-platform: macos)` in `zen-theme.css`).
2. One full-window **theme layer** (`#zen-browser-background`, with a
   `::before` / `::after` pair that cross-fades between Spaces) paints the
   Space's theme *with alpha* over the vibrancy. It covers the whole window:
   the sidebar, the top row and the 8 pt ring around the page card.
3. The UI sits on top: transparent sidebar, tabs, tinted pill/tiles; the
   **page card** (web content, radius 7) is above the layer.

### 15.2 Prefs (defaults, from Zen's shipped `firefox.js`; none edited unless stated)

| Pref | Default | Notes |
|---|---|---|
| `zen.widget.macos.window-vibrancy` | `true` | master switch; `false` = flat opaque window (measured) |
| `zen.widget.macos.window-material` | `1` (int) | read **live** (changing it re-materials the window at once); values below |
| `widget.macos.sidebar-blend-mode.behind-window` | `true` | the legacy switch; `ZenUIMigration` v7 sets `window-vibrancy=false` if a user had this `false` |
| `widget.macos.titlebar-blend-mode.behind-window` | `false` | |
| `widget.transparent-windows` | `true` | |
| `zen.widget.mac.mono-window-controls` | `true` | grey (mono) traffic lights when inactive |
| `zen.widget.macos.override-system-swipe-gestures` | `true` | |
| `zen.theme.acrylic-sidebar` | `false` | extra blur for the floating compact sidebar |
| `zen.theme.acrylic-urlbar` | `false` | blur + 92% alpha for the floating URL bar |
| `zen.view.window.scheme` | `2` | 0 dark, 1 light, 2 auto (follows the system) |
| `zen.view.grey-out-inactive-windows` | `true` | inactive windows are greyed |
| `zen.theme.dark-mode-bias` | `0.3` | how readily a colour is treated as "dark" |
| `zen.theme.use-system-colors` | `false`, `zen.theme.accent-color` = `AccentColor` | |
| `zen.theme.border-radius` / `content-element-separation` | `-1` (auto) / `8` | |
| `zen.theme.gradient.show-custom-colors` | `false` | hides the custom-colour list in the picker |
| `layout.css.prefers-reduced-transparency.enabled` | `false` | web-content flag only; chrome CSS still honours the media feature (15.6) |
| `browser.theme.toolbar-theme` | `2`, set to `0` at startup (user value) | |

All 79 related prefs with default, current value and "user set" flag are in
`prefs-glass-theme.json`. Only `browser.theme.toolbar-theme` and
`zen.view.compact.enable-at-startup` show a user value in this profile
(both written by Zen itself).

### 15.3 Window material (`zen.widget.macos.window-material`)

Measured as the mean RGB of a fixed sidebar patch over my stripe backdrop,
dark scheme, no theme (higher = more of the backdrop shows):

| Value | Mean RGB | | Value | Mean RGB |
|---|---|---|---|---|
| **1 (default)** | **44, 61, 78** (most see-through) | | 5, 7, 8-12, 0 | 37, 43, 50 (all the same: a fallback) |
| 2 | 41, 57, 72 | | 6 | 35, 38, 42 (darkest) |
| 3 | 39, 52, 65 | | `window-vibrancy=false` | 28, 29, 29 (flat, opaque) |
| 4 | 37, 48, 58 | | | |

So the pref is an index into a short native material list, not a raw
`NSVisualEffectMaterial`; the default is the lightest of them.

### 15.4 Which surfaces are translucent

| Surface | Glass? |
|---|---|
| Window background, sidebar, top row, the 8 pt ring around the card | **Yes**: vibrancy + the theme layer's alpha |
| Page card with a **real page** | No (opaque) |
| Page card with a **blank / new-tab page** | **Yes**: a transparent document shows the window glass (`glass-06-active-blank-page-card.jpg`, `picker-03-...jpg`) |
| URL pill, Essentials tiles, selected tab pill | only a tint: 13.5% light (`oklch(.866 .015 251 / .135)`), 22% for the selected tab |
| Floating compact sidebar | glass; blur boost with `acrylic-sidebar` |
| Theme picker panel | frosted glass (blurred page behind it, `picker-01-open.jpg`) |
| Cmd+T / Cmd+L floating URL panel | **opaque** `rgb(24,24,24)` (white in light); with `acrylic-urlbar`: `rgba(20,20,20,.92)` + `backdrop-filter: url(zen-backdrop-filters.svg#underlay-dark) blur(40px)`; same 15 pt radius, 0.5 pt outline, long shadow (section 4) |
| Context menus, menu-bar menus | native `NSMenu` (system glass; `widget.macos.native-context-menus` = true); Zen adds no styling (`main-17`, `main-18..20`) |
| Dialog/arrow panels (`--arrowpanel-background`) | opaque `rgb(31,31,31)` / `rgb(244,244,244)` |

### 15.5 Blur strength and the tint over it

- **Blur**: the system material, heavy and not tunable from CSS or prefs
  (apart from the material choice). In `glass-01-...jpg` the 70 pt stripes
  with 1 px edges turn into soft colour bands with transitions tens of points wide,
  and the 90 pt black backdrop text disappears completely.
- **Default theme** (no colour): the layer is `rgba(0,0,0,0.4)` in dark and
  fully transparent in light; so dark = darkened blur, light = bright, washed
  blur (`glass-01`, `glass-03`).
- **One colour** (picker default opacity **0.5**): the layer is
  `rgba(c*0.92 + white*0.08, 0.5)` (the 0.92 is `blendWithWhiteOverlay`:
  `min(1, op + 0.3 + 0.6 * (1 - op - 0.3))` at `op = .5`), e.g. the dot
  `rgb(173,6,154)` gives `rgba(180,26,162,0.5)`: the blur stays visible through
  it, tinted (`glass-02`, `glass-04`).
- **Several colours**: stacked gradients, every stop at alpha = opacity: for a
  3-dot analogous preset `linear-gradient(-5deg, rgba(245,218,221,.5) 10%,
  transparent 80%), radial-gradient(circle at 95% 0%, rgba(229,245,218,.5) 0%,
  transparent 75%), radial-gradient(circle at 0% 0%, rgba(245,238,218,.5) ...)`
  (rotation -30 deg in the algorithm; `picker-02`, `picker-03`).
- **Opacity slider range**: min **0.30**, max **0.80**, step 0.001, default 0.5
 . Texture (grain, a PNG
  overlay) 0..1, default 0 (`--zen-grainy-background-opacity`).
- A dark colour keeps light text even in the Light scheme: the dark/light UI
  decision follows the colour (`zen-should-be-dark-mode`, `glass-04`).

### 15.6 Active vs inactive, and "Reduce transparency"

- **Inactive** (another app has focus): the whole window goes **flat**: no
  blur, no tint, opaque dark grey, traffic lights grey (`glass-05-inactive-flat.jpg`
  vs `glass-06`). This is the native vibrancy following the window state plus
  `zen.view.grey-out-inactive-windows` (`:-moz-window-inactive` ->
  `InactiveCaption`). Screenshots of an inactive window say nothing about the glass.
- **Reduce transparency**: Zen's CSS only gates `acrylic-sidebar` and
  `acrylic-urlbar` on `(prefers-reduced-transparency: reduce)`. Emulated with
  `ui.prefersReducedTransparency = 1` (a profile pref, not the system
  setting): the acrylic floating URL panel fell back to opaque
  `rgb(24,24,24)` with no filter, verified. The window vibrancy is the
  system's `NSVisualEffectView`, which macOS itself makes opaque under the
  system's Reduce Transparency; I did **not** flip that system setting (a
  system change) and this Mac has it off. No Zen code path overrides it.

### 15.7 The theme picker

**Open**: sidebar context menu "Theme picker", Spaces > Theme..., or Edit Theme
in the Create-Space form (`cmd_zenOpenZenThemePicker`). It is a native
popover (`PanelMultiView.openPopup`, position `topright topleft`, x -10) that
opens at the **top of the window overlapping the sidebar's right edge**;
`zen.theme` changes are written to the active Space.

**Geometry** (`picker-open.json`, pt; panel at (269,141) in an 1100 x 720 window):

| Control | Size / position |
|---|---|
| Panel | **380 x 514**, padding 10, frosted glass |
| Scheme buttons (top) | three **30 x 30**, radius 5.2, at x 393 / 444 / 495 (gap 21), y 167: **auto** (sparkle), **light** (sun), **dark** (moon); the current one has a 10% white fill; they write `zen.view.window.scheme` 2 / 1 / 0 |
| Dotted field | **358 x 358**, radius 8 (panel radius - 4), fill 3% white, a 1 px dot grid every 6 px, hint "Click to add a color" centred |
| Dots | primary **38 px** with a 6 px white border and a dark ring, drag it to change the theme; others `--icon-size-medium` with a 3 px white border; scale 1.05 on hover, 1.2 while dragging; **max 3** |
| Field actions (bottom, centred) | three **30 x 30** buttons: **+** add a dot, **-** remove, **shuffle** (cycle harmony) |
| Presets | row of **26 x 26** circles (1 px 10% shadow), **41** presets in pages of 8 with left/right arrows (page 282 wide); hover scale 1.05, press 0.95 (0.1 s); 1-dot presets are flat colours (e.g. `rgb(244,239,223)` cream, `rgb(240,184,205)` pink, `rgb(233,195,227)` lilac), 3-dot ones a gradient swatch |
| Opacity slider | a **sine-wave** track 250 wide whose wave is cut off at the value, pill thumb (hover scale 1.1 over 0.5 s), 0.30 - 0.80, default 0.5 |
| Texture knob | **66 x 66** round dial at the right of the slider (grain 0..1) |

**Where the panel opens (measured, `picker-position.json`):** all three entry
points (the Space's more-actions menu "Theme...", the sidebar context menu
"Theme picker", the Spaces menu "Theme...") run `cmd_zenOpenZenThemePicker`
-> `openThemePicker`, which anchors the panel to **`#TabsToolbar`** (the
sidebar's tab area, x 6 to sidebar-6, y 81 to window bottom-6), not to the
toolbox or the URL bar, with position `topright topleft`, x -10, y 0 (y -160
only from the Create-Space form's Edit Theme button). In window coordinates the
panel's **left edge is sidebar width + 7** (sidebar 262 -> x 269, 230 -> 237)
and it is **vertically centred on `#TabsToolbar`** (top = 81 + (tab-area height
- 514) / 2: 141 in a 720 pt high window, 242 in a 923 pt one); there is no
popover tail (`hidepopovertail=true`). So it opens beside the sidebar over the
page card's left edge, not below the URL bar.

**Colour harmonies** (the shuffle button / adding dots): `complementary`
[180], `singleAnalogous` [310], `splitComplementary` [150, 210], `analogous`
[50, 310], `triadic` [120, 240], `floating` [] (dots move independently).
(Source, `calculateCompliments`: dragging the primary dot moves the others by the
harmony's angles, unless it is `floating`; not exercised on screen.) Presets store `data-position` (x, y in the 358
field), `data-lightness` (80-90 for the pastels) and `data-algo`.

**How a Space's gradient appears**: the colours become the whole-window layer
(15.1), so the **sidebar, the top row and the 8 pt ring** show the gradient
over the blur, with the page card above it; a separate toolbar element
(`#zen-toolbar-background`) exists for the multi-toolbar layout. Each Space keeps its own theme; switching Spaces cross-fades
the layers.

**Animations**: edits are applied **live and instantly**: in a window recording,
adding a dot, applying a preset and switching to light each landed in a
**single frame** (both layers have `transition: 0s`). Only a **Space switch**
cross-fades (`--zen-background-opacity`, `zen.workspaces.switch-animation-duration`
= 250 ms). The panel's own open/close is the native popover's (not captured
separately).

### 15.9 Adjusting the glass: the opacity slider and the grain dial

Both live in the theme picker's bottom row and edit the **active Space's theme**.

| Control | Range | Default | Stored | Effect |
|---|---|---|---|---|
| **Opacity** slider (sine-wave track, 250 pt wide, pill thumb) | `0.30 - 0.80`, step `0.001` | `0.5` | `workspace.theme.opacity` | alpha of the theme colour layer over the (fixed) native blur; a haptic tick at every 0.1 |
| **Grain** dial (66 x 66 pt, a knob on a ring of 16 dots) | 16 steps `k / 16`, `0 .. 0.9375` | `0` (off) | `workspace.theme.texture` | opacity of the tiled `grain-bg.png` above the colour layer (`.zen-browser-grain`), also `--zen-grainy-background-opacity`; `0` hides it; 0.2 s fade |
| Light / dark / auto buttons | 3 | auto (`2`) | **pref** `zen.view.window.scheme` (0 dark, 1 light, 2 auto) | **global**, applies to every Space and window |

**Storage (verified):** `prefs.js` contains no opacity, texture or theme pref:
after setting opacity 0.30 / 0.50 / 0.80 and 0.80 + grain 0.9375 and forcing
`Services.prefs.savePrefFile`, the `zen.*` lines were byte-identical (only
`zen.workspaces.active` is there). The values are in the Space's object
`{type: "gradient", gradientColors: [...], opacity, texture}`, saved to
`zen-sessions.jsonlz4` (plus its backup and `sessionstore-backups`) when the picker
closes; while dragging, the change is applied live and not yet saved.
**It is per Space**: in the test profile "General" held a colour with opacity
0.8 and grain 0.9375 while "Work" and "Play" still had
`{gradientColors: [], opacity: 0.5, texture: 0}`; every new Space starts there.

**Computed layer for one colour `rgb(173,6,154)` (live):**

| Opacity | `#zen-browser-background::after` | Notes |
|---|---|---|
| 0.30 (min) | `rgba(186, 46, 170, 0.30)` | most backdrop and blur showing, pale tint |
| 0.50 (default) | `rgba(180, 26, 162, 0.50)` | |
| 0.80 (max) | `rgba(173, 6, 154, 0.80)` | nearly the solid colour; still 20% see-through |
| 0.80 + grain 0.9375 | same, plus `.zen-browser-grain` at 0.9375 | grain overlay `display: flex` |

The colour is mixed with white on macOS by
`blendedAlpha = min(1, op + 0.30 + 0.60 * (1 - (op + 0.30)))`
(0.84 at 0.30, 0.92 at 0.50, 1.0 from 0.70 up), so a lower opacity is also a
paler colour. The default theme (no colour) ignores the slider in dark
(`rgba(0,0,0,.4)` fixed) and is transparent in light. The **blur itself is not
adjustable** in Zen: the only knobs are the on/off pref
`zen.widget.macos.window-vibrancy` and the material index
`zen.widget.macos.window-material` (15.3).

**Screenshots** (active window over the striped backdrop, one colour
`rgb(173,6,154)`, picker open; `opacity-*.jpg`): `opacity-01-min-0.30`,
`opacity-02-default-0.50`, `opacity-03-max-0.80`,
`opacity-04-max-0.80-grain-0.94`, `opacity-05-default-grain-0.5`. What they
show: at **0.30** the sidebar is almost only the blurred backdrop with a faint
tint, and the slider's wave is **flat** (a straight line) with a small 10 px
thumb; at **0.50** the wave has grown to its full height up to the thumb
(16 px); at **0.80** the wave is complete and the thumb is 25 px (the thumb
width follows the value: 10 / 16 / 25 px); the sidebar is a strong magenta. Grain
shows as fine noise over the colour and also over the picker's field.

**Colours:** the resolved colours for the default theme in dark and light (frame,
card, panel, context menu, floating URL bar, tab rows, tab text, accent) with
the CSS variable each comes from are in
[zen-colors.json](zen-colors.json).

### 15.8 Screenshots (active window over the striped backdrop)

| File | State |
|---|---|
| `glass-01-dark-no-theme-active.jpg` | dark, no theme (Space "General") |
| `glass-02-dark-one-colour-active.jpg` | dark, one colour, default opacity 0.5 |
| `glass-03-light-no-theme-active.jpg` | light, no theme |
| `glass-04-light-one-colour-active.jpg` | light scheme, same one colour |
| `glass-05-inactive-flat.jpg` | inactive: flat, no glass |
| `glass-06-active-blank-page-card.jpg` | active, blank page card also shows the glass |
| `glass-07-urlbar-opaque-default.jpg`, `glass-08-urlbar-acrylic-pref.jpg` | Cmd+T panel default and with `acrylic-urlbar` |
| `picker-01-open.jpg`, `picker-02-three-dots-dark.jpg`, `picker-03-three-dots-closed.jpg` | the picker and a 3-dot gradient |

Caveats: region captures include my test backdrop (not the user's
wallpaper); my Zen was quit once by something else and relaunched
(same profile) between `glass-06` and the picker shots, so tab titles differ;
the user's own Zen lives in a fullscreen Space and re-raises itself, so
capture protocol = verify on-screen windows before and after.

## 16. Small windows

Measured by resizing the window (`window.resizeTo`) with 7 Essentials and ~10 tabs
(`small-window.json`; screenshots `small-01-1200x720.jpg`, `small-02-700x495.jpg`).

| Item | Value |
|---|---|
| **Minimum window** | **500 x 495 pt** (`#main-window` `min-width: 500px`, `min-height: 495px`); a resize below that is clamped |
| First-run window | 1200 x 720 (locked during onboarding; min 875 x 560 while it runs) |
| Layout at 1200 x 720, 1000 x 650, 900 x 600, 800 x 520, 700 x 495 and 500 x 495 | **identical**: row 1 (lights, toggle, back/forward/reload), pill, Essentials, Space header, tabs and the bottom bar keep their sizes; only the **page card** shrinks (`window - sidebar - 8` wide, `window - 16` high) |
| Sidebar width | **independent of the window size**: in this profile 206 pt at every size (the width is a stored user setting, resizable by dragging the splitter; the very first window had 230); it does not collapse or switch to compact mode by itself |
| Row 1 at width 206 | lights box 70 x 20 at x 6; back / forward / reload buttons 31 x 29 at x 107 / 138 / 169 (always sidebar width - 99 / -68 / -37) |
| Essentials at sidebar 206 | container 194 wide: 7 tiles wrap **3 + 3 + 1** (tile 62 x 50, gap 4), because four columns need >= ~231 (see 2A) |
| Card at 500 x 495 | 286 x 479 at (206, 8): still the 8 pt gap on the right and bottom, radius 7 |

So there is no responsive behaviour in Zen's chrome: a narrow window just gives
a narrow page. (Compact mode, `Cmd+S`, is the user's tool for reclaiming width.)
