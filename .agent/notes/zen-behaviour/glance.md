# Glance

Zen 1.24b. Source: `src/zen/glance/ZenGlanceManager.mjs` (2133 lines), `actors/ZenGlanceChild.sys.mjs` and
`ZenGlanceParent.sys.mjs`, `zen-glance.css`, `zen-glance.inc.xhtml`, `prefs/zen/glance.yaml`,
`src/browser/base/content/zen-keysets.inc.xhtml`, `src/zen/common/zen-sets.js`,
`src/browser/themes/shared/zen-icons/icons.css`, `locales/en-US/browser/browser/{zen-general,preferences/zen-preferences}.ftl`,
tests in `src/zen/tests/glance/`. Live: `.agent/notes/zen-mac-spec.md` section 7 (measured on a real Option-click).
Related: `split-view.md` (Glance -> split), `folders.md`, `essentials-and-pinned.md` (external links from pinned tabs).

## 1. Model

- A **Glance** is a temporary, child tab shown as a **centred card (80 % wide, full height)** over its parent tab,
  which stays visible and dimmed behind it. The card is a normal tab (`zen-glance-tab`, `glance-id`) whose element
  is **appended inside the parent tab's content** in the sidebar (it is not a row of its own); it has
  `ownerTab = parent`, same container (`usercontextid`) as the parent.
- One Glance at a time per window: a second open request while one is showing returns the current one (and if the
  parent was selected, switches back to the glance). `#glances` keeps an id -> {tab, parentTab, browser,
  originalPosition, elementData} map for reopen.
- Selecting the parent tab hides the glance; re-selecting the glance tab (the child) shows it again (`quickOpenGlance`).
  Any location change/selection of a tab that is not this glance's tab quick-closes the glance state
  (`onLocationChange`).
- A Glance **ends** in one of three ways: closed (tab removed), **expanded** to a full tab (section 5), or turned into a
  **split view** (section 6).

## 2. Opening

| Input | Zen does |
|---|---|
| **Option (Alt) + click** a link (default `zen.glance.activation-method` = `alt`; options `ctrl`, `alt`, `shift`, `meta`) | opens that link in a Glance |
| Right-click a link -> **Open Link in Glance** (`context-zenOpenLinkInGlance`, before the "open" separator) | same, animating from the rectangle recorded on the last mousedown (the link under the right-click) |
| Option-click a **bookmark** (toolbar/Library) | `openGlanceForBookmark`: Glance from the bookmark's rectangle (positions relative to the page area) |
| Pinned (non-Essential-only) tab that is an **app tab** opens a link to **another domain** (`zen.glance.open-essential-external-links` = true) | the new tab is turned into a Glance on its pinned owner: `shouldOpenTabInGlance`: owner is pinned, owner has no glance, owner's browsing context `isAppTab`, host differs (about: and non-http(s)/file are excluded) |
| A search chosen with `where = tab` (`onSearchSelectCommand`), gated by `zen.glance.enable-contextmenu-search` (true) and the tab having an owner | the results tab opens as a Glance on its owner tab, from the last click position (or the content centre). Which UI exactly triggers this: LIVE CHECK 10 |
| `zen.glance.enabled` = false | none of the above (the click behaves as the normal Alt-click) |

Details of the click (child actor):
- Only a **primary-button** click on a link (or a node inside one) with **exactly one** modifier pressed, and it is
  the one configured (`event.ctrlKey ^ altKey ^ shiftKey ^ metaKey` odd parity is required, then the configured modifier is
  required). A drag longer than **4 px** between mousedown and click cancels it. Skipped when the event was
  already `defaultPrevented`, when the URL fails the security check (`javascript:` is blocked, the principal
  must be allowed to load), so unsafe links do nothing Glance-specific.
- On a successful activation the click is `preventDefault`ed and `stopPropagation`ed: no normal Alt-click (download) happens.
- On **mousedown** (every link press) the actor records the clicked node's rectangle (zoom-scaled) as
  `lastLinkClickData`; that is the start rectangle for the animation. If the click gives no rectangle (menu item,
  search) the previous recorded one, or a point at the window centre, is used.

Guards: the URL must be a string, non-empty, with a triggering principal that may load it; otherwise nothing happens.

## 3. The opening animation (350 ms)

`zen.glance.animation-duration` = **350 ms** (0 in tests, instant with `gReduceMotion` where noted).

1. The new tab is created (`skipAnimation`, background notify off), attached under the parent, **selected**, and the
   parent keeps showing (it is "visually selected").
2. **Element preview**: when the click rectangle is known, a screenshot of the clicked element is taken and shown
   as `.zen-glance-element-preview` (an `<image>` centred in the card, no pointer events). The real page content is
   hidden (opacity 0) at first and **fades in over 350/4 = 87.5 ms** (ease-in-out) over the preview.
3. **Card motion** (`browserWrapper` keyframes, ease-in-out, 350 ms): from the link's rectangle (centre x/y,
   width/height) to the **centre of the content area, 80 % of its width x 100 % of its height**. 90 keyframes per
   path of translate x/y and scale x/y. The path is an **arc**: height = min(distance x 0.4, 30 px, 60 % of the free
   space above/below), bending down when there is more room below, otherwise up. Opening uses an **ease-out-back**
   curve (`c1 = 0.6`, slight overshoot), closing uses ease-out with exponent 6.
4. **Parent page behind** animates (spring, bounce 0.2, 350 ms): `scale 1 -> 0.97` and `opacity 1 -> 0.3`, shadow
   `--zen-big-shadow`, still rounded.
5. **Side buttons** (section 4) fade and slide in with a spring (0.2 s, no bounce), starting `350 ms - 0.2 s` after
   the open begins, so they land when the card does: opacity 0 -> 1, x from -20 pt (right sidebar: +20) to 0.
6. On finish: card size is fixed to 80 % x 100 %, `has-finished-animation`, `GlanceOpen` is dispatched on the child tab.
   While opening, `animatingOpen` blocks closing (except tab close); the page docShell stays active unless
   `zen.glance.deactivate-docshell-during-animation` (false).
7. Status panel (`#statuspanel`) is hidden during the animation.

## 4. Look when open

- Card: `.browserContainer` 80 % x 100 % of the content area, centred (`margin: auto`), page background opaque
  (`white` / `#111`), `.browserStack` clipped with `--zen-big-shadow` and radius
  `--zen-native-inner-radius / --zen-squircle-value` (corner-shape round); in `zen-no-padding` mode the card is square
  (0 radius) and the dimmed page behind gets radius 6 and separation 6.
- Parent page behind: scale 0.97, opacity .3, radius and big shadow; **it is not interactive** (the overlay covers it).
- **Side button column** (`.zen-glance-sidebar-container`): at the **outer edge** of the card, on the opposite side from
  the sidebar: `left: 100%` of the card for a left sidebar (right sidebar: `right: 100%`), `top: 15 px`, padding 12,
  gap 12, `max-width: 56`. Live-measured: three **32 x 32** round buttons, pitch 44, at y 35 / 79 / 123. Each
  button: `--size-item-large`, background `color-mix(rgb(24,24,24) / rgb(231,231,231) 96 %, accent)`, icon colour =
  accent, radius 999, padding 8, shadow `0 0 12px 1px rgba(0,0,0,.07)`; hover: lighter background + scale 1.02;
  press: scale 0.98 (0.05 s). In order: **Close** (x, `close.svg`, label "Confirm" hidden), **Expand** (`fullscreen.svg`),
  **Split** (`split.svg`; disabled when the active split group is full or a Glance on a full group).
- **Escape** inside the glanced page closes it (section 7). A click on the dimmed overlay outside the card closes it
  too (`onOverlayClick`: the click target is the overlay itself and not the content wrapper).

## 5. Expand to a full tab (Cmd+O, or the middle button)

`cmd_zenGlanceExpand` = **Cmd+O** (`accel+O`; the same command as the Expand button; also when the glanced page
requests DOM fullscreen: `onFullscreenEntered` -> expand). `fullyOpenGlance`:

1. The glance tab leaves the parent and takes a **normal tab row right after the parent** in the sidebar (open
   animation `animateItemOpen`), gets the active Space id and is not pinned. If the parent is in a folder, folder handling
   (`#handleZenFolderPinning`, not fully read) depends on `zen.folders.owned-tabs-in-folder` (default false).
2. The side buttons are removed at once. The glance attributes are cleared, the tab is **selected** and its state is
   flushed to the session.
3. Animation (skipped with reduced motion): the card grows to 100 % x 100 % and the content area does a **250 ms
   scale pulse `1 -> 1.005 -> 1`**; any split view is deactivated; then the overlay is cleaned up, `zen-glance-overlay` is
   removed, and the glance ID is deleted.
4. Result: a normal tab with the page, selected, the parent unchanged and no longer dimmed.

## 6. Split from Glance (the third button)

`cmd_zenGlanceSplit` -> `splitGlance`: `fullyOpenGlance({ forSplit: true })` (no pulse animation), then
`gZenViewSplitter.splitTabs([parent, glanceTab], "vsep", initialIndex)`: a **vertical split with the parent on the
left (sidebar on the left) and the glance tab on the right**, the **glance tab selected**
(`initialIndex 1`); with a right-hand sidebar the order is mirrored (glance on the left, `initialIndex 0`). Then
`animateBrowserDrop` animates the new pane in (unless reduced motion). Disabled when the parent's split view
already has 4 tabs (`maybeDisableOpeningTabOnSplitView`). Details of the split in `split-view.md`.

## 7. Closing

| Input | Zen does |
|---|---|
| Close button | `cmd_zenGlanceClose`: `closeGlance({ onTabClose: true })` |
| **Esc** in the page | Esc handler sends `ZenGlance:CloseGlance` with `hasFocused` = a page element other than `body` is focused. See confirm below |
| Click on the dimmed area outside the card | `closeGlance({ onTabClose: true })` |
| **Cmd+W** with the glance selected | closes the **glance** (not the parent) |
| Closing the parent tab | closes the glance first (`onTabClose` on parent) |
| Tab-cycling (next / previous tab) | `getFocusedTab`: direction < 0 = the **parent**, otherwise the glance tab (tests `next_tab` / `prev_tab`): the glance and its parent act as a pair when stepping; exact keys: LIVE CHECK 11 |

**Confirm step**: if Esc/close arrives while an element inside the glanced page has focus (a text field, etc.)
(`hasFocused`) and there is a close button, the **first** Esc doesn't close: the Close button turns **red**
(`rgb(220,53,69)`, white icon) and expands to show **"Confirm"** (max-width 4 rem, 0.2 s), for **3 s** (then reverts); a second
Esc / click within 3 s closes. Without focus (page body focused) Esc closes at once.

Also `closeGlance` first asks the page's `beforeunload` (`permitUnload`): if the page refuses the close does nothing
(unless `skipPermitUnload`). While a glance is animating open, closing only works via tab close.

**Closing animation**: the arc sequence runs back (ease-out, exponent 6) from the 80 % x 100 % card to the **recorded
link rectangle**, 350 ms, the overlay `fade-out`s (`.browserStack` opacity 0.15 s), the side buttons fade out
(0.2 s spring), the parent page animates back to scale 1 / opacity 1 (spring). The glance tab is moved after the
parent in the tab list, hidden, then removed. When it was the **only tab** in the window, a new tab opens instead of
leaving the window empty.

After close: the **parent tab is selected** (the original tab). If the glance was closed because the parent was
closed, normal close behaviour takes over.

## 8. Edge cases worth copying

- Only one Glance at a time; a second Option-click while one is open does nothing special.
- The glance is **not** a sidebar row; its parent shows it as a child.
- Alt-click on non-links or on `javascript:` links: no Glance.
- Quitting the app removes open glance tabs without animation.
- The glance tab is never pinned; Expand always creates an unpinned tab.
- Splitting a tab that has a Glance uses the **parent** (`getTabOrGlanceParent`) everywhere (split view, pinning, folders).
- External-link Glance from a pinned tab only for sites on another host and app-tab pins.

## 9. Prefs

| Pref | Default |
|---|---|
| `zen.glance.enabled` | true |
| `zen.glance.activation-method` | `alt` |
| `zen.glance.animation-duration` | 350 (ms) |
| `zen.glance.enable-contextmenu-search` | true |
| `zen.glance.open-essential-external-links` | true |
| `zen.glance.deactivate-docshell-during-animation` | false |

Constants: background scale 0.97, parent opacity 0.3, card 80 % x 100 %, arc <= 30 px (ratio 0.4), 90 keyframes, expand pulse 250 ms (1.005),
confirm timeout 3000 ms, drag threshold 4 px, button column offset top 15 / gap 12 / 32 px buttons.

## 10. LIVE CHECK list

1. Frame timings of open and close (60 fps recording): confirm ~350 ms and the overshoot at the end of the opening.
2. Option-click vs. Cmd/Ctrl/Shift-click with the default pref: nothing else triggers it.
3. Esc with focus in an input: red Confirm state (3 s) then close; Esc with no focus closes at once.
4. After close: which tab is selected, the sidebar child row, and whether the parent flickers.
5. Expand (Cmd+O): where the new row appears (right after the parent?), pulse visible?
6. Split button: the layout of the result (parent left, glance right, which selected).
7. Button column at the right edge and its position with a right-hand sidebar; the disabled split look.
8. Clicking the dim area closes; clicking the parent page behind (outside the overlay) does nothing.
9. Link from a pinned tab to another site opens a Glance (needs `isAppTab` pinned tab).
10. Which UI triggers the `where = tab` search Glance (section 2 table, search row).
11. Which keys step to the parent / glance tab while a Glance is open.

## 11. Test script (Mac loop; Zen and Koan)

1. Open a page with links. Option-click a link: a card grows from the link to 80 % x 100 % centre (350 ms), the page
   behind shrinks to 97 % and dims; three round buttons appear to the right of the card after ~150 ms.
2. Hover each button (scale 1.02), press (0.98). Click **Close**: reverse animation back to the link rectangle; the
   parent is selected; the glance tab is gone from the sidebar.
3. Open again; click the dimmed margin: closes. Open again; press Esc: closes (page body focused).
4. Open a Glance of a page with a text field, focus it, press Esc: Close button turns red with "Confirm" for 3 s;
   Esc again: closes.
5. Open; Cmd+O: the glance becomes a normal tab after the parent, selected; the page is dimmed no more.
6. Open; click Split: two panes, parent left, glance right selected; the pane gap equals the split view's.
7. Open; Cmd+W: closes the glance only. Open; close the parent tab: both close.
8. Set the trigger pref to `ctrl`: Option-click is now a normal Option-click; Ctrl-click opens a Glance.
