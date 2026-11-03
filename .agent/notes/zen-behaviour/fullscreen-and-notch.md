# Fullscreen and the notch

Zen 1.24b. Two different things share the word:

- **Native macOS fullscreen** (green button / **Ctrl+Cmd+F** / View menu): the OS moves the window to its own Space.
  Mostly OS behaviour; Zen adds a few state-dependent rules.
- **DOM fullscreen** (a page calls `requestFullscreen`, e.g. a video's fullscreen button): Zen hides its chrome.

Sources: live capture (`.agent/notes/zen-mac-spec.md` sections 2A "Fullscreen" and 12, from the user's own photo
`ref-02-fullscreen-notch-strip.png`); `src/zen/common/zenThemeModifier.js` (`updateElementSeparation`),
`src/zen/common/styles/{zen-browser-ui,zen-toolbar,zen-browser-container,zen-omnibox,zen-single-components}.css`,
`src/zen/tabs/zen-tabs.css`, `src/zen/compact-mode/ZenCompactMode.mjs`, `src/zen/spaces/ZenSpaceManager.mjs`,
`prefs/zen/view.yaml`. Zen has **no notch-specific code** in 1.24b (`grep -ri notch` finds only unrelated names); the
"notch" behaviour is entirely what the OS does with a window in native fullscreen on a notched MacBook, which
we measured. Related: `compact-mode.md` section 6, `cmd-t-cmd-l-session.md`, `split-view.md`, `glance.md`.

## 1. Native fullscreen (measured on the notched 1470 x 956 MacBook screen)

| Aspect | Zen's behaviour |
|---|---|
| Entering | The window leaves its Space for a new fullscreen Space (OS animation, ~0.5-0.7 s). Attributes on the root: `inFullscreen=true`, `sizemode=fullscreen` (`macOSNativeFullscreen` is referenced by CSS; LIVE CHECK 7). |
| **Notch** | The top **33 pt strip** (the OS safe area beside the camera housing) **stays black and empty**: the Zen window is 1470 x 923 at (0, 33). **Nothing is drawn under or behind the notch**, so nothing (sidebar, URL pill, traffic area) is clipped by it. The window background does not extend into the strip. |
| Menu bar | Auto-hides; shows again when the pointer hits the very top edge (it temporarily covers/pushes the strip, OS). |
| **Traffic lights** | **Gone** (the button box has no size); the **sidebar toggle becomes the first item of row 1 at the far left (x 6)**, with back / forward / reload still right-aligned in the same positions as in windowed mode (x 163 / 194 / 225, row 38 high). |
| Everything else | **Unchanged**: URL pill, Essentials, Space header, divider, tabs, bottom bar; **the page card keeps its 8 pt margins and 7 pt radius** (`--zen-element-separation`). |
| Exiting | Ctrl+Cmd+F, or the green button revealed from the menu bar, exits (whether Esc does is a LIVE CHECK 7). The window returns to its previous frame, the traffic lights return and the toggle moves back after them. |

### 1.1 Native fullscreen + compact mode (borderless)

With `zen.view.borderless-fullscreen` = **true** (default) and compact mode on and **no split view**:
`updateElementSeparation` sets the element separation to **0** (clamped to 0.1 px, which sets `zen-no-padding`):
**the page goes edge to edge**: no card margin, no radius (`border-radius`/shadow rules are guarded by
`:not([zen-no-padding])`), directly below the notch strip. The sidebar reveals by hovering the left edge as in
windowed compact mode. Leaving fullscreen (or leaving compact mode, or opening a split view) restores the normal
separation. Without compact mode the margins stay (table above). Both transitions re-evaluate on
`MozDOMFullscreen:Entered/Exited` and on `fullscreenchange`.

Right-hand sidebar in compact + native fullscreen: `:root[macOSNativeFullscreen][zen-compact-mode][zen-right-side] body`
gets `contain: none` (workaround for issue 14409: otherwise the right-edge hover zone stops working).

`sizemodechange` (minimise, maximise, fullscreen enter/exit) calls the compact mode `_clearAllHoverStates` so a stuck
reveal doesn't survive the transition.

## 2. DOM fullscreen (video / page fullscreen)

- The whole chrome disappears: `#navigator-toolbox` is `visibility: collapse`, the appcontent navbar wrapper is
  collapsed, the sidebar's top bar collapses to `max-height: 0; opacity: 0; pointer-events: none`.
- The content fills the window: the browser containers lose the card look (rules are all guarded by
  `:root:not([inDOMFullscreen])`): no margin, no radius, no shadow.
- The tab-loading indicator is hidden; the URL bar keeps `visibility: visible` in DOM fullscreen (rule cites issue 5229).
- Split view: pane outlines and headers hide, only the fullscreen pane shows with `inset: 0`; Glance: a Glance whose
  page requests fullscreen **expands to a normal tab first** (`glance.md` section 5).
- Space strip bookkeeping (`onPinnedTabsResize`, `onWindowResize`) is **skipped** while in DOM fullscreen so
  layout work doesn't run on the hidden sidebar.
- A 0.5 px right padding is put on the selected browser until the next paint when DOM fullscreen is entered (a repaint
  workaround in `updateElementSeparation`).
- **Exit** (Esc, or the page's button): chrome returns, the sidebar height is recomputed
  (`flashSidebarIfNecessary(aInstant)`), and in compact mode the sidebar **flashes** for 800 ms only if
  `zen.view.compact.toolbar-flash-popup` is true (default false).

## 3. Prefs and constants

| Pref | Default |
|---|---|
| `zen.view.borderless-fullscreen` | true |
| `zen.view.compact.toolbar-flash-popup` | false |
| `zen.view.compact.toolbar-flash-popup.duration` | 800 (ms) |

Constants: notch strip 33 pt (OS), element separation 8 (0.1 min), card radius 7.

## 4. LIVE CHECK list

1. Native fullscreen in the **default** layout: confirm the 33 pt strip, the toggle at x 6, no traffic lights (already
   measured on the user's photo; recheck with our own capture, window-only `-l`, in a separate short run).
2. Native fullscreen + compact mode: page edge to edge? Reveal by left-edge hover? What is at the top (the strip)?
3. Hover at the top edge: the menu bar over the strip; does the page content shift?
4. Window state after exit: same frame, traffic lights back, toggle repositions.
5. DOM fullscreen via a video (YouTube local test page `<video>` + button): chrome collapsed, exit via Esc, flash (off).
6. Cmd+T / Cmd+L in native fullscreen: URL bar position relative to the strip (the `cmd-t-cmd-l-session.md` item 6).
7. Does Esc exit native fullscreen? Is the `macOSNativeFullscreen` root attribute really set (spec only lists `inFullscreen`)?

## 5. Test script (Mac loop; Zen and Koan; do native fullscreen in its own short run)

1. Windowed: note row 1 (traffic lights, toggle, back/forward/reload) and the card margins.
2. Ctrl+Cmd+F: wait for the OS animation; capture (window `-l` of the new Space window only): black 33 pt strip above, no
   traffic lights, toggle at far left of row 1, card margins unchanged. Ctrl+Cmd+F again: back.
3. Enter compact mode (Cmd+S... see `compact-mode.md`), fullscreen again: page edge to edge. Hover the left edge:
   sidebar reveals. Exit.
4. Load a page with a `<video>`; click its fullscreen button: all chrome gone, page fills; Esc: chrome back and no flash.
