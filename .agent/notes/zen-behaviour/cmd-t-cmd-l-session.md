# The floating URL bar: Cmd+T / Cmd+L sessions

Zen 1.24b. Source: `src/zen/urlbar/ZenUrlbarController.mjs` (`handleNewTab`,
`handleUrlbarClose`, `onUrlbarOpen/Close`), `src/zen/common/modules/ZenUIManager.mjs`
(`handleNewTab`, `updateTabsToolbar`, `onFloatingURLBarOpen`,
`newTabButtonWantsClipboard`), `src/browser/components/urlbar/content/UrlbarInputBase-mjs.patch`,
`UrlbarChildController-mjs.patch`, `Tabbrowser-sys-mjs.patch`,
`src/zen/common/styles/zen-omnibox.css`, Zen's own tests in `src/zen/tests/urlbar/`,
and the live measurements in `../zen-mac-spec.md` section 4 (measured on the
running app: geometry, no animation, Esc, 45 s memory).

## 1. Two kinds of session

| | **New Tab session** (Cmd+T, File > New Tab, the "+ New Tab" row, `BrowserCommands.openTab`) | **Location session** (Cmd+L, File > Open Location, click/focus the pill) |
|---|---|---|
| Marker | `zen-newtab` attribute on the URL bar; every `#tabs-newtab-button` gets `in-urlbar` | none |
| Gate | `zen.urlbar.replace-newtab` (true) **and** `where == "tab"` **and** not a URL-bearing new-tab request (`werePassedURL`) **and** not a clipboard-search request, **and** not in the welcome stage and not in tests | `Browser:OpenLocation` |
| Opens a tab? | **No**: Cmd+T creates nothing; a tab is created only when a result is chosen | no |
| Input | empty, placeholder `Search...`; **restores the last session's text if less than `zen.urlbar.wait-to-clear` ms (45 000) ago** | the current URL, **fully selected** (even if you had a partial mouse selection: test `test_Floating_Highlight_Everything`) |
| Current tab's selected look | removed: `_visuallySelected = false` while the session lasts | kept |
| "+ New Tab" row | highlighted (`in-urlbar`) | not |
| Result action | `where = "tab"`, `reuseEmpty = true`: opens a **new tab** (reusing the empty/"new tab" if it is one) | opens in the **current tab** (`current`), except: a **pinned** tab navigating to another domain opens a new tab (`zen.tabs.open-pinned-in-new-tab`, true); from the **empty tab** it always opens a new tab |
| Pressed again | **Cmd+T while the session is open closes it** (toggle, no tab created) | Cmd+L keeps it open and re-selects all |

## 2. Opening (input -> what happens)

1. **Cmd+T** (default layout `zen.view.use-single-toolbar = true`):
   `BrowserCommands.openTab` -> `gZenUIManager.handleNewTab` -> `nsZenUrlbarController.handleNewTab`.
   - remember `#lastTab = selectedTab`, set `#lastTab._visuallySelected = false`;
   - remember the URL bar's current label (`#prevUrlbarLabel`) to restore later;
   - install `gURLBar._zenHandleUrlbarClose` (a closure owning this session's id; ids are
     sequential, `#owner`, so a stale close from an older session is ignored);
   - set `zen-newtab`, mark the "+ New Tab" button(s) `in-urlbar`;
   - `gURLBar.search(lastSearch || "")` then run `Browser:OpenLocation` (this opens
     the view and the floating panel).
2. **The panel opens** (`UrlbarInputBase._openPopover` patch): `breakout-extend`
   set; `onUrlbarOpen` blanks the pill (`pageproxystate = invalid`, remembers whether it was
   valid in `had-proxystate`); the floating attribute `zen-floating-urlbar` is set when
   `zen.urlbar.behavior == "float"` **or** (`"floating-on-type"` (default) **and** the bar
   was **not** focused by a mouse press). Then `onFloatingURLBarOpen` ->
   next animation frame `updateTabsToolbar` computes `--zen-urlbar-top` and
   `--zen-urlbar-width` (below).
3. **Geometry** (measured; see spec section 4.2): top = `innerHeight/2 - max(333, barHeight)/2`
   (333 = `FLOATING_URLBAR_MIN_HEIGHT`), width = `min(innerWidth / 1.5, 750)`
   (`FLOATING_URLBAR_MAX_WIDTH`); centred with `left:50%; translate:-50% 0`; `z-index:1000`;
   font 1.5em (macOS) of the 11 pt root = 16.5 pt; 62 pt input row; results max 244 pt.
4. **Timing / animation: none.** The panel appears and disappears in one frame (<= 17 ms
   in a 60 fps recording). The only animations are the search-mode chip
   (`scale 1 -> .98 -> 1`, 0.25 s, plus a 1 s `zen-urlbar-searchmode` sweep) and the
   hover/identity-icon fade (0.15 s).
5. **Mouse path (important, differs from what the old parity doc said):** clicking or
   focusing the pill **with the mouse does NOT float it** (`test_Click_Shoudnt_FLoat_Urlbar`):
   `focusedViaMousedown` is true, so it opens as the normal in-sidebar breakout (the pill
   grows in place, results drop under it). Only keyboard focus (Cmd+L, Cmd+T) floats in
   the default `floating-on-type` mode. **LIVE CHECK** the exact look of the in-sidebar
   breakout and whether typing then moves it to the floating position.
6. **Cmd+T with the accel key held over the "+ New Tab" button** (accel+click) asks for
   "paste and search the clipboard" (`newTabButtonWantsClipboard`, `searchClipboard`):
   this is **not** a floating session; it opens a tab searching the clipboard.
7. **Welcome stage / tests:** no session opens during onboarding.

## 3. While the panel is open

- **Typing**: normal Firefox urlbar results (open tabs "Switch to Tab", history,
  bookmarks, search suggestions). Defaults for an empty input: open-tab results then
  top sites (see spec). `zen.urlbar.hide-one-offs` true (no one-off buttons),
  `zen.urlbar.suggestions.quick-actions` true, `zen.urlbar.show-pinned` true.
- **Tab key on an empty input** (no Ctrl/Alt/Shift): enters **commands mode** (a
  `ZEN_ACTIONS` search mode: the command palette, `enableCommandsMode`), with the
  0.25 s scale pulse. Tab with text moves selection normally.
- **Up/Down/Enter/Ctrl+N/Ctrl+P**: standard urlbar view navigation.
- **Enter**: opens the selected/typed entry per the table in section 1; the bar closes.
- **Switching tabs** (Cmd+1..9, Ctrl+Tab, clicking a tab) closes the session with
  `onSwitch = true`: the typed text is **discarded** (`#clearSessionData`) and the
  pill goes back to the new tab's URL.
- **Window loses focus (Cmd+Tab away)**: the bar stays (focus/blur does not close it);
  returning keeps the text (parity doc: "survives Alt-Tab").
- **Esc**: closes the session in one press (`handleUrlbarClose`); the page regains focus
  ("Arc-like: blur the input on exit"). No tab is created.
- **Click outside the panel**: closes it (and, per the parity notes, the click also
  reaches what was clicked; **LIVE CHECK**).

## 4. Closing (`handleUrlbarClose(closeSeq, onSwitch, onElementPicked)`)

Runs in a `setTimeout(0)` and only if this session still owns the bar (`isOwner`):
1. remove `zen-newtab`; clear `in-urlbar` on the buttons;
2. restore `#lastTab._visuallySelected = true` if it is still selected;
3. text memory: unless closing because of a tab switch, keep `#lastSearch =
   gURLBar._untrimmedValue` and start a timer `waitToClear` (45 000 ms) that wipes it;
4. restore the pill: `setURI({uri: #prevUrlbarLabel, dueToTabSwitch, isSameDocument: !onSwitch})`,
   then `handleRevert()`;
5. if the bar had focus: dispatch `ZenURLBarClosed {onSwitch, onElementPicked, closeSeq}`
   (other code listens, e.g. split view and Glance), close the view, update text overflow.
The `breakout-extend` and `zen-floating-urlbar` attributes are removed and the textbox
position reset (`UrlbarInputBase` blur path); the page browser is focused.

## 5. Single-toolbar blur rule

In the default single-toolbar layout, losing focus **reverts** the typed text to the page
URL (`test_single_toolbar_reverts_typed_value_on_blur`); with `zen.view.use-single-toolbar
= false` the typed value is kept on blur. This is separate from the 45 s *session*
memory above (that one only restores the text when a New Tab session is re-opened).

## 6. Pill text (when not editing)

`trimURL`: in single toolbar with `zen.urlbar.show-domain-only-in-sidebar` (true) the
pill shows the **host only**, `https://` and a leading `www.` stripped and a lone
trailing `/` removed; while the bar is open (`breakout-extend`) the full URL is shown.

## 7. Prefs

| Pref | Default | |
|---|---|---|
| `zen.urlbar.replace-newtab` | true | Cmd+T opens the session instead of a blank tab |
| `zen.urlbar.behavior` | `floating-on-type` | `normal` / `floating-on-type` / `float` |
| `zen.urlbar.wait-to-clear` | 45000 | ms the New Tab text is remembered |
| `zen.urlbar.open-on-startup` | true | open a session when the window starts on the empty tab |
| `zen.urlbar.show-domain-only-in-sidebar` | true | host-only pill |
| `zen.urlbar.single-toolbar-show-copy-url` | true | copy-URL button on hover |
| `zen.urlbar.hide-one-offs` | true | |
| `zen.urlbar.suggestions.quick-actions` | true | |
| `zen.urlbar.show-pinned`, `show-contextual-id`, `show-sponsored`, `show-pip-button`, `show-protections-icon` | true / false / false / false / false | |
| `zen.theme.acrylic-urlbar` | false | glass card (rgba(20,20,20,.92) + blur 40 px); reduced-transparency falls back to opaque |
| `zen.tabs.open-pinned-in-new-tab` | true | pinned tab + other domain -> new tab |

## 8. LIVE CHECK list

1. The in-sidebar breakout look and behaviour after a mouse click (item 2.5) and what the
   user sees when typing in it.
2. Click-outside delivery (does the clicked element also get the click?).
3. Esc with the view open and text typed (one press? two?), after real typing (measured
   one press in a 1470 x 923 window: confirm in the fresh profile again).
4. Cmd+L while a split view / Glance is active (the `ZenURLBarClosed` listeners).
5. Cmd+T while in compact mode with the sidebar hidden (does the sidebar reveal? see
   compact-mode.md; the floating bar is centred in the window, not the sidebar).
6. Fullscreen: bar position relative to the notch strip.

## 9. Test script for the Mac loop (Zen and Koan)

1. Window with 3 tabs, tab 2 selected, URL `https://example.com/page`. Cmd+L: expect the
   centred 750 x 62(+results) card at y = (H - 333)/2, the full URL selected, the pill blank,
   tab 2 still highlighted, "+ New Tab" not highlighted. Esc: card gone in one frame, pill
   restored to `example.com`. No tab created.
2. Cmd+T: expect the same card, **empty input**, "+ New Tab" highlighted, tab 2 **not**
   highlighted. Type `wi`, Esc, within 45 s Cmd+T: expect `wi` back. Wait > 45 s, Cmd+T: empty.
3. Cmd+T, Cmd+T: second press closes the card, no tab.
4. Cmd+T, type `example.org`, Enter: tab count + 1, new tab selected, card gone.
5. Cmd+L, type `example.org`, Enter: tab count unchanged, tab 2 navigates.
6. Pinned tab selected, Cmd+L, type another domain, Enter: opens a **new** tab.
7. Empty tab selected (see close-last-tab doc), Cmd+L, Enter on a result: opens a new tab.
8. Cmd+T, press Tab on the empty input: commands mode chip appears (0.25 s scale pulse).
9. Cmd+T then Cmd+2: card closes, text discarded, pill shows tab 2's host.
10. Mouse click on the pill: **not** a floating card (in-sidebar breakout).
11. Timing: card must appear/disappear in <= 1 frame (record 60 fps, window-only capture).
