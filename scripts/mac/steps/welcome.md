# Mac steps: the welcome screen, chrome://koan-welcome (#44)

For the Mac loop and the hands-on tester. A DCHECK build; a fresh `--user-data-dir` for every row unless it says otherwise;
**without** `--no-first-run` and `--no-default-browser-check`; always `--use-mock-keychain`, and
`--enable-logging=stderr --v=0`. After every row, check the log and crash reports (no CHECK, no DCHECK, no
`Unhandled chrome.send`). The page is a DevTools target (`/json/list`, URL `chrome://koan-welcome/`): read it with CDP
`Runtime.evaluate`.

**Import safety:** never import anything. On the import page keep **"NO, start fresh."** (the first choice), except in
W-IMP, which picks a browser only to see the dialog open and then presses **Cancel** in it.

Zen's reference: `.agent/notes/zen-behaviour/onboarding.md`, `.agent/notes/zen-mac-spec.md` §1 and §13, and the measured
JSON in `~/work/koan/.cache/zen-ref/raw/measure-*.json`. Measure each page after its entry animation (about 3.5 s after
launch for the intro, 1.5 s after each Next). Audit: `.agent/notes/upstream/welcome.md` (W1-W28).

## The flow

| # | Do | Expect |
|---|---|---|
| W-1 | Launch (1470 x 956 screen) | One window, 1200 x 720, centred on the screen, no traffic lights, the green intro filling it, "Welcome to / a calmer internet" springing in, then the round arrow. No Chromium intro window, no infobar, no white frame before the green (W13). |
| W-2 | Try to resize the window from each edge and corner; double-click where the title bar would be; ⌥-click the (hidden) zoom area | The size doesn't change (W5). |
| W-3 | Press on the green background, move 200 px, release; then click Start | The window follows and stays (W9). After the release, hovering the page still works and Start takes one click. Repeat 10 times fast: no crash, no stuck window. |
| W-4 | Start, then Next through every page | The pages in Zen's order: import ("NO, start fresh." chosen), colours, search engine, Essentials, default browser, "All set?". Back on every page but the first returns with the reverse slide. Compare each page with Zen's measurements (title 33px/700, card 86% x 82%, sidebar 38%). |
| W-5 | Colours page with #41's picker in the stack | The picker opens over the right pane at Zen's place, fades in; Back or Next closes it (0.12 s). Without #41: the nine colours; picking one recolours the active Space live. The auto/light/dark buttons switch the browser's scheme. |
| W-6 | Search: pick another engine, Back, Next | The engine is the default at once (Settings > Search engine); after Back and Next it is still the one shown as chosen. |
| W-7 | `--search-engine-choice-country=DE`; finish the welcome, quit, relaunch twice | No Chromium search engine choice dialog, first run or later (W14). |
| W-8 | Essentials: pick Obsidian and GitHub, Next to the end, "Dive in!" | The screen fades, the window buttons come back, the window can be resized again, and the two apps are pinned tabs (lazy, not loaded). The keyboard is on the page. |
| W-9 | Default browser: keep "NO", finish; relaunch three times | No default-browser, pin or PDF infobar (with #169). With "YES": macOS asks to confirm; answer "Keep Safari". |
| W-10 | Relaunch after finishing | No welcome. The window opens at 1200 x 720 (its saved size, W8). |
| W-11 | `kill -9` on the colours page; relaunch | No welcome (it is seen once shown, W3); the session comes back (#169). |

## Edge cases

| # | Do | Expect |
|---|---|---|
| W-12 | Esc on any page | The screen closes, nothing more applies, the window is resizable and has its buttons. |
| W-13 | ⌘W, then ⌘Q, during the welcome (a fresh profile each) | No crash, no CHECK. After ⌘W: no window left, Dock click opens a normal window without the welcome. |
| W-14 | ⌘T, ⌘L, ⌘N, ⌘, and the menu bar's File and View items during the welcome | No crash. Record what each does (W25: the welcome doesn't block them, as in Zen); report anything that shows over the screen. |
| W-15 | ⌃⌘F during the welcome, then ⌃⌘F again | The screen covers the fullscreen window and goes back with it; no crash (W6). |
| W-16 | A 1280 x 800 display, and a display smaller than 1200 x 720 if one is available (or a scaled resolution) | The window is the screen's work area at most, centred; the card fits. |
| W-17 | Right-click on the page and on the card | No context menu (W12). |
| W-18 | After the welcome, type `chrome://koan-welcome` in a tab | A blank page; no window resize, no button hidden, nothing changes (W10). |
| W-19 | `--incognito` on a fresh profile; a guest window | No welcome (W26). |
| W-IMP | On the import page pick Safari (if listed), Next to the end, "Dive in!" | After the screen closes, Chromium's import dialog opens in front. **Press Cancel.** Nothing is imported. |
| W-20 | VoiceOver on: the start button, a page's choices, Next | Each is announced; the choices are one radio group. |
| W-21 | Dark and light system appearance, with the system accent changed | The page's accent follows Zen's formula from the system accent (`--zen-primary-color`); the Next button is `oklch(0.55 c h)` in dark. |
