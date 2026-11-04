# LIVE CHECK results, 2026-11-04 (real Zen 1.24b on the Mac)

One fresh-profile Zen (`-no-remote -marionette`, `zen.welcome-screen.seen` seeded so no onboarding or import), window-only
captures, real pointer events (HID `mouseMoved`) and real key events (Marionette key actions), the user away. Window 1249 x 830
at (221, 33); sidebar 230 wide. Measurements are DOM geometry sampled every animation frame inside Zen, so timings are exact
to a frame (16.7 ms). Raw captures are in `~/work/koan/.cache/zen-ref/raw/live1/` (not committed).
Items below refer to the LIVE CHECK numbers in each script.

## Compact mode (`compact-mode.md`)

| Check | Result |
|---|---|
| Toggle animation (Cmd+S equivalent, `toggle()`) | Starts ~30 ms after the call; **~100 ms** total (`t=84 -> 178 ms` in the recording). Page card `x 230 -> 8` with a decelerating curve, no overshoot (`101.9, 68, 38, 16.4, 10.4, 8.6, 8` at 84, 95, 111, 128, 145, 161, 178 ms); card width `1011 -> 1233` (= 1249 - 2 x 8), y 8, height 814 (= 830 - 16). Sidebar toolbox `x 0 -> -241`, 246 wide (230 + the 8 pt float + 8). So the 0.1 s spring (bounce 0) in the script is right |
| Hover reveal geometry | Revealed toolbox `x = -4`, `y = 4`, **246 x 822**; page card unchanged (8, 8, 1233, 814): it floats over the page |
| Reveal timing (real pointer) | The pointer's last move into the edge zone (window x = 5) at t = 673 ms; `zen-has-hover` set at **734 ms (+61 ms)**; slide `-241 -> -4` over **~226 ms** with a slight overshoot (`-1.4` at +160 ms, settling to `-4` at +226 ms, ~1 % of 246): matches the 0.25 s spring-shaped `linear()` curve |
| Hide timing | `mouseleave` at 2907 ms; `zen-has-hover` removed at ~3090 ms: **~150-185 ms later** (keep-hover 150 ms + a frame); slide-out `-4 -> -241` in **~150 ms** (`-30.7, -85.9, -140.5, -179.6, -206, -223.2, -233.7, -239.3, -241`), visually done at ~140 ms (the easing reaches 1.0 at 59 % of 0.25 s) |
| Outside-window tolerance (LIVE CHECK 3 / pref 200 pt) | Pointer left through the left edge: the sidebar **stays** at 3, 5, 71, 161, 191 and **199 pt** outside the window, **collapses at 206 pt** (and 216). So the 200 pt horizontal tolerance is measured from the window edge and works as written |
| After the tracker collapsed it | A pointer that **teleports** from outside back onto the edge zone (x = window + 2) does **not** reveal; moving into the window first and then back to the edge does. (First mouse move after leaving is consumed, as the script says.) A real pointer crossing the edge continuously should be unaffected, but a Koan test that jumps the pointer must move inside first |
| Menu from the sidebar (LIVE CHECK 2) | With the sidebar revealed, a tab context menu opened natively: the sidebar stays; moving the pointer to the page with the menu open: stays; after the menu closes with the pointer inside the window it collapses by **+0.6 s with no further mouse move** (still open at +0.12 s). With the pointer **outside the window but within 200 pt** it stays after the close, even after another mouse move |
| Cmd+Opt+S | Sets `zen-user-show` and reveals (`x = -4`); again: hides. Works with the pointer elsewhere |
| Background tab while hidden | Toast "New background tab opened!" with an Open button (`#zen-open-background-tab-button`) |
| Close last tab in compact mode | Window stays; empty tab selected; toolbox gets `zen-has-empty-tab` and shows (`x = -4`) within ~0.2 s and stays; the URL bar does **not** open |
| Cmd+T with the sidebar hidden (LIVE CHECK 5) | Sidebar stays hidden (`-241`); with the pointer already on the edge the sidebar was already revealed and stayed (no pop on top of it) |
| Native fullscreen in compact (LIVE CHECK 3/9) | `BrowserCommands.fullScreen()`: window **1470 x 923 at y = 33** (the 33 pt notch strip is above it), root gets `inFullscreen`, `sizemode=fullscreen` and **`macOSNativeFullscreen`** (answers fullscreen LIVE CHECK 7); `--zen-element-separation` = `0.1px`, `zen-no-padding`, **card (0, 0, 1470, 923): edge to edge**. Pointer at the screen's left edge reveals the sidebar (`x = -5`); away: hides. Compact OFF while fullscreen: card `(230, 8, 1232, 907)`, separation 8; ON again: edge to edge. Exit restores 1249 x 830 at (221, 33). Another window of the same process (`id 6389`, no name, 1470 x 68 at y = 33) exists at the top during fullscreen; I did not identify it (possibly the native fullscreen titlebar/toolbar overlay) |

## Empty tab and closing the last tab (`close-last-tab-and-empty-tab.md`)

1. Fresh profile starts with one hidden empty tab (`zen-empty-tab`, `about:blank`, selected) and **no URL bar open**; one Space.
2. Close the only real tab (Cmd+W): window stays, the empty tab is selected, pill blank, `zen-has-empty-tab` set (sidebar shown),
   the URL bar is **not** opened automatically (LIVE CHECK 5 answered: no). Settled in ~0.2 s.
3. Cmd+W on the empty tab (pref `zen.tabs.close-window-with-empty` true): **the window closes and the app keeps running** (process alive,
   still the frontmost app with no windows).
4. From the empty state: Cmd+T opens the floating bar with the **remembered text** (see below), Enter on a URL opens a **new** tab
   (`[empty tab, new tab selected]`), Cmd+W returns to the empty state.
5. Startup from the empty state: see the correction in the script (URL bar not open live).
6. Not done: Essentials present + pref false (LIVE CHECK 2), private window (4).

## Cmd+T / Cmd+L (`cmd-t-cmd-l-session.md`)

| Check | Result |
|---|---|
| Panel geometry (window 1249 wide) | Floating bar `x 250, y 249, width 750`; **centred** ((1249 - 750) / 2 = 249.5); height **182** with two results ("Visit" + search), 280 with a longer list; dark panel (matches the spec's `rgb(24,24,24)`), large soft shadow, an icon row with the typed text, a divider, then result rows (capture `06-cmdT-clean.png`). Attributes: Cmd+T -> `zen-newtab`, `zen-floating-urlbar`, `breakout-extend`; **Cmd+L -> `zen-floating-urlbar`, `breakout-extend` (no `zen-newtab`)** as the script says |
| Open timing | Appears within one frame of the key (no animation seen in `getBoundingClientRect`, opacity or `transform`; `getAnimations()` empty): the sample at +246 ms in the recording was already final; key to visible ~50 ms |
| Text memory | Esc then Cmd+T again within 45 s: the bar shows the **previous text** (`example.com`) selected; Cmd+L from the empty state shows the empty value. Matches `#lastSearch` / `waitToClear` |
| Esc | One press closes it; no tab created (`tabs` unchanged) |
| Click outside the panel (LIVE CHECK) | One left click on the page outside the bar closes it |
| Enter on a typed URL | New tab opened and selected, sidebar hidden again in compact |
| Cmd+T from the empty state | Works (sidebar remains visible because of the empty tab) |

## Fullscreen and the notch (`fullscreen-and-notch.md`)

Native fullscreen: window frame `1470 x 923 at (0, 33)`; `macOSNativeFullscreen` is set (LIVE CHECK 7); in compact mode the card is edge to edge as the script says;
in a non-compact fullscreen the card keeps its margins (`230, 8` ... 8 pt separation). The 33 pt strip is above the window (OS). Traffic lights hidden: in the
revealed sidebar the row starts with the sidebar toggle at the left.

## Folders (`folders.md`)

| Check | Result |
|---|---|
| Create via `createFolder([tab], { renameFolder: true })` | Folder `New Folder` expanded; contents `[hidden pinned empty tab, the tab]` (2 tabs, as the script says); **rename input starts immediately with the whole text selected** (`selectionStart 0, selectionEnd 10`); `zen-renaming-tab` set |
| **Esc** during that rename (LIVE CHECK 2) | Keeps the default name **"New Folder"**; the folder stays |
| Indent | Child tab 14 px right of the header label (`-213` vs `-227`, sidebar hidden) |
| Collapse with the selected tab inside | `has-active` set, icon `state=close`, `active=true` (the dots), the tab stays exactly where it was (opacity 1, unchanged y): no movement because only other items would hide |
| Delete Folder (menu action `delete()`), LIVE CHECK 9 | **No dialog**; the folder and its tabs close (hidden empty tab too). **Cmd+Shift+T then restores a plain Firefox `tab-group` named "Work" holding the tab (pinned), not a `zen-folder`**: folder look/behaviour is lost (a Zen 1.24b quirk; do not copy it) |

## Not covered this slot

Spaces, Essentials/pinned, split view, Glance, theme picker, Library, folder double-click and the sidebar drag cases: need another slot.
