# Review of PR #57 "Spaces like Zen's" (issue #34)

Branch `feat/zen-spaces` at 198c76f, diff `origin/develop...HEAD` (46 files). Nothing was built or run
(shared build lock); everything below is from reading the diff against Chromium 158 at
`~/work/koan/chromium/src` (HEAD 67e29b82), Zen's source and the merged work. Findings marked
**(uncertain)** are not fully verified by reading; Mac findings were not run on a Mac.

Verified OK (no finding):
- All 7 patches (`saved_tab_groups/BUILD.gn`, `saved_tab_group_model_listener.cc`, `tab_menu_model.cc/.h`,
  `tab_strip_model.cc`, `tab_group_view_layout.cc`, `tab_strip_view_layout.cc`) pass `git apply --check` against
  pristine `git show HEAD:<path>` files. REBASE.md has a row for each of them.
- Every upstream API used exists with the signature the PR uses: `TabStripModel::{EnterFocusMode, GetFocusedGroup,
  AddToGroupForRestore, AddToExistingGroup(indices, group, add_to_end), AddToNewGroup, ChangeTabGroupVisuals,
  MoveGroupTo, IsEphemeralTabGroup, closing_all}`, `TabGroupChange::{kClosed, GetCloseChange(),
  TabGroupClosureReason::kDetachedToAnotherTabstrip}`, `TabLifecycleUnitExternal::DiscardTab(reason, footprint=0)`,
  `chrome::{AddAndReturnTabAt, AddTabAt}(…, group)`, `chrome::ShowQuestionMessageBoxAsync`, `IDC_NEW_SPLIT_TAB`,
  `TabMenuModel` command ids.
- #54: the PR merges its hunks into the existing `tab_menu_model.cc.patch` (one `koan::AddZenTabMenuItems()` call;
  `koan_split_view.cc` is untouched and finds its items by command id, so moving the call after the group items is safe).
- Unload does not lose tabs: `DiscardWebContents` swaps the `WebContents` inside the same `TabModel`
  (`CopyStateFrom`), so the tab, its `TabHandle` and its history stay.
- Colours all come from `kColorKoanSpace*` / `kColorKoanEmojiHovered` mapped in the mixer; radii are in
  `koan_sidebar_metrics.h` or the existing `SidebarRowCornerRadius()`. No Linux-only code outside guards in the new files.
- Delete Space: strings match Zen's ftl, the last Space can't be deleted, the active one is left first (`Cycle(false)`),
  the closed-Space revival is skipped for `deleting_`.
- #50/#53: `Clear` still never removes a Space (`empties_workspace` adds a tab first) and `UnpinnedTabsForClear` is
  unchanged; with no ∞ view the "ungrouped only" rule is moot.

---

## Blockers

### B1. Explicitly activating a tab in another Space is overridden by that Space's "last active tab"
`src/browser/ui/views/koan_workspaces.cc:348-364` (`SwitchTo`), `:785-802` (end of `EnsureOneSpaceShown`),
`:829-843` (`RestoreLastActiveTab`), `:994-997`.

What is wrong: upstream leaves focus mode whenever the active tab moves outside the focused group
(`tab_strip_model.cc` `SetSelection`, ~4264-4276: `exited_focus`). `EnsureOneSpaceShown()` then re-focuses
`active->GetGroup()` through `SwitchTo()`, and `SwitchTo()` first calls
`ActivateTabAt(last_active_tabs_[space])`. So the tab the user just chose is replaced by the Space's remembered tab.
`RestoreLastActiveTab()` (posted from `OnTabGroupFocusChanged`) does the same override a second time.

Failure scenario: Spaces A and B; the user has visited B before (so `last_active_tabs_[B]` = B-tab-2). In A the
user picks B-tab-5 through the omnibox "Switch to this tab" suggestion, tab search, a `chrome.tabs.update({active})`
call, or back-to-opener (#46) closing a tab whose opener is in another Space. Upstream activates B-tab-5 and exits focus;
Koan's Update then activates B-tab-2 instead (loading it if it was pending or unloaded) and B-tab-5 never shows.
The e2e only tests switching through the bar, keys and wheel, so nothing catches it.

Fix: only apply "last active tab" for Koan-initiated switches (bar, keys, menu, wheel, wrap). In
`EnsureOneSpaceShown` call `model->EnterFocusMode(target)` directly (the active tab is already in `target`), and in
`RestoreLastActiveTab` only restore when the active tab is the group's first tab (the one upstream's `EnterFocusMode`
picked, `tab_strip_model.cc:2259-2272`). Add an e2e check: with two visited Spaces, activate a non-remembered tab of the
other Space via `Target.activateTarget` and assert that tab is the one shown.

### B2. Swipe (and, on Linux, the wheel) wakes the destination Space's first tab before Koan restores the last active one
`koan_workspaces.cc:968-999` (`OnTabGroupFocusChanged`), `:829-843`; upstream
`vertical_tab_strip_focus_swipe_controller.cc` `RotateTabGroupFocus` -> `TabStripModel::RotateFocusedGroup`
(`tab_strip_model.cc:2317-2366`) -> `EnterFocusMode` (`:2259-2272`, activates `tab_group->GetFirstTab()`).

What is wrong: `SwitchTo()` carefully avoids activating the first tab ("keeps upstream from activating (and maybe
reloading) the Space's first tab on the way"), but swipes never go through `SwitchTo()`. The first tab is activated
(shown, so a restored-on-demand or discarded tab loads), and only afterwards a posted task activates the real last tab.
The last step of a swipe that wraps past the first or last Space additionally passes through a frame with focus
mode off (`ExitFocusMode`, then `Update()` re-focuses), so the sidebar can briefly list every Space's tabs
**(uncertain: depends on whether the posted task runs before the next frame)**.

Failure scenario: after a restart with #47 on, or after "Unload Space", the user swipes (Mac trackpad) A -> B -> C.
B's and C's first tabs load (or reload), although the user never opens them; the tab the user lands on is loaded
second. This breaks #47's guarantee (tabs in unopened Spaces don't load) and makes Unload Space pointless once the
user swipes past it.

Fix: make Koan own the swipe/wheel instead of reacting afterwards: either (a) a one-line hook in
`TabStripModel::EnterFocusMode` / `RotateFocusedGroup` (patch already exists for this file) calling
`koan::PickFocusTab(group)` to choose the Space's remembered tab instead of `GetFirstTab()`, or (b) add a Koan
pre-target handler on the region view that handles the swipe itself with `Cycle()`/`SwitchTo()` and marks the event
handled before upstream's controller (its `StopPropagation` already prevents a second handling). (a) also fixes the
wrap path and needs no Mac-specific code. Add an e2e that restarts, swipes (see N4) and asserts the middle Space's first
tab is not loaded.

---

## Should fix

### S1. Unload Space skips every safety check Memory Saver has, and can discard the visible split sibling
`koan_workspaces.cc:484-497` (`UnloadSpace`). `TabLifecycleUnitSource::TabLifecycleUnit::Discard`
(`tab_lifecycle_unit.cc:359-419`) has no eligibility check for `EXTERNAL`; Memory Saver's exemptions live in
`performance_manager/policies/page_discarding_helper.cc` / `cannot_discard_reason.cc`, which this path does not consult.
Only `i == active_index()` is excluded.

Failure scenarios: (1) a background tab in the Space is playing audio, in a call, capturing the screen/camera, or
has a half-written form; "Unload Space" kills it with no prompt (docs/PERFORMANCE.md promises these are "never
discarded", which is true only for Memory Saver). Zen's `explicitUnloadTabs` goes through Firefox's own checks
**(uncertain what exactly Firefox skips; the Zen file only shows the filter on `pending`/essential/empty tabs)**.
(2) The active tab is in a split view: its sibling is on screen and selected but `active_index()` only protects one of
them, so the visible pane is replaced by a discarded contents. (3) Tabs that never loaded (`NeedsReload`, #47) are
"discarded" needlessly; Zen skips `pending` tabs. (4) `TabStripModel::DiscardWebContents` calls `FixOpeners(index)`
(`tab_strip_model.cc:403`), so tabs opened from an unloaded tab lose their opener link for #46's back-to-opener **(uncertain: same for Memory Saver, probably acceptable)**.

Fix: skip `model->IsTabSelected(i)`, tabs with `IsCurrentlyAudible()`, being captured / using devtools, tabs that
are pending (`contents->GetController().NeedsReload()` or `WasDiscarded()`, the same predicate as `koan_shortcuts.cc:55`),
and ideally ask `PageDiscardingHelper`'s eligibility (`CanDiscard`-style) before calling `DiscardTab`. Toast how many tabs
were unloaded (reuse KoanToast). Document the skips in USAGE/ZEN_PARITY ("Same" is currently claimed).

### S2. Space tint is re-applied from every window on every tab-strip change
`koan_workspaces.cc:692-705` (`Update()` calls `UpdateTint()` unconditionally), `:1014-1065`. In develop `UpdateTint()`
ran only on focus or visuals changes (`OnTabGroupFocusChanged`, `OnTabGroupChanged`); now any insert/remove/replace
schedules `Update()` -> `UpdateTint()`. The tint is the profile's single theme colour, so each window overwrites it.

Failure scenario: window A shows a Blue Space (browser tinted blue); window B shows a Default-theme Space. A background
tab in B finishes loading `window.open`, or Memory Saver discards a tab in B (`kReplaced`): B's `Update()` runs
`UpdateTint()`, sees `showing_tint` and restores the user's colour while A is still on screen; A's next tab change
re-tints it. Flicker and theme-pref writes (`kWorkspaceTintApplied`, saved colour) each time.

Fix: call `UpdateTint()` only from events that change the active Space (focus change, active Space's visuals,
window activation / `OnTintPrefChanged`), and bail out in `UpdateTint()` unless this is the last active browser
window (the check already used in `OnTintPrefChanged`).

### S3. The Create-a-Space form is invisible while the sidebar is collapsed, and it hides the tab list
`koan_space_header.cc:278-283` (form laid out with `visible = !collapsed`), `koan_workspaces.cc:585-598`
(`ShowCreateForm()` hides the unpinned scroll view and rows before checking). Scenario: icons-only sidebar, Ctrl+Alt+N
(or "+" > Create Space): the tab list vanishes, no form appears, the (invisible) name field has focus, and typing goes
nowhere visible. Same risk in compact mode while the sidebar is slid out **(uncertain: depends on whether focusing
the field reveals it)**. Fix: expand the sidebar (or show the form as a bubble) when the sidebar is collapsed, and
don't hide the rows unless the form is actually shown. Add an e2e for the collapsed case.

### S4. Mouse-wheel switching: Mac threshold, and not "over the sidebar" as documented/Zen
`koan_workspaces.cc:664-678`; `OnMouseWheel` is only wired in `koan_space_header.cc:214`, `koan_space_bar.cc:125,244`.
- `std::abs(x_offset) < MouseWheelEvent::kWheelDelta`: on Mac `kWheelDelta` is 53 (`ui/events/event.cc:678`) but one
  Cocoa wheel notch yields `kScrollbarPixelsPerCocoaTick (40) * deltaX` (`ui/events/cocoa/events_mac.mm:175`), so a single
  notch (deltaX = 1) is ignored; only accelerated scrolling passes **(uncertain on the exact deltaX per notch)**.
- Only the header and the bottom bar handle it. Zen listens on the whole toolbox (`ZenSpaceManager.mjs:615-660`),
  also switches on Ctrl + vertical wheel (`zen.workspaces.scroll-modifier-key`, natural-scroll pref), but Koan's
  tab list does not. On Linux the region-wide upstream controller (pre-target `OnMouseEvent`, `#if WIN/LINUX`) handles
  wheel events first and sets them handled, so Koan's cooldown code is never reached there; on Mac the upstream
  controller only handles `ScrollEvent`s (trackpad), so a Mac mouse wheel over the tab list does nothing.
- ZEN_PARITY.md and USAGE.md say "Same / over the sidebar".

Fix: accumulate `x_offset` per gesture instead of thresholding one event, handle it region-wide with a pre-target handler
on the Mac `MouseWheelEvent` path, add Ctrl + vertical wheel, or correct the docs.

### S5. Mac shortcuts for Spaces are Ctrl, not Zen's accel
`src/browser/koan_shortcut_table.cc:120-121` (not in the diff, but this PR's docs/parity table claim "Same"). Zen binds
Space forward/backward to `alt + accel` (`ZenKeyboardShortcuts.mjs:786-808`: Cmd+Option+Left/Right on macOS) and adds
Ctrl+1..0 to switch to Space N on macOS only (`:777-790`). Koan binds `kCtrl | kAlt` on every platform (the split
shortcuts a few lines below correctly use `kAccel | kAlt`) and has no "switch to Space N". Same Ctrl/accel mismatch for
`kCompactModeShowSidebar` (line 116). Fix: `kAccel | kAlt` for forward/backward (check it doesn't collide with Chromium's
Cmd+Option+arrow tab switching, which Koan's table must then unbind), add Ctrl+1..0 on Mac, update USAGE.md.

### S6. Accessibility: no keyboard or screen-reader path to Space actions
`koan_space_header.cc:47` (heading role only; no `SetFocusBehavior`), `:86,:231` (the "..." button exists only while
hovered), `koan_space_bar.cc` (Space buttons expose a name but not which one is active).
Scenario: a keyboard or VoiceOver user cannot rename, change icon, unload or delete a Space: the header is not
focusable, rename needs a double-click, the menu needs a right-click, and the "..." button is invisible/unreachable.
Switching announces nothing and the bar doesn't say which Space is selected. Fix: make the header focusable
(Enter = menu, F2 = rename), keep the actions button in the focus order, give Space buttons a radio/tab role with a
checked/selected state, and announce the new Space's name on switch (`NotifyAccessibilityEvent`).

### S7. Group header hiding keys off the global feature flag, not the per-window Spaces state
`patches/.../tab_group_view_layout.cc.patch` (`koan_space = IsGroupFocused() && IsWorkspacesEnabled()`) vs
`KoanWorkspaceController::IsEnabled()` (no private windows). In an Incognito window Spaces are off
(`ReplaceTabGroupMenuItems` leaves upstream's group items; no Koan header is installed), but a focused group now has
neither its group header (new in this PR) nor the Unfocus pill (already). A user can create and focus a group there
and cannot rename or colour it. `ShowsFocusExitedToast`/`SavesTabGroups` do check `IsOffTheRecord()`; this hook doesn't.
Fix: pass a per-window predicate (`koan::SpaceHeaderReplacesGroupHeader(tab_group_view)`) or check the profile. The
patch can also be smaller: set `header_bounds` height 0 under the condition rather than restructure three places.

### S8. The per-Space theme field is clean at the API but not at the storage seam #41 needs
`koan_workspaces.h:74-78`, `.cc:79,432-482,507-515`. `SpaceTheme` is `optional<TabGroupColorId>` and persistence is
"the group colour" with Grey as the sentinel for Default (`kDefaultThemeColor`), spread over `CreateSpace`,
`RenameSpace`, `SetSpaceIcon`, `SetSpaceTheme`, `GetSpaceTheme`, the revival path and `SnapshotSpaces`, each rebuilding
`TabGroupVisualData(title, color)` by hand. #41 ("extend `SpaceTheme`") cannot persist a gradient/opacity/texture in
a tab group colour, so it would have to rewrite all of these. Grey is also unselectable as a real colour.
Fix: one `ReadSpace(id)` / `WriteSpace(id, {icon, name, theme})` pair that owns the encoding (title + colour today),
so #41 replaces storage in one place; keep `SpaceInfo`/`SpaceTheme` as is.

---

## Nits

- **N1** `koan_workspaces.cc:278-294` (`SplitTitle`): a name that starts with an Extended_Pictographic character is
  read as an icon, and that set includes (c), (R), TM, i-in-box, arrows. A Space named "(c) Legal" becomes icon (c) + name
  "Legal"; one named only "🚀" shows the name "Space". Only treat characters the picker offers, or store the icon separately (see S8).
- **N2** `:532-549`: `deleting_` is cleared only on `kClosed`. If a tab's beforeunload keeps the Space alive
  (`RunUnloadListenerBeforeClosing` in `tab_strip_model.cc:~4230` defers the close), the id stays, and later closing that
  Space's last tab won't bring it back. Erase entries for groups that still exist at the next `Update()`.
- **N3** `docs/REBASE.md`: upstream behaviours the PR relies on without a patch have no row: `RotateFocusedGroup`
  exiting focus at the ends and `EnterFocusMode` choosing the first tab (the wrap logic in `EnsureOneSpaceShown`),
  `VerticalTabStripFocusSwipeController` (Mac `OnScrollEvent` / Win-Linux `OnMouseEvent` split), `SetSelection` leaving
  focus for a tab outside the group, `AddToGroupForRestore` reusing a closed group's id, `Navigate` adding new tabs to
  the focused group (`browser_navigator.cc:1041`). Add "(no patch)" rows.
- **N4** e2e gaps (`scripts/e2e/spaces.mjs`): the wheel check on Linux passes through upstream's controller, not
  Koan's `OnMouseWheel`; no `ScrollEvent`/swipe test at all (so swipe wrap, B2 and the Mac path are untested);
  double-click rename and double-click icon are untested (UI DevTools can't send a click count), only the menu paths;
  no restart check of Space names, icons, colours or "no extra Space appeared" (`power.mjs` only asserts load counts);
  no Unload All Other Spaces, no Unload with audio/split, no Delete of a non-active Space, no form Cancel/Escape, no
  multi-window tint. `tab_management.mjs` dropped the "collapsed group" check, which still applies in Incognito windows
  where Spaces are off.
- **N5** `koan_space_header.cc:173-175` + `koan_workspaces.cc:985`: switching Space cancels an in-progress rename
  (`CancelRename`), while docs say leaving the field keeps it. On Linux the button click blurs first (keeps); on Mac
  buttons don't take focus on click so the typed name is silently dropped **(uncertain)**.
- **N6** `koan_workspaces.cc:877-885`: after the first switch the header and the scroll view keep their own layers forever
  (`SetPaintToLayer` is never undone), and a switch during a running animation recreates layers at the target transform
  (old layer jumps to its end). Clean up at animation end; `SlideOutLayers` is leaked if the window dies mid-animation
  (the observer is only deleted on completion/abort) **(uncertain)**.
- **N7** `koan_space_header.cc` / `koan_space_bar.cc` `Layout()` use absolute LTR x positions (not mirrored), while the
  animation code honours `base::i18n::IsRTL()`. Pick one.
- **N8** `GetSpaces()` (`:304-320`) lists every group in the model, including ephemeral groups and groups created by
  extensions (`chrome.tabs.group`), which would appear as Spaces **(uncertain)**.
- **N9** Zen parity not listed under "Not done": dropping a tab on a Space button / hovering it while dragging switches Space
  (`zen.tabs.dnd-switch-space-delay`); swiping past the last Space opens the Create flow in Zen
  (`ZenSpaceAddSwipe`, `edge-actions`) where Koan wraps; `open-new-tab-if-last-unpinned-tab-is-closed` is false in Zen
  but Koan always adds a tab (documented).
- **N10** Bottom bar in the icons-only sidebar: `KoanSpaceBarView::Layout` computes `room = max(0, width - 2*side)`,
  so with 3+ Spaces the 10px dots are laid out around the "+" with no room **(uncertain, not seen in any screenshot)**.
- **N11** Saved-group migration **(uncertain)**: `SavesTabGroups()` stops new saved groups, but a profile that already has
  saved groups from earlier builds keeps them in the saved-group model while `OnTabGroupAdded` now skips connecting the
  restored local groups. Check a profile created on develop.
- **N12** Session restore **(uncertain)**: if a window is ever shown with an initial NTP before restored tabs arrive, `Update()`
  wraps that NTP in a "Space" group, and closing the NTP later revives that group with a new tab (a stray empty Space).
  `power.mjs` doesn't assert the Space count after restart.
