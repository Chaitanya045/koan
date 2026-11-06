# Mac steps: Zen's theme picker and per-Space themes (#41, PR #62)

For the Mac loop and the hands-on tester. **DCHECK build**, fresh profile unless a row says otherwise, Zen look (the
default). After **every block**: `grep -E ':FATAL:|Check failed|DCHECK failed' <koan log>` is empty, no new `.ips`, and
Koan is still running. Two DCHECKs matter here: `Spaces:` (the leak detector, `CheckSpaces()`: the window's colours are
only the ones the theme controller set) and `paints a translucent theme on an opaque layer` (the theme controller's
opaque-layer detector).

Compare with Zen 1.24b (zen-ref captures in `.agent/notes/screenshots/zen-mac/`, spec `.agent/notes/zen-mac-spec.md`
§15). Values in *italics* come from Zen's source, not from screenshots.

Ids TT1–TT10 are the themes audit's Mac tests (`.agent/notes/upstream/themes-glass-ntp.md` §1.5). Tests 1, 3 and 4 are
#159's (`scripts/mac/spaces/README.md`).

## A. The picker

| # | Do | Expect |
|---|---|---|
| A1 | Right-click the Space header > **Theme…** | Zen's panel, *380 wide*, beside the sidebar: on macOS *13pt right of the tab list (`#TabsToolbar`), centred on it*, with the popover blur behind it and the macOS menu radius (*10px, 12px on macOS 26*). Same place from the sidebar's context menu > Theme…, the palette's **Open Theme Picker** and the Library's Space card. |
| A2 | Click a swatch on page 1, then the arrows to page 2–5 and a swatch there | The window's gradient changes at once (no transition), the dots move; page arrows slide the pages. |
| A3 | Drag the big dot around the field; right-click a dot; **+** / **−** / harmony | Colours follow the drag (only the background while dragging; mode and accent when the drag ends). Up to 3 dots. Harmony cycles Complementary / Split / Analogous / Triadic / Floating. |
| A4 | Drag the opacity slider end to end | *0.30–0.80, default 0.5*. Opaque window (no glass on develop): the colour blends with the window colour. A haptic tick at every tenth (trackpad). |
| A5 | Drag the grain dial all the way round | *16 steps*, a tick per step, the grain fades over *0.2 s* between strengths. |
| A6 | Scheme buttons Automatic / Light / Dark | The website appearance follows (a page's `prefers-color-scheme`); Settings > Appearance > Mode shows the same value (TT6). |
| A7 | Esc, or click outside | The panel closes and the theme is saved with the Space (reopen: same dots, opacity, grain). |
| A8 | Seed `{"koan":{"theme":{"gradient":{"show_custom_colors":true}}}}` into `Default/Preferences` (Koan not running), open the picker, type `#ff8800` in the hex field, click **+**, drag the slider, click the row's remove button | The hex row appears with `#ff8800`, the slider works with it shown, remove empties it (the picker's `CustomColors` property: `#ff8800`, then empty). |
| A9 | Two Spaces with different themes; switch with the bottom-bar buttons, ⌃⌥←/→, and a two-finger swipe | The gradient cross-fades (*250 ms*, Zen's spring); no flash of the other Space's or the user's colour (TT1). |
| A10 | Quit (⌘Q) and relaunch | Each Space keeps its theme. |
| A11 | Create a Space (+ > Create Space), **Theme…** in the form, pick a preset, create | The new Space has that theme; the Space you were on keeps its own. |

## B. Colours and scope

| # | Do | Expect |
|---|---|---|
| B1 (#159 test 1) | A Space with a blue preset. Read `WindowTint` on `KoanSpaceBarView` (UI DevTools) | Non-zero (the theme's primary colour), and the window shows the gradient. Default theme: 0. |
| B2 (TT1) | Two windows on Spaces with different themes; in one, switch Spaces 10 times quickly | Each window keeps its own colours; no flash; no `Spaces:` DCHECK. |
| B3a (T2b) | Settings > Appearance > Mode = Light; a dark preset (picker pages 3–4, the window turns dark). Open page info, the downloads bubble, a JS `alert()`, the find bar (⌘F), ⌘T (the floating URL bar) and ⌘L on a page | All of them **light** (the window's scheme), as Zen's panels and open URL bar (`zen-theme.css` applies the Space's dark mode to `#browser` only). The sidebar, toolbar and gutter stay dark (the Space). Glance (⌥-click a link) and the Library follow the Space (dark). |
| B3b (T2b) | The reverse: Mode = Dark, a light preset (page 1, first swatch) | The same surfaces **dark**; the window chrome light. |
| B3c (T2b) | In B3a's window, right-click the page | Native context menus follow the window's NSAppearance, which follows the Space (dark): Koan keeps upstream's rule so the traffic lights keep their contrast (`NativeWidgetMac::SetBackgroundColor`). A known difference from Zen (light menus there): record it. Follow-up: set the menus' own NSAppearance from the window's scheme. |
| B3d (T2b) | B3a's window in macOS fullscreen (⌃⌘F): page info, the downloads bubble, the extensions menu, ⌘T | All **light**, as out of fullscreen (they are anchored in the fullscreen overlay, whose sidebar and toolbar stay dark). |
| B3e (T2b) | B3a's window: ⌥-click a link (Glance), then open page info from the Glance card's site icon, or a permission prompt there | Glance's card dark (the Space), the bubble **light** (the window's scheme). |
| B3f (T2b) | B3a's window: trigger a toast (⌘⇧C copies the URL) | The toast **dark**, like the sidebar (Zen's toast is inside `#browser`). With the default theme: the window's scheme. |
| B3 (TT3) | Blue Space. Open page info (click the site icon in the URL pill), the downloads bubble, a JS `alert()` (`data:text/html,<script>alert(1)</script>`), the find bar (⌘F), a page's context menu, chrome://settings | **Record with screenshots** whether surfaces get a Material You tint. Expected from the code: surfaces stay Zen's greys (*`rgb(31,31,31)` / `rgb(244,244,244)` panels*); only accent things (primary buttons, focus rings, toggles, selection, links) take the Space's colour, as Zen's `--zen-primary-color`. Native menus are native. Anything else tinted is a finding (likely an illustration, `kColorSysIllo*`, or an AI gradient). |
| B4 (TT4, #159 test 3) | Install a local theme .crx (the Mac loop's; no Web Store), then open a default-theme Space and a coloured Space | Neither window shows the extension theme's images or colours: Koan's look and the Space themes stay, and the theme stays listed in chrome://extensions. Uninstall: no change. |
| B5 (TT5) | Settings > Appearance > Mode = Dark; a Space with a light preset (page 1, first swatch); open `data:text/html,<div style="height:300vh">x</div><script>document.title=matchMedia('(prefers-color-scheme: dark)').matches</script>` | The title is `true` and the page's scrollbar is dark. chrome://settings follows the window (light). |
| B6 (TT9) | From a themed window, ⇧⌘N | The Incognito window has Chromium's dark Incognito look, no Space colours; its pages follow the system. |
| B7 (#159 test 4) | A profile from an old build with the tint prefs set (`scripts/mac/spaces/seed_profiles.py`) | The user's own colour comes back once (`MigrateWorkspaceTint`), the tint prefs are cleared, Space themes work. |
| B8 | Settings > Look and Feel > "Show the active Space's theme" off, then on | Off: the window takes the user's colour and mode at once, no gradient (`WindowTint` 0). On: back. No `Spaces:` DCHECK. |
| B10 (TT2) | A themed Space, the picker open: drag the big dot around for 5 s, then release; repeat 3 times | Smooth, no stutter at release. (Instruments, Allocations: `ColorProvider` instances grow by at most two per release, the window's key and its panels' key, not per move.) |
| B9 (TT10) | Mode = Automatic; toggle macOS's appearance in System Settings | The window, menus and traffic lights follow within 1 s. |

## C. Crash path (layers, Labels)

DCHECK build. Each row with a themed Space (a preset with 3 dots) **and** with the default theme.

| # | Do | Expect |
|---|---|---|
| C1 | ⌘S (compact mode) on and off 5 times; hover the left edge to reveal the sidebar each time | No `CustomCornersBackground` CHECK, no opaque-layer DCHECK, gradient behind the sidebar. |
| C2 | Native fullscreen (⌃⌘F) in and out 5 times, with compact mode on and off | Same. |
| C3 | Click another app and back, 10 times (window inactive and active) | Same; an inactive window keeps its theme (no glass on develop, so no greying). |
| C4 | Open the picker, click a swatch, click the slider at 20 / 50 / 80 / 35 % | No `label.cc` subpixel DCHECK, no other CHECK. |
| C5 | Switch theme presets 20 times quickly (keyboard: Tab to the swatches, arrows) | No CHECK; the gutter around the page shows the gradient after each (its layer is re-created when it stops being a solid colour). |
| C6 | ⌘T / Esc 10 times with the picker closed, in a themed window | No CHECK, no repaint storm. |

## C (glass). The translucent path

Develop has no glass (`kGlassFrame` is off), so blocks A–C never take the Space theme's translucent path (Zen's maths for
a transparent window, the translucent layers, the inactive grey). Run this block on **macOS 26, Apple Silicon**, on a
stack of develop + #159 + **#103** (`feat/mac-glass`, its current head; Koan's glass for every window) + #62, as agreed with the
glass thread. Upstream's `--enable-features=GlassFrame` alone is not what Koan ships: its expand-on-hover blur backdrop,
its one-window cap and its missing Reduce Transparency rule are #103's to change, so crashes there don't count against
#62. `glass_frame.enabled` defaults on with the feature on Apple Silicon; nothing to tick. Same pass criteria as C, plus
no `paints a translucent theme on an opaque layer` DCHECK.

| # | Do | Expect |
|---|---|---|
| CG1 | C1–C6, in the glass window | As in C; the gradient is translucent over the blur (the default theme: black at 40% in dark). |
| CG2 | System Settings > Accessibility > Display > Reduce transparency on, then off, with the themed window open (and once with the picker open) | Opaque within 1 s, translucent again after; no CHECK, no black panel. |
| CG3 | Fullscreen (⌃⌘F) in and out 5 times, compact mode on and off | Opaque in fullscreen (not eligible), translucent after; no CHECK. |
| CG4 | A 3-dot preset; the opacity slider at 0.3, then at 1.0 | 0.3: the colours over the blur; 1.0: the opaque paint covers the blur (Koan's opaque end above Zen's 0.80). |
| CG5 | Click another app, then back | Inactive: flat, opaque, macOS's window background colour (Zen's `InactiveCaption`), no theme. Active: the theme over the blur again. |
| CG6 | With a themed window active, open a second window (⌘N) | Both windows glass; each keeps its own Space's theme; no CHECK. |
| CG7 | Three themed windows, ⌘Q, relaunch (session restore) | All three are glass and themed without clicking each; no CHECK. |

## D. Teardown

DCHECK build, a themed Space. After each row: no CHECK, no crash, the other windows unaffected.

| # | Do | Expect |
|---|---|---|
| D1 | Open the picker, change the preset, then ⌘W | The window closes cleanly. (The picker is dropped with the window, not saved: the Space keeps its last saved theme.) |
| D2 | Two windows, the picker open in one; ⇧⌘W in it | That window closes; the other keeps its theme. |
| D3 | Switch Space and press ⌘W within the 250 ms cross-fade | Closes cleanly. |
| D4 | Drag a dot in the picker and press ⌘Q while dragging | Quits; on relaunch the Space has a valid theme (the last saved one). |
| D5 | Drag a tab out of a themed Space's window into a new window | The new window opens in its default-theme Space; the tab keeps working; no CHECK. |
