# Compact mode (hover reveal, popups, fullscreen)

Zen 1.24b. Source: `src/zen/compact-mode/ZenCompactMode.mjs` (1075 lines),
`zen-compact-mode.css`, `sidebar.inc.css`, `toolbar.inc.css`,
`ZenMouseTrackerCocoa.mm` + `nsIZenMouseTracker.idl`,
`src/zen/common/modules/ZenUIManager.mjs` (popup tracking),
`src/zen/common/zenThemeModifier.js` (`updateElementSeparation`),
`prefs/zen/*.yaml`. Live: spec sections 5 and 15 (real-pointer reveal on the Mac).

## 1. Model

- Compact mode hides the **sidebar** (in the default single-toolbar layout the sidebar is
  the only chrome, so "compact" = no visible chrome at all). With the multiple-toolbar
  layout it can instead (or also) hide the top toolbar (`zen.view.compact.hide-toolbar`);
  with a single toolbar `canHideToolbar` is always false and `canHideSidebar` always true.
- State: attribute `zen-compact-mode="true"` on `:root` **and** `#zen-main-app-wrapper`,
  persisted in `zen.view.compact.enable-at-startup` (written at every toggle once the
  window has finished restoring, so a window reopens in the mode it was closed in).
- The sidebar stays a real toolbox element that is moved off-screen / shown floating by
  CSS; the **page card does not resize when it is revealed**: the revealed sidebar
  floats over the page.

## 2. Toggling

| Input | Result |
|---|---|
| **Cmd+S** (`cmd_zenCompactModeToggle`), the sidebar context menu "Enable compact mode" / "Compact Mode >", `Ctrl+click`... | `gZenCompactModeManager.toggle()`; ignored while a toggle animation runs or while the Library is slightly open (`ZenLibrary.isLibrarySlightlyOpen`); never in popup windows (`shouldBeCompact`) |
| **Cmd+Opt+S** (`cmd_zenCompactModeShowSidebar`) | `toggleSidebar()`: toggles `zen-user-show` on the toolbox: pins the sidebar visible/floating until toggled again (works only in compact mode) |
| `cmd_toggleCompactModeIgnoreHover` | toggle with `_ignoreNextHover`: the sidebar does not pop open because the pointer happens to be over it |
| Dragging the sidebar splitter closed | enters compact mode with the sidebar hidden and shows the toast "Sidebar hidden! Use Cmd+S to bring the sidebar back" (`zen-sidebar-drag-collapsed-toast`) |
| Window starts | applies the saved state after `SessionStore.promiseAllWindowsRestored`, **without animation** |

**Toggle animation** (`animateCompactMode`, only if `zen.view.compact.animate-sidebar` is
true, the sidebar is not potentially open (`zen-user-show`, `zen-has-hover`, or
`zen-has-empty-tab`) and it is not the startup apply):
- entering: the sidebar's margin on the screen-edge side animates `0 -> -(sidebarWidth - 1*elementSeparation)`
  (spring, `bounce: 0`, **0.1 s**, easeIn); the window-wide attribute is set **before**
  animating;
- leaving: the margin animates from `-(sidebarWidth - 3*elementSeparation)` to 0 (**0.1 s**,
  easeOut) with `#browser` clipped meanwhile; the attribute is removed **after** the animation;
- `zen-compact-animating` is on `:root` for the duration (blocks re-toggles);
- afterwards the URL bar regains focus if it had it, `ZenCompactMode:Toggled` is dispatched.
  (Do not confuse with the 0.25 s reveal slide below.)

## 3. Revealing (hover) and hiding

### Elements and hot zones (`hoverableElements`)

| Element | Screen edge | Keep-hover |
|---|---|---|
| the sidebar toolbox | left (right if `zen.tabs.vertical.right-side`) | `zen.view.compact.sidebar-keep-hover.duration` = **150 ms** |
| `#zen-appcontent-navbar-wrapper` (top toolbar, only if it is hidden) | top | none |
| the macOS window buttons box | n/a | n/a |

### Reveal

1. Pointer enters the element (`mouseover`/`dragover`): wait `HOVER_HACK_DELAY` (pref
   `zen.view.compact.hover-hack-delay`, default **0** ms) then set **`zen-has-hover`** on it,
   unless: the pointer is over a `panel`; `supress-primary-adjustment` is true; the floating
   URL bar was hovered (`_hasHoveredUrlbar`); `_ignoreNextHover`; or already hovered.
   `zen.view.compact.show-sidebar-and-toolbar-on-hover` (true): if false the element only
   gets `zen-has-implicit-hover` (used for the URL pill's buttons) and does not slide in.
2. CSS (`sidebar.inc.css`): while the toolbox matches `[zen-has-hover] | [zen-user-show] |
   [zen-has-empty-tab] | [flash-popup] | [has-popup-menu] | [movingtab] | [zen-compact-mode-active]`
   it slides in: `translate` over **`--zen-compact-mode-time: 0.25s`** with a spring-shaped
   `linear()` easing `--zen-compact-mode-func`: 0 -> 0.50 at 20% of the time, 0.90 at 41%,
   1.000 at 59%, peak ~1.007 at 88%, 1.003 at 100%. The hidden position is
   `translateX(-(100% - --zen-compact-float - 1px))` with `--zen-compact-float` = the
   element separation (8 pt) or 10 pt in the top-toolbar-hidden case; `transition: visibility 0.15s`
   delays `visibility: hidden` so the slide-out is seen.
3. Geometry measured live (1470 pt window): hidden toolbox x = -241 (246 wide incl. float margin);
   revealed x = **-4**, **246** wide, **10 pt** inset top/bottom/left, rounded and shadowed,
   darker/more opaque than the docked sidebar; the page card spans x 8 .. W-8; the splitter
   becomes a 6 pt hot-zone at x -10. Real pointer at the left edge: nothing at +0.18 s,
   fully shown by ~0.7 s (capture latency included); hidden < 0.5 s after leaving.

### Hide

- `mouseleave`: ignore if the pointer moved onto the mac window buttons box (the box is
  part of the sidebar's hot zone: x/y within `actualWindowButtons`). After `HOVER_HACK_DELAY`:
  not hidden if hovered again, a pending enter exists, the URL bar is floating-focused,
  `supress-primary-adjustment` (single toolbar), `_ignoreNextHover`, a **tab is being dragged**
  (`_isTabBeingDragged`), or `dragleave` into a descendant.
- If the element has a keep-hover duration (sidebar: 150 ms) and `zen-has-hover` is set: **flash**
  (keep it open) for 150 ms then collapse; otherwise collapse on the next animation frame.
- The toolbox is also kept open by `has-popup-menu` (see 4), `zen-compact-mode-active`
  (see 4) and `flash-popup`.

### Leaving the window through the edge

When the pointer leaves the **window** within 10 pt of the sidebar's screen edge and within
the sidebar's vertical extent (+7 pt), `_trackMouseOutsideWindow` registers the window with
the **native mouse tracker** (`nsIZenMouseTracker`, `ZenMouseTrackerCocoa.mm`): while the
pointer stays within `zen.view.compact.outside-window-edge-offset.horizontal` = **200 pt**
(vertical: **100 pt** for the top edge) of the window, the sidebar **stays revealed**; moving farther,
deactivating the window (`deactivate` event) or the tracker's `zen-mouse-tracker:exited`
collapses it. If the tracker cannot register (non-macOS/failure) it falls back to a flash of
`zen.view.compact.toolbar-hide-after-hover.duration` = **1000 ms**. The first mouse move after
leaving that is back over the window and not on the element hides it immediately.
(The point: you can fling the pointer to the screen edge to reveal the sidebar without
stopping exactly on the window edge, and a sidebar opened this way does not vanish while the
pointer is on the Dock / next to the window.)

## 4. What keeps the sidebar open besides hover

| Reason | Mechanism |
|---|---|
| A **popup opened from the sidebar** (context menu, tab menu, Space menu, the theme picker, a dropdown) | `ZenUIManager` listens for `popupshowing`: if the popup's origin is inside a tracked element (the sidebar; the top toolbar wrapper), that element gets `has-popup-menu`. Ignored: popups inside a `panel`, and plain `menupopup` elements for the sidebar (bug 7590). On `popuphidden`: the attribute is removed immediately if the main window is `:hover`, otherwise it is kept **until the next mouse move** into the window |
| A **panel/open/breakout** descendant of the sidebar (e.g. a site-info panel, the in-sidebar URL breakout opened by a mouse click) | `zen-compact-mode-active` via the `:has()` polyfill (selector `[panelopen], [open], [breakout-extend]`, excluding `#urlbar[zen-floating-urlbar]`, tabs and `.zen-compact-mode-ignore`) |
| The **floating** (keyboard) URL bar | does **not** hold the sidebar open (explicitly excluded); the bar floats centred in the window while the sidebar stays hidden; hovering that bar sets `_hasHoveredUrlbar` so the sidebar does not pop open under the pointer |
| The **empty tab** is selected | `zen-has-empty-tab`: the sidebar is shown and cannot hide (see close-last-tab doc) |
| `zen-user-show` | Cmd+Opt+S pin |
| A **tab drag** in progress | `movingtab` on the tab strip keeps it open |
| **Renaming a tab** | `zen-renaming-tab` forces it open |
| **Flash** | `flash-popup` for `zen.view.compact.toolbar-flash-popup.duration` (800 ms) when `zen.view.compact.toolbar-flash-popup` is true (default **false** in the shipped prefs; code default true): after exiting DOM fullscreen (`flashSidebarIfNecessary`), not in split view and not during a Glance animation |

## 5. Background tab opened while the sidebar is hidden

`_onTabOpen(tab, inBackground)`: if the tab opens in the background, compact mode is on,
the sidebar is not potentially open (`isSidebarPotentiallyOpen`), the Glance is not animating
and `zen.view.compact.show-background-tab-toast` is true, a **toast** appears (message
`zen-background-tab-opened-toast`, with an "Open" button that selects the tab). If Space
routing sent the tab to another Space the toast text is `zen-space-routing-tab-routed-toast`
(with the target Space name) and has no button. No toast if the sidebar is already visible
and the tab landed in the current Space.

## 6. Fullscreen and compact mode

- **DOM fullscreen** (a video's fullscreen button): `#navigator-toolbox` is
  `visibility: collapse` (no sidebar at all), the page fills the window; the top toolbar
  wrapper is hidden too; on exit, `flashSidebarIfNecessary` flashes the sidebar if the
  flash pref is on, and the URL bar height is recalculated.
- **Native macOS fullscreen** (Ctrl+Cmd+F) **in compact mode** with
  `zen.view.borderless-fullscreen` (true): `updateElementSeparation` sets
  `--zen-element-separation: 0.1px` (and `zen-no-padding="true"`), so the **page card loses its
  8 pt margins and its rounded corners** (`--zen-native-inner-radius: 0px`) and runs edge to edge
  below the notch strip; the sidebar still reveals on the left edge hover. **Not** applied when
  split view is active. In native fullscreen *without* compact mode the margins stay
  (spec section 12). Both transitions re-run on `MozDOMFullscreen:Entered/Exited`.
- `sizemodechange` (minimise, maximise, fullscreen change) calls `_clearAllHoverStates`:
  any hovered element that is no longer under the pointer collapses.

## 7. Prefs

| Pref | Default (shipped) | |
|---|---|---|
| `zen.view.compact.enable-at-startup` | false | persisted state |
| `zen.view.compact.hide-tabbar` | true | compact hides the sidebar |
| `zen.view.compact.hide-toolbar` | false | (multi-toolbar layouts) also hide the top toolbar |
| `zen.view.compact.animate-sidebar` | true | the 0.1 s toggle animation |
| `zen.view.compact.show-sidebar-and-toolbar-on-hover` | true | hover reveal |
| `zen.view.compact.sidebar-keep-hover.duration` | 150 ms | keep-open after leaving the sidebar |
| `zen.view.compact.toolbar-hide-after-hover.duration` | 1000 ms | fallback keep-open after leaving through the edge (no native tracker) |
| `zen.view.compact.outside-window-edge-offset.horizontal` / `.vertical` | 200 / 100 pt | native tracker reach (code defaults 250 / 150, the shipped prefs override) |
| `zen.view.compact.toolbar-flash-popup` / `.duration` | false / 800 ms | flash after DOM fullscreen exit |
| `zen.view.compact.show-background-tab-toast` | true | background-tab toast |
| `zen.view.compact.hover-hack-delay` | 0 ms | debounce on enter/leave |
| `zen.view.compact.debug` | false | log |
| `zen.view.borderless-fullscreen` | true | edge-to-edge page in fullscreen + compact |
| `zen.view.use-single-toolbar` | true | in single toolbar only the sidebar is hidden |
| `zen.tabs.vertical.right-side` | false | sidebar on the right: edges mirror |

## 8. LIVE CHECK list (needs a fresh Zen and the screen)

1. Sidebar reveal with the pointer **dragged** with a tab (the `movingtab` path), and on
   dropping elsewhere.
2. Popup from the sidebar (right-click a tab): sidebar stays; after the menu closes with the
   pointer **outside** the window, when does it collapse (next mouse move)?
3. Reveal while a native fullscreen Space is active; the notch strip; whether the edge
   hot-zone is the screen edge or the window edge there.
4. The theme picker popover while the sidebar is revealed (does it keep it open? it is a
   `panel`, so the origin test is skipped).
5. Cmd+T while hidden: the bar floats; does `supress-primary-adjustment` stop the sidebar
   from popping when the pointer is at the edge?
6. The exact toggle visuals (0.1 s): record at 60 fps window-only and read the margin curve.

## 9. Test script for the Mac loop (Zen and Koan)

1. Default layout, one tab. Cmd+S: sidebar slides off in ~0.1 s; the page card is **x 8 .. W-8**,
   y 8 .. H-8 (equal 8 pt gaps). Cmd+S again: sidebar returns, card back to `x = sidebarW`.
2. Compact on. Move the pointer to the left window edge: after <= 1 frame + 0.25 s the sidebar is
   at x -4, 246 wide, floating (card does not move). Leave to the right: after 150 ms it
   slides out (0.25 s). Record 60 fps and compare curves (spring-shaped: 50% at 20% of the time).
3. Pointer to the left edge **outside** the window (within 200 pt, on the desktop): the sidebar
   stays; move 250 pt away: it collapses; Cmd+Tab away: it collapses at once.
4. Right-click a tab while revealed: the menu opens, the sidebar stays; choose Escape; with the
   pointer inside the window it hides (150 ms); with the pointer outside it hides on the next
   mouse move.
5. Cmd+Opt+S: sidebar pinned open (even with the pointer elsewhere); again: unpinned.
6. Cmd+T in compact mode: the bar floats centred; sidebar stays hidden; Esc: nothing visible changes.
7. Close the last tab in compact mode: the sidebar is shown (empty tab) and cannot hide until a
   tab is opened.
8. Open a link in a background tab (middle-click) while hidden: toast with an Open button.
9. Native fullscreen (Ctrl+Cmd+F) in compact: page edge to edge (no card margin/radius), sidebar
   reveals at the left edge; exit: margins return. In split view: margins stay.
10. A video's fullscreen: no sidebar; Esc: back (flash only if the flash pref is on).
11. Quit and relaunch in compact mode: the window opens in compact mode with no animation.
