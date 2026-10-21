# Review round 2: PR #56, compact mode variants, tabs on the right, collapsed sidebar (Fixes #35, #20)

Branch `feat/zen-compact-layouts` at `fff1785` (6 commits over `origin/develop` `43de34b`, rebased onto #55). I read the code and ran nothing: no build, no e2e (the shared build lock is in use), and the branch was not modified. The author's claim that all 17 e2e tests pass on the rebased tree is not something I checked.
References: round 1 (`.cache/reviews/pr56-review.md`), the author's reply (last PR comment), Chromium 158 at `chromium/src` HEAD `67e29b82`, and Zen 1.23.3b in `.cache/zen-src`.
`src/` line numbers are branch HEAD. Upstream line numbers are from the patched file (`git show HEAD:<path>` plus the branch patch, applied in a scratch directory).
"Uncertain" means derived from code reading only, with a plausible but unconfirmed runtime effect.

## Result

No blockers. 2 should-fix, 7 nits. Safe to merge, with the two should-fix items fixed or ticketed first.
B1 and S1 to S8 are fixed as described, apart from the S3 side effects below, and the Mac fallback (S8) is safe to compile. The new-window item (R2-S1) shows the B1 symptom, single toolbar lost in compact mode, in a flow the new e2e does not cover.

## Round 1 findings: verification

| Item | Verdict | Notes |
|---|---|---|
| B1 | Fixed | `IsSidebarCollapsedByUser()` (`koan_compact_mode_controller.cc:778`) returns `*collapsed_before_` whenever it has a value. `collapsed_before_` is set only in `Activate()` (`:892`, while compact mode hides the sidebar) and reset in `Deactivate(false)`, so the "before" answer is the same before and after the `Deactivate(true)` + `Activate()` churn. I traced both round 1 scenarios (tabs-on-right toggle, Ctrl+Alt+B multi to single) and both now compute single toolbar. `ToggleSingleToolbar` (`:309`) was switched from `hides_sidebar()` to `collapsed_before_`, which is equivalent in the one state where it matters. `compact_single.mjs` covers both. A remaining hole is in R2-S1. |
| S1 | Fixed | `IsSidebarOnRight()` (`koan_browser_view_controller.cc:154`) requires `IsSidebarEnabled()`, `GetIsNormalType()` and `ShouldDrawVerticalTabStrip()`. The layout hook, the corner views, the clip insets, the strip's resize area, the header mirror, the card insets and the toast all go through it. Not gated: the raw `sidebar_on_right_` cache, still used by `LayoutHotZones`, `IsCaptionButtonBesideSidebar` and `IsHideToolbarOnlyBlocked`. The first two only run when `mode_ != kNone`, which already implies a vertical-tab window (`UseContentCard`). The third is R2-S2. |
| S2 | Fixed | `GetRevealedToolbarSidebarInset` (`koan_browser_view_controller.cc:170`) is nonzero only when compact mode hides the toolbar but not the sidebar. The layout patch insets the separated top container's params, lays it out, and mirrors its bounds and children. The repo's frame dump (`compact-revealed-toolbar.png`) and `compact_variants.mjs` show the toolbar beside the sidebar. See N2 for what is still untested. |
| S3 | Fixed, with side effects | The predicate is now pref-independent and `IsCommandIdEnabled` uses it. `FixBlockedCompactVariant` rewrites the shared pref in more cases than Zen's menu-time check does. See R2-S2. |
| S4 | Fixed | `menu_runner_.reset()` runs first (`:486`). The runner is declared after both models in the header, so it is also destroyed first. `~MenuRunner` can no longer touch a freed model. |
| S5 | Fixed for every path I could reach, with one gap | See "Lock lifetime" below and N1. |
| S6 | Fixed | Left and right clip insets are swapped before `SetTargetContentBounds` (tabbed patch, patched file `:1320-1326`). `koan_mirror` is declared at function scope (`:999`) in the same function, and `CalculateProposedLayout` is only called from `Layout()`, so the swap and the mirror always agree. |
| S7 | Fixed for drags that start expanded | `IsResizing()` makes the layout stay put until release. The patch evaluates `koan_started_expanded` before `starting_width_on_resize_` is cleared (patched file `:760`). A drag that ends collapsed calls `OnSidebarDragCollapsed` after `SetIsResizing(false)`. The collapse callbacks only post `UpdateActive`, so `collapsed_by_drag_` is set before any of them run. See N3 for drags that start collapsed. |
| S8 | Fixed (reverted) | The Mac `#if` that kept the band is gone. Behaviour is develop's. There is no new Mac-only code, so the compile risk is unchanged. The new `GetCaptionButtonAreaSize(view, leading)` uses `frame->GetBrowserLayoutParams()` like the existing overload. Docs match. Nobody has smoke-tested Mac. See N6. |
| S9 | Mostly addressed | The paths that cannot be tested headless (S3, hover-only menu) are explained in the PR. |
| N1, N2, N3, N7, N8, N9, N10 | Fixed | N4 is fixed only for the strip's two corner views and the top container's corners, as the author says. N5, N6, N11: not done, accepted. |

### Lock lifetime (S5), path by path

- Menu closed by selection or dismissal: `MenuRunnerImpl::OnMenuClosed` notifies the adapter, which runs `OnContextMenuClosed`, which resets the lock. The command runs first, then the lock goes, so a "Tabs on the right" toggle happens while the sidebar is still held.
- Window closed while the menu is open: the owner widget going away takes the normal `ExitMenu` path, which calls the callback. Otherwise `OnBrowserViewDestroying` resets the runner (`:282`). That path swaps in an empty delegate, so the callback does not run, but `DestroyFor` clears the property right after, which destroys the controller and so `menu_lock_`, while the `VerticalTabStripRegionView` is still alive (the hook is at the top of `~BrowserView`). `VerticalTabStripExpandOnHoverLock` also clears its region pointer if the region dies first (`vertical_tab_strip_region_view.cc:243`). No dangling pointer, no leak.
- `ShowContextMenu` called again: the old lock is replaced after the new one registers, so the counts stay right.
- `RunMenuAt` returns without showing: the lock is not released. See N1.

### Hook patches

- All five apply with `git apply --check` to pristine `git show HEAD:<path>` files (scratch dir `/tmp/r2apply.*`), and all five apply together.
- With Koan's flags off they change nothing:
  - `IsSidebarOnRight` and `GetRevealedToolbarSidebarInset` return false and 0.
  - `InsetHorizontal(0)` and `InLocalCoordinates(visual_client_area)` are no-ops here, because `GetBrowserLayoutParams(use_browser_bounds=true)` already returns params with origin (0,0).
  - `SetOrientation` is called with the orientation the corner views were constructed with (`browser_view.cc:1007-1022`), and `SetOrientation` returns early when equal.
  - The corner swap and the clip-inset swap are guarded.
  - The resize area keeps its original x.
- The three `BUILD.gn` hunks are one new dep line each, in a header-only target (`//koan/browser/ui/views:views`), so no cycle: `:impl` is separate. Not checked with `gn`.
- `git diff` shows three patches regenerated without their `index` lines and `@@` section headers. That is harmless for `git apply` but noisy for rebases (N4).

### #47 (no timers or paints while idle)

Holds. New timers: `flash_timer_` (one-shot, 800 ms, started only on a tab change with the flash pref on, stopped in `EndFlash`/`Deactivate`). The menu lock adds no timer. `UpdatePointerMonitor` still creates an `EventMonitor` only while the toolbar, the caption strip or a peek is out. `CheckSidebarPeek` returns at once without a peek lock. The new per-layout work (`UpdateCaptionButtons`, `LayoutHotZones`, `IsSidebarOnRight` lookups) runs only on layouts and starts an animation only when an opacity differs. `OnBrowserViewLayout` can post `UpdateActive` only while `mode_ != DesiredMode()`, and the desired mode does not depend on anything `Activate` toggles, so I found no 2-cycle. `FixBlockedCompactVariant` writes at most once: the pref becomes `sidebar`, after which `PrefHidesSidebar()` is true.

---

## Blockers

None.

---

## Should fix

### R2-S1. A window created while compact mode is on reads compact mode's own collapse as the user's, and drops single toolbar

- **Where:**
  - `koan_compact_mode_controller.cc:778-793` (`IsSidebarCollapsedByUser`), `:870-894` (`Activate`, which sets `collapsed_before_` from the live state).
  - Upstream: `vertical_tab_strip_state_controller_impl.cc:253-258` (`NotifyCollapseChanged` calls `UpdateSessionService()` and `UpdatePrefService()`, which writes `kVerticalTabsCollapsedState`) and `browser_window_features.cc:600-620` (a window not created by session restore starts from that pref).
- **What's wrong:** `Activate()` collapses the sidebar, and upstream persists a collapse as the profile's sidebar state and in the session. Koan stores the "before" state only in the window's memory (`collapsed_before_`). The new window's state controller starts collapsed, so `IsSidebarCollapsedByUser()` has nothing but the live state to read and returns true.
- **Failure scenario (defaults: single toolbar on, compact hides the sidebar):**
  1. Window A: Ctrl+S. The sidebar collapses; when the animation ends the profile pref records "collapsed".
  2. Ctrl+N. Window B starts with a collapsed sidebar. B's first `UpdateActive` runs `Activate(kCompact)`: `collapsed_by_user` is true, so `DesiredSingleToolbar()` is false. B shows compact mode with the toolbar above the page and no nav row or URL pill in the sidebar, while A shows single toolbar. `collapsed_before_` is true, so Ctrl+S in B leaves the sidebar collapsed (the collapsed layout) instead of restoring single toolbar.
  3. Same after a crash or a kill, or any restart where Koan did not get to run `Deactivate(false)` before the session was saved. A normal quit probably recovers: `Deactivate(false)` expands the strip and upstream calls `SetCollapsed(false)` at the start of an expand animation (`vertical_tab_strip_region_view.cc:264`). Uncertain.
  4. Develop was not affected, since its single toolbar did not depend on the collapsed state. This is new with the collapsed layout.
- **Why the tests pass:** every e2e runs one window.
- **Fix:** Keep the user's pre-compact state outside the window: a profile pref written when compact mode first collapses the sidebar and cleared when it restores it, and read by a window that starts with compact mode already on. Alternatively, treat a collapsed sidebar at a window's first activation with compact on as "not by the user". Add a Ctrl+N step to `compact_single`: single toolbar and the nav row are present in the new window.

### R2-S2. `FixBlockedCompactVariant` rewrites the shared pref from every window, in states where compact mode does not apply

- **Where:** `koan_compact_mode_controller.cc:809-829` (`IsHideToolbarOnlyBlocked`, `FixBlockedCompactVariant`), called at `:837` (`UpdateActive`) and `:352` (`OnTabsOnRightChanged`). Docs: `docs/USAGE.md` ("neither does Koan"), `docs/ZEN_PARITY.md`.
- **What's wrong:**
  - It runs first in `UpdateActive`, before `DesiredMode()` is checked. It runs in every `BrowserView` (the controller exists for all of them), whatever the feature flag, window type or tab-strip mode, and it uses the raw `sidebar_on_right_` and that window's own collapsed state.
  - The pref `koan.compact_mode.hide` is profile-wide and every window observes it, so any window that is "blocked" writes it back to `sidebar` whenever anything triggers `UpdateActive` (a pref change, a collapse change, a layout mismatch).
  - Zen rewrites at the same moments (`checkIfIllegalState`), but Zen has one window state per pref and only runs it while building the compact context menu. Koan's trigger set is wider.
- **Failure scenarios:**
  1. Settings: the user picks "Hide Top bar" on the settings page in a window whose sidebar is collapsed with window buttons on its side (Mac with the sidebar on the left, or GNOME buttons with tabs on the right). The pref write wakes this window's `UpdateActive` and the choice reverts to "Hide Tab bar". The Settings row has no disabled state.
  2. Two windows: window A is collapsed and blocked, window B is not. In B the user picks "Hide toolbar" in the menu (enabled there). A's observer runs, sees A as blocked, and rewrites the pref to `sidebar`. B's choice is gone. Same on any other pref change, so a user who prefers toolbar-only loses the choice the first time they collapse the sidebar in any window, and expanding it again does not restore it.
  3. Compact on, hiding only the toolbar, on Mac or with buttons on the sidebar's side. The user clicks the sidebar's collapse toggle. The collapse callback runs `UpdateActive`, the pref becomes `sidebar`, and the sidebar the user just collapsed to icons disappears while the toolbar comes back. Zen does the same, but nothing in Koan's docs says so.
  4. The check also runs in a window using horizontal tabs, or with `--disable-features` for compact mode off, if that window's persisted collapsed state is true (the pref stays set when the user switches the tab-strip mode) and tabs-on-right puts the buttons on the "sidebar's" side. The pref is rewritten from a window that has no sidebar.
- **Fix:**
  - Run it only when the window can use compact mode: `features::IsCompactModeEnabled()` and `UseContentCard()`, and only when compact mode is on or being turned on, not on every update.
  - Gate it per window, not per profile, or do not rewrite the pref at all. Keep the pref as the user's choice and have `DesiredHideToolbar()` fall back while blocked, as the code did before this round. Then only `IsCommandIdEnabled` needs the pref-independent test.
  - If the rewrite stays, say so in `USAGE.md` and make the Settings row show the item disabled.

---

## Nits

- **N1. The menu lock is not released when `RunMenuAt` returns without showing a menu.** `koan_compact_mode_controller.cc:511-521`. The lock is taken before `RunMenuAt`. `MenuRunnerImpl::RunMenuAt` returns without notifying the adapter when another views menu is active (`menu_runner_impl.cc:161`, `CancelAllActive` then `return`) or when the application is shutting down (`menu_runner.cc:50-54`, `IsShuttingDown`). Then `OnContextMenuClosed` never runs, and the sidebar stays held by `kKeepCurrentState` (stuck expanded over the page in compact mode) until the next menu or the window closes. A click on the sidebar while another menu is open is normally eaten by that menu, so I could not construct a pointer path; keyboard or touch might. Fix: after `RunMenuAt`, `if (!menu_runner_->IsRunning()) menu_lock_.reset();` (on Mac the call blocks and the callback has already run). Also reset `menu_lock_` in `OnBrowserViewDestroying`, next to `menu_runner_.reset()`, so the release does not depend on the controller's destructor order.
- **N2. The revealed toolbar's inset is one layout pass behind, and untested with tabs on the right.** `koan_browser_view_controller.cc:170-181` reads `region->width()`, the bounds from the previous pass (the sidebar is laid out later in the same `Layout()`), and that width includes the expand-on-hover overlay (`browser_view_tabbed_layout_impl.cc:902-914`). Hovering a collapsed sidebar narrows a revealed toolbar by the overlay's growth, and while the sidebar animates the toolbar's edge trails it by a frame. Probably converges on the last pass, but I did not confirm that a pass follows the final width. `compact_variants` checks only the left-sidebar case; the right-sidebar plus revealed-toolbar geometry (the inset plus both mirrors in `browser_view_layout_impl.cc.patch`) has no test. By my reading the maths is right. Fix: use the width from the layout being computed, or add a one-case e2e (tabs on the right, hide toolbar, reveal).
- **N3. `IsResizing()` also flips the mode for drags that start collapsed.** `koan_compact_mode_controller.cc:784-788`. It only matters with expand-on-hover turned off in Chromium's settings, because otherwise a collapsed strip has no visible resize area (`vertical_tab_strip_region_view.cc:790`, set when a resize ends). Then the first drag step from icons makes `DesiredMode()` single toolbar, and the nav row and URL pill are installed in a still-narrow strip. Releasing collapsed removes them again. The fix for S7 only needs to cover drags that start expanded, e.g. record that at the first `OnResize` instead of using the blanket flag. Uncertain.
- **N4. Patch hygiene.**
  - `BUILD.gn.patch`, `browser_view_tabbed_layout_impl.cc.patch` and `vertical_tab_strip_top_container.cc.patch` lost their `index` lines and `@@` section titles, which adds noise to the next rebase diff.
  - The `browser_view_layout_impl.cc.patch` hunk rewrites four upstream lines in place (`params`, `new_params`, the `SetBoundsRect` call) instead of only adding lines; this is the likeliest conflict on the next Chromium bump (`docs/REBASE.md` already lists it).
  - `std::swap` is used in the tabbed patch (patched file `:1576`) without an `#include <utility>`; it only builds through a transitive include.
- **N5. Unrelated `.gitignore` change.** `/depot_tools/` and `/chromium/` became `/depot_tools` and `/chromium` (so a `chromium` symlink is ignored). Harmless, but not part of this PR's topic.
- **N6. Mac stays unverified.** Nobody has run this on macOS. The author documents (`ZEN_PARITY.md`, `USAGE.md`, #51) that the traffic lights sit over the page's top-left corner whenever the sidebar is hidden or on the right. In the default Mac layout with tabs on the right and single toolbar, they overlap the page permanently, because `HasPageSideCaptionButtons()` is false on Mac and the strip is always collapsed to 8 px. That is the stated choice, not a defect. The GN dep added to `browser_view_layout_impl` was not checked with `gn gen` (header-only target, so a cycle is unlikely). A Mac smoke test of tabs-on-right plus compact is still owed.
- **N7. Two sources of truth for the side.** `sidebar_on_right_` (the pref) is used for the hot zones, `IsCaptionButtonBesideSidebar`, the menu check and `IsHideToolbarOnlyBlocked`, while everything else uses the gated `koan::IsSidebarOnRight()`. They agree wherever compact mode is active (R2-S2 covers where they do not). One accessor would remove the question.
