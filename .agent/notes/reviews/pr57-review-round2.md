# Round-2 review of PR #57 "Spaces like Zen's" (issue #34)

Branch `feat/zen-spaces` at 756babd (2 commits on develop 6822da6 = #58), diff `origin/develop...HEAD`.
Nothing was built or run (shared build lock). Everything below is from reading the diff against Chromium 158
(`~/work/koan/chromium/src`, `git show HEAD:<path>`), the round-1 review and the author's reply. Mac paths were
read only. #59 was read at origin/feat/zen-pinned-essentials (eb454f1), and the two branches were merged with
`git merge-tree` (object database only, nothing checked out).

Verdict: **safe to merge into develop** (the branch merges cleanly with develop, no blockers). It will conflict
mechanically with #59 (see S1), which is a rebase cost on whichever lands second, not a defect.

Counts: Blockers 0, Should fix 2, Nits 8.

---

## Verified: round-1 blockers

### B1 (explicit activation overridden) - fixed
- `SwitchTo()` (`koan_workspaces.cc:510-517`) now only calls `EnterFocusMode(space)`; `RestoreLastActiveTab()` and the
  posted second activation are gone.
- Explicit activation (switch-to-tab, tab search, `Target.activateTarget`): `TabStripModel::SetSelection`
  (`tab_strip_model.cc:4252-4270`, upstream) leaves focus mode because the new active tab is not valid in the focused
  group. `EnsureOneSpaceShown()` (`:1095-1107`) then calls `SwitchTo(active->GetGroup())`. In `EnterFocusMode`
  (`:2258-2272`) the new `PickSpaceTab()` hunk sits inside the `active_tab()->GetGroup() != group` branch, so with the
  active tab already in the group it is not consulted and the chosen tab stays. Traced end to end; no path overrides it.
- `last_active_tabs_` is recorded in `OnTabStripModelChanged` (`:1237-1246`) from `selection.old_tab` when the group
  changes, so leaving by explicit activation records the old Space's tab correctly.
- Test: `spaces_switching.mjs` activates A's non-remembered `a2` while in C and asserts `a2` is shown. It exercises the
  right code (DevTools `activateTarget` -> `ActivateTabAt`). Omnibox "Switch to tab" and back-to-opener use the same
  `ActivateTabAt` entry.

### B2 (swipe/wheel past an unloaded Space) - fixed
- Every route to a Space ends in `TabStripModel::EnterFocusMode`: Koan's `SwitchTo`/`Cycle`/`SwitchToNumber`, the bar,
  upstream's swipe (`VerticalTabStripFocusSwipeController::RotateTabGroupFocus` -> `RotateFocusedGroup` ->
  `EnterFocusMode`), palette "Focus on". The patch makes it ask `koan::PickSpaceTab()` (`koan_workspaces.cc:1403-1413`)
  for the Space's remembered tab, so the group's first tab is never activated, so (`NavigationControllerImpl::SetActive`,
  `navigation_controller_impl.cc:5056`) never loaded on the way.
- Wrap: the `RotateFocusedGroup` hunk wraps first/last past the ends inside the same function, so there is no
  focus-off frame at the wrap point any more (`WrapsSpaces()` is null-safe for windows without Spaces).
- After a restart `GetLastActiveTab()` (`:528-555`) falls back to the tab with the greatest
  `WebContents::GetLastActiveTime()`. Discard keeps it (`tab_lifecycle_unit.cc:282`), session restore sets it
  (`CreateParams::last_active_time`).
- Unload is not undone by swiping: only the tab the user lands on loads.
- Tests: `spaces_switching.mjs` unloads the other Spaces, wheels A -> B -> C and asserts load deltas `b2 === 1,
  c2 === 1`, `b1/c1/a*` zero; the same after a restart; wrap both ways. Good. Limitation: on Linux the wheel goes
  through upstream's controller, not Koan's `SpaceWheelHandler` (Ctrl+wheel cannot be sent through UI DevTools); the
  Mac path is unrun. The shared tail (`RotateFocusedGroup` -> `EnterFocusMode`) is what carries B2, and it is tested.

## Verified: S1-S8

- **S1** `koan_tab_unload.cc`: `DiscardEligibilityPolicy::CanDiscard(page, URGENT, ignore_recent_visibility=true,
  &reasons)` exists with that signature (`discard_eligibility_policy.h`), the policy is installed unconditionally
  (`chrome_browser_main_extra_parts_performance_manager.cc:183`), build deps and header visibility are fine
  (`//chrome/browser/performance_manager` exports both headers). The reason list is sensible: audible, capture, PiP,
  USB/BT, DevTools, form/user edits, extension-protected, opted-out, PDF, web app and `kVisible` keep the tab; the
  recency/URL/pinned/notification reasons do not. Selected tabs (split partner) are kept by `IsTabSelected` and pending
  tabs are skipped (`:705-725`). Toast and docs updated. Fail-open nit: N6.
- **S2** `Update()` no longer calls `UpdateTint()`; calls remain only in `OnTabGroupFocusChanged`, a visuals change of
  the active Space, `OnTintPrefChanged` and `OnWidgetActivationChanged(active)`; `DecidesTint()` gates all of them to the
  active/last-active window. Correct. No multi-window test (acknowledged).
- **S4** `SpaceWheelHandler` is a `kSystem` pre-target handler on the whole `VerticalTabStripRegionView`
  (`EventTarget::GetPreTargetHandlers` sorts system before default, so it precedes upstream's). On Mac it reads
  `ui::ScrollEvent` with `GetMouseWheelTick120ths != 0` (notched wheels only; `Widget::OnScrollEvent` copies the event with
  its native event and skips the wheel conversion when handled, so `HasNativeEvent()` is valid and marking it handled is
  respected), no magnitude threshold, 200 ms cooldown, Ctrl+vertical, RTL honoured. Win/Linux: horizontal wheel stays
  with upstream, Ctrl+wheel is Koan's. Unrun on Mac.
- **S5** `kAccel | kAlt` for forward/backward and compact-mode show-sidebar (unchanged on Linux = Ctrl+Alt); Ctrl+1..9,0 on
  Mac only via `bind(VKEY_0 + number % 10)`; `SwitchToNumber` returns false when the Space does not exist, so Chrome
  keeps the key. High priority is correct: `ChromeCommandDispatcherDelegate prePerformKeyEquivalent` calls
  `eventHandledByViewsFocusManager(kHighPriority)` before `CommandForKeyEvent` (the Cmd+Option+arrow menu items). Only the
  two Space keys get high priority; `Run()` returns false (key passes on) when Spaces are off.
- **S6** Header: `FocusBehavior::ALWAYS`, role button + menu popup, focus ring, Enter/Space/menu key open the menu, F2
  renames, "..." button always in the focus order. Bar: radio group with checked state; `views::Button` installs a focus
  ring by default (`button.cc:675`). Announcement on switch via `AnnouncePolitely`. Tests for F2/Enter exist.
- **S7** `ShowsSpaceHeader()` asks the window's `KoanWorkspaceController::IsEnabled()` (not private). `tab_management.mjs`
  checks the group header in a private window. See N3 for a residual.
- **S8** `ReadSpace`/`WriteSpace` with `SpaceData{name, icon, theme}` are the only code touching
  `TabGroupVisualData` (`grep` finds it only at `koan_workspaces.cc:625-645`); create, rename, icon, theme, revival and
  snapshot all go through them. #41 can replace those two functions plus `SpaceTheme`/`TintSeed`/`KoanThemeMenu`.
- Nits fixed and checked: N1 (every emoji in `kEmojis` still passes `IsEmoji`; I checked all 201 entries against the
  list of text-default pictographs), N2, N6, N7, N8, N10.

## Verified: merged work still holds

- **#58** The "+" menu's New Tab runs `IDC_NEW_TAB`, which still goes through `MaybeOpenUrlBarForNewTab`, so with
  replace-newtab on it opens the URL bar (Zen's behaviour); `spaces.json` turns it off only for `spaces.mjs`. A committed
  URL opens a foreground tab through `Navigate`, which joins the focused group (`navigator/browser_navigator.cc:1040`);
  anything loose is adopted by `EnsureOneSpaceShown`.
- **#56** Header/bar are placed in LTR terms and mirrored by `GetMirroredRect`; tabs-on-right does not mirror sidebar
  contents (only the top row, `MirrorSidebarHeaderLayout`), so they agree. The form's `kKeepExpanded` lock is the same
  mechanism as compact mode's `PeekSidebar`. The collapsed-width check matches `kVerticalTabStripCollapsedWidth`.
  Compact-mode + Create form is not covered by a test.
- **#55** `AppendWorkspaceCommands` uses `GetSpaces()`; "New Space" runs `ShowCreateForm`; names are without icon.
  `palette.mjs` creates Spaces through the form.
- **#47** `SavesTabGroups()` keeps Spaces out of saved groups, so a closed window's tabs restore on demand; the listener
  patch returns before `AddGroup`, and the rest of the listener already tolerates groups without a listener
  (`OnTabGroupChanged`, `TabGroupedStateChanged`, `OnTabStripModelChanged` all check `local_tab_group_listeners_`).
  Start-up activates only the restored active tab's Space (`EnsureOneSpaceShown` -> `EnterFocusMode` keeps the active
  tab). Unload uses `EXTERNAL` discards and skips pending tabs.
- **#50** `UnpinnedTabsForClear` is unchanged and reads the focused group; `CloseUnpinnedTabs` still adds a tab into the
  Space before closing the rest, so Clear never removes a Space.

## Verified: patches

All six patches apply to pristine 158 files with `git apply --check -p1` (scratch tree, each file taken from
`git show HEAD:<path>`): `saved_tab_groups/BUILD.gn`, `saved_tab_group_model_listener.cc`, `tab_menu_model.cc`,
`tab_strip_model.cc`, `tab_group_view_layout.cc`, `tab_strip_view_layout.cc`. All are listed in `docs/REBASE.md`
(Space header, focused group header, last tab, wrap, toast, saved groups, tab context menu). Sizes are small (the
tab-strip-model hunks are 1-6 lines each; the group layout patch is the largest, three small edits). The BUILD deps
they need already exist (`tabs/common` impl, `ui/tabs`, `frame`) or are added in the BUILD patch. The net diff has no
`tab_menu_model.h` patch. No Linux-only code outside guards: the only platform code is `BUILDFLAG(IS_MAC)` blocks
(`koan_shortcut_table.cc`, `koan_shortcuts.cc`, `koan_workspaces.cc` x2).

---

## Blockers

None.

## Should fix

### S1. The tab-menu hook is not #59's API; the two PRs collide on files and patches, and the PR text says they do not
`src/browser/ui/views/koan_tab_menu.cc:1-19`, `src/browser/ui/views/koan_tab_menu_hooks.h:19-27`,
`patches/chrome/browser/ui/tabs/tab_menu_model.cc.patch` (hunk at the end of `Build()`).

The PR comment and commit message say the hook is "#59's void `koan::AddKoanTabMenuItems()` in
`koan_tab_menu_hooks.h`". On #59's head (eb454f1) that header does not exist. #59 ships `koan_tab_menu.h` /
`koan_tab_menu.cc` with a table (`TabMenuFeature`, `kFeatures`, `TabMenuFeatureId` with `kSpaces = 2` reserved at
3200), a 6-argument `AddKoanTabMenuItems(menu, delegate, tab_strip, index, num_tabs, submenus)`, and a
`tab_menu_model.h` patch (`koan_submenus_`). #57 ships a different 4-argument function in a different header, in a file
with the same name as #59's.

Failure scenario: `git merge-tree` of the two heads reports 14 conflicting files, among them an **add/add conflict in
`src/browser/ui/views/koan_tab_menu.cc`** and content conflicts in `patches/.../tab_menu_model.cc.patch`,
`.../tab_strip_model.cc.patch`, `patches/chrome/browser/ui/BUILD.gn.patch`, `src/browser/ui/views/BUILD.gn`,
`koan_features.cc`, `koan_flag_entries.h`, `koan_settings_rows.cc`, REBASE/USAGE/ZEN_PARITY and three e2e files. The two
`tab_menu_model.cc.patch` hunks are the same two lines (include + call at the end of `Build()`), so a hand merge that
keeps both calls would add Space/Move To items twice or with the wrong signature, and a `.patch` merged textually
and not re-applied against a pristine file will not apply. (#59 alone already conflicts with develop in 6 files since
#58, so it needs a rebase either way.)

No semantic incompatibility: `ReplaceTabGroupMenuItems(menu, tab_strip, index)` is already a pure function that finds
items by command id and owns its own submenu delegate, so it becomes a `TabMenuFeature{kSpaces, add_items, is_enabled,
execute}` with trivial `is_enabled`/`execute` (both are called unconditionally by #59's loops, so they must not be null).

Fix: pick the order and have the second PR adapt. Recommended: land #59 first (it owns the shared API), then rebase
#57 to delete `koan_tab_menu.cc`, `koan_tab_menu_hooks.h` and its patch hunk, and register Spaces in `kFeatures`
(removing the `KeepTabMenuSubmenu` plumbing in favour of `TabMenuContext::AddSubmenu()` is optional, the controller-kept
model works). If #57 lands first, #59 drops its `koan_tab_menu.cc` add and rebases onto the 4-argument function. Correct
the PR text either way.

### S2. "Move To" activates the target Space's remembered tab before the moved tab, loading it
`src/browser/ui/views/koan_workspaces.cc:793-796` (`MoveTabsToSpace`: `SwitchTo(space)` first), `:826-829`.

`SwitchTo(space)` -> `EnterFocusMode` -> `PickSpaceTab` makes the target Space's last tab the active, visible tab. A
pending or discarded tab that becomes visible loads at once (`WebContentsImpl::UpdateVisibility...` calls
`GetController().SetActive(true)`, `web_contents_impl.cc:5406-5411` -> `LoadIfNecessary`, `navigation_controller_impl.cc:5056`).
The moved tab is added to the group and activated only afterwards, in the same task, which hides the remembered tab
again - but its load has started.

Failure scenario: after "Unload All Other Spaces" (or after a restart with #47), right-click a tab > Move To > B. B's
remembered tab, which the user never opens, loads (network, scripts, audio) and is then hidden. This is the
guarantee B2 protects for swipes; Move To goes around it. No test covers Move To into an unloaded Space.

Fix: do not show the Space first. Move the tabs with `AddToExistingGroup` and activate the last moved tab; upstream leaves
focus mode (the active tab is outside the focused group), and `EnsureOneSpaceShown()` re-focuses the Space on that tab,
as in the B1 path. The old reason for showing the target first (the source Space emptied and revived) is handled by the
`closed_spaces_` revival, which already runs after the move. Add to `spaces_switching.mjs`: unload B, Move To B, assert
B's other tab's load count did not change.

## Nits

- **N1** `docs/REBASE.md:57`: the row "New tabs in the active Space (no patch)" names `chrome/browser/ui/browser_navigator.cc`;
  in Chromium 158 the file is `chrome/browser/ui/navigator/browser_navigator.cc` (the `GetFocusedGroup()` fallback is at
  `:1040`). A rebaser grepping the documented path finds nothing.
- **N2** `docs/REBASE.md:54`: the swipe row says Mac upstream "only" handles scroll events "with phases" and "a Mac mouse
  wheel's have none and are left alone". `VerticalTabStripFocusSwipeController::OnScrollEvent` has no phase filter for
  `kScroll` events with `momentum_phase NONE`; it calls `ProcessSwipeOffset` for them. A notched Mac wheel only avoids
  upstream's swipe because Koan's `kSystem` handler runs first and stops propagation. Reword so that a future change to
  the priority or to `Handle()` is checked against this.
- **N3** `patches/chrome/browser/ui/views/tabs/common/tab_group_view_layout.cc.patch`: the Space's own group header is
  laid out with `koan_space ? gfx::Rect() : header_bounds` but keeps `group_header_->GetVisible()` (true).
  `TabGroupHeaderView` is `ACCESSIBLE_ONLY` focusable (`tab_group_header_view.cc:222`) and Enter/Space open the group editor
  (`:292-300`: rename, colour, Ungroup, Close group). A screen-reader user can reach this zero-size control and ungroup or
  close the Space through upstream's editor. Pass `false` for `visible` when `koan_space`.
- **N4** `koan_workspaces.cc:117-123, 808-825`: the `IsPinnedInSpace()` seam is documented as "#59 adds
  `koan::IsPinnedInSpace()`". #59 adds `koan::IsPinnedForUser()`, true for Essentials and Space-pinned tabs alike, and
  states that it already keeps a pinned tab pinned and at the top of the new Space after a move (re-checked 400 ms after
  moves stop). With the seam wired to it, `MoveTabsToSpace`'s own `MoveWebContentsAt` and #59's normaliser would both
  reposition the tab. Remove the seam (and fix the comment) when #59 lands; until then it is dead code.
- **N5** `koan_workspaces.cc:1098-1107`: when no Space is focused and the active tab is an Essential (first `Update()` after
  a restart that quit on an Essential), `SwitchTo(last_space_ or first Space)` -> `EnterFocusMode` replaces the active tab
  with the Space's remembered tab, because the Essential's group is null. The user's selected tab is not restored. (By
  reading; same as before the PR, no test.)
- **N6** `koan_tab_unload.cc:29-38`: `IsTabInUse()` returns false (not in use) when `PerformanceManager` is unavailable or the page
  node/policy is missing, so a safety check fails open and an audible or capturing tab would be unloaded. Return true in
  those cases.
- **N7** `koan_shortcuts.cc:51-53` (`IsPending`) duplicates `koan::IsTabUnloaded()` (`koan_tab_unload.h`), and #59 has a third
  copy of "pending". Use the shared one.
- **N8** Test gaps that matter for the above: Move To into an unloaded Space (S2), Unload with an audible or split tab (S1's
  skip rules), more than one window (S2 of round 1, the tint), Create form in compact mode, anything on macOS (S4/S5
  ran only by reading). The Mac `OnScrollEvent` branch, the Ctrl+1..0 bindings and the high-priority registration are
  never compiled or run on Linux.
