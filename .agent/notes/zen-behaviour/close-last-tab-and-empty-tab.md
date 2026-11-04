# Closing the last tab, and the empty tab

Zen 1.24b. Source: `src/zen/spaces/ZenSpaceManager.mjs` (`selectEmptyTab`,
`#initializeEmptyTab`, `handleTabBeforeClose`, `handleTabBeforeRemove`,
`makeSureEmptyTabIsFirst`, `#changeToEmptyTab`, `handleTabCloseWindow`),
`src/browser/components/tabbrowser/Tabbrowser-sys-mjs.patch`,
`src/browser/base/content/browser-commands-js.patch`,
`src/browser/components/urlbar/content/UrlbarChildController-mjs.patch`,
`prefs/firefox/browser.yaml`, `prefs/zen/zen.yaml`, `prefs/zen/workspaces.yaml`.

## 1. What the "empty tab" is

- One hidden `about:blank` tab per window with the attribute **`zen-empty-tab`**,
  created at startup (`#initializeEmptyTab`: `addTrustedTab("about:blank",
  {inBackground, _forZenEmptyTab, skipAnimation})`, or the one restored from the
  session store, `tabData.zenIsEmpty`). Tracked as `gZenWorkspaces._emptyTab`.
- It is **never listed**: excluded from the tab strip, Ctrl-Tab / tab search, the
  tab-list menus, the extension API (`tabs.query` ignores it), "close other tabs",
  the find bar (disabled on it), the "last tab in window" test, pin/unpin
  (`cmd_zenTogglePinTab` ignores it) and from folders' tab counts. A *folder* also
  owns its own pinned `zen-empty-tab` (its header anchor): that is a separate
  thing, do not mix them up.
- It sits **first in the active Space's tab container** and carries that Space's
  `zen-workspace-id` (`makeSureEmptyTabIsFirst`, called on every Space change), so
  it is re-parented to whichever Space is shown.
- Its document is a **transparent** blank page (`_forZenEmptyTab` forces
  `allowTransparentBrowser`): the window glass shows through the page card (seen
  live: `glass-06-active-blank-page-card.jpg`).
- Selecting it sets `zen-has-empty-tab="true"` on `:root` **and** on the sidebar
  toolbox (`#changeToEmptyTab`, on every tab location change). Effects:
  - the URL pill shows its identity/page-action buttons permanently
    (`:root[zen-has-empty-tab] ...opacity:1`, `zen-omnibox.css`), and `pageproxystate`
    is `invalid`, so the pill is **blank** (no URL);
  - in compact mode the sidebar **cannot hide** while it is set
    (`#navigator-toolbox:is([zen-has-empty-tab], ...)` in `compact-mode/sidebar.inc.css`
    and `ZenCompactMode.mjs`), so the user always sees the sidebar and URL pill;
  - `supress-primary-adjustment` is off (the URL bar select/focus logic treats the
    toolbox as visible).
- **Anything typed or navigated from it opens in a NEW tab**
  (`UrlbarChildController.getWhere` returns `"tab"` when the selected tab is
  `zen-empty-tab`): the empty tab itself never navigates.

## 2. When Zen selects the empty tab

| Trigger | Condition | Source |
|---|---|---|
| Window opens with nothing to restore, or the startup page was a blank/home page | `zen.urlbar.open-on-startup` (true) -> the URL bar session opens at once: `BrowserCommands.openTab()` (floating Cmd+T bar) when `zen.urlbar.replace-newtab` is on, else `openLocation()` | `ZenSpaceManager` ~1090-1135 |
| **Closing the last tab** of the window | `browser.tabs.closeWindowWithLastTab = false` (Zen's default, `prefs/firefox/browser.yaml`) | `handleTabBeforeClose`, `Tabbrowser.removeTab` -> `newTab` -> `selectEmptyTab(BROWSER_NEW_TAB_URL)` |
| Closing the last **unpinned** tab while pinned tabs/Essentials exist | only if `zen.workspaces.open-new-tab-if-last-unpinned-tab-is-closed` (default **false**) | `handleTabBeforeClose` (`tabsPinned.length === 1`) |
| Creating a Space that has no tabs for it (`#prepareNewWorkspace`), or switching to a Space whose last-selected tab is gone / that has none | always | `ZenSpaceManager` ~1882, ~2641 |
| A startup page was removed because it duplicated the empty state | session start only | `ZenSpaceManager` ~1055-1090 |

`selectEmptyTab` reuses `_emptyTab` when it is alive **and** `zen.urlbar.replace-newtab`
is true; otherwise it opens a real tab on the homepage (`gZenUIManager.openAndChangeToTab`,
`browser.startup.homepage`'s first URL) and stamps it with the active Space id.

## 3. Closing a tab: the cases

Closing = Cmd+W, the tab's close button, middle-click, "Close Tab" in a menu.

1. **Other visible tabs remain** (the "visible" set = the tabs of the active Space
   plus Essentials plus pinned tabs; the empty tab does not count): normal behaviour,
   the successor is selected: the owner tab if `browser.tabs.selectOwnerOnClose`,
   otherwise the most recently used (`zen.tabs.select-recently-used-on-close`,
   default true), else the neighbour. Close animation: the row collapses
   (`gZenVerticalTabsManager.animateItemClose`, skipped under reduced motion).
2. **It is the only visible tab in the window** (`tabs.length === 1`): because
   `browser.tabs.closeWindowWithLastTab` is **false**, the window is **not** closed:
   the tab closes and the **empty tab is selected** (`Tabbrowser.#isLastTabInWindow`
   ignores the empty tab). The sidebar shows the Space, the pill is blank, the page
   card is the transparent glass. Nothing is auto-opened (no URL bar) at this
   point; the user presses Cmd+T / Cmd+L / clicks the pill. (`shouldCloseWindow()`
   is also true when the window has no toolbar, or with Spaces disabled in a
   non-private window: then `_isClosingWindow` is set and `cmd_closeWindow` fires
   100 ms later, `handleTabBeforeRemove`.)
3. **Last *unpinned* tab with pinned/Essentials present**: by default (pref false)
   the pinned/Essential tab (or whatever the normal successor logic picks) is
   selected, no empty tab. With the pref **true** the empty tab is selected instead
   and the pinned tabs stay untouched.
4. **The tab being closed is not the selected one**: nothing else changes.
5. **A Glance tab** closing: tears the overlay down and re-selects its parent; the
   last-tab logic is skipped (`glance-id` guard).
6. **Split view**: see the empty-tab Cmd+W rule below (no-op while the empty tab is part
   of a split group).
7. **beforeunload / "leave page?"**: runs before any of this; if cancelled the tab
   stays.
8. **Undo close tab** (Cmd+Shift+T) restores the closed tab into its Space (and its
   folder / pinned state); the empty tab is not a restorable tab (session store
   skips it: `SessionStore ... aTab.hasAttribute("zen-empty-tab")`).

## 4. Cmd+W (and File > Close Tab) while the empty tab is selected

`BrowserCommands.closeTabOrWindow` (patched, before the pinned-tab special case):
- selected tab has `zen-empty-tab` **and** `split-view` -> **nothing happens**;
- otherwise -> `gZenWorkspaces.handleTabCloseWindow()`: if `zen.tabs.close-window-with-empty`
  (default **true**) -> run `cmd_closeWindow` (**the window closes**, via the normal
  window-close path: closed-windows list, session save); if the pref is false ->
  nothing happens.
So the usual sequence is: Cmd+W (last tab closes, empty tab shown) -> Cmd+W again
(window closes). On macOS the app keeps running with no window.

## 5. What the user can do from the empty tab

| Input | Result |
|---|---|
| Cmd+T | toggles the floating URL bar session (see cmd-t-cmd-l-session.md); Enter opens a new tab |
| Cmd+L / click the pill | opens the URL bar; any result opens in a **new tab** (never in the empty tab) |
| Cmd+N | new window; Cmd+Shift+N blank window; Cmd+Shift+P private window |
| Cmd+F | nothing (find bar disabled) |
| Cmd+Shift+D (pin) | ignored |
| Ctrl+Tab | skips it (never offered) |
| Drag a tab onto the sidebar | normal; the empty tab is not a drop target (the empty-tab guard in `ZenDragAndDrop.js`) |
| Switch Space | the empty tab moves to that Space's container (first position) and is selected only if that Space has no other tab to show |
| Quit and relaunch | session restore restores the empty tab. The source (`ZenSpaceManager.mjs` ~1100) calls `BrowserCommands.openTab()` when `zen.urlbar.open-on-startup` is true, but **live (2026-11-04, fresh profile launched with Marionette, window focused) the floating URL bar was NOT open** after 8 s: only the empty tab and the visible sidebar. See `live-results-2026-11-04.md` |

## 6. Prefs

| Pref | Default | Meaning |
|---|---|---|
| `browser.tabs.closeWindowWithLastTab` | **false** (Zen) | false -> closing the last tab selects the empty tab instead of closing the window |
| `zen.tabs.close-window-with-empty` | true | Cmd+W on the empty tab closes the window |
| `zen.workspaces.open-new-tab-if-last-unpinned-tab-is-closed` | false | closing the last unpinned tab shows the empty tab even if pinned tabs remain |
| `zen.urlbar.replace-newtab` | true | the empty tab / floating bar replaces the "new tab page"; false -> a real new tab is opened |
| `zen.urlbar.open-on-startup` | true | open the URL bar when the window starts on the empty tab |
| `zen.tabs.select-recently-used-on-close` | true | successor choice when closing |
| `zen.tabs.close-on-back-with-no-history` | true | Back with no history closes the tab (and so can lead here); `ZenCommonUtils.mjs` ~160 |

## 7. LIVE CHECK (to verify with a fresh Zen when the screen is free)

1. Close the only tab: confirm the window stays open, the pill is blank and the
   sidebar shows the Space; time from key to empty state (expect the close animation
   only).
2. Same with Essentials present and the pref false: which tab is selected.
3. Cmd+W on the empty tab with the pref true: window closes; what the Dock/menu
   show on macOS (no windows, app alive).
4. Closing the last tab in a **private** window.
5. Whether the URL bar opens automatically after "close last tab" (the source says
   no; confirm).

## 8. Test script for the Mac loop (run in Zen and in Koan)

1. Fresh profile, one tab open. Cmd+W. Expect: window still open, sidebar visible,
   blank pill, glass page card, no tab rows (except Essentials).
2. Cmd+W again. Expect: the window closes (app keeps running).
3. New window; open 3 tabs; close them one by one with Cmd+W; after the last, the
   same empty state as step 1.
4. Pin one tab, open one more, close the unpinned one: expect the pinned tab is
   selected (pref default). Set `zen.workspaces.open-new-tab-if-last-unpinned-tab-is-closed`
   true and repeat: expect the empty state with the pinned tab still in the sidebar.
5. From the empty state press Cmd+T, type `example.com`, Enter: a **new** tab opens
   and the empty state is gone; close it: back to the empty state.
6. From the empty state: Cmd+F does nothing; Ctrl+Tab does not show it.
7. Quit/relaunch from the empty state: window restores on the empty tab (verified live; compact mode
   also restores). The floating URL bar was **not** open live although the pref is true: Koan should match the
   shipped behaviour only after this is re-checked in the user's real Zen (not a Marionette launch).
