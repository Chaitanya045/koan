# Review of PR #59: pinned tabs and Essentials like Zen's (fixes #38)

Branch `feat/zen-pinned-essentials` at `0304f5d`, diff `origin/develop...HEAD` (43 files, +3070/-55).
Read-only review: nothing was built or run. Everything below was verified by reading the PR code against
upstream Chromium 158 (`~/work/koan/chromium/src`, HEAD) and Zen's source. "Uncertain" marks findings that
depend on runtime behaviour I could not observe.

Paths: `koan_pinned_tabs.cc` is `src/browser/ui/views/koan_pinned_tabs.cc`; `koan_pinned_tab_views.cc` is in the same
directory; upstream paths are relative to `chromium/src`.

## Checked and fine

- All 11 hook patches changed or added by the PR pass `git apply --check` against pristine
  `git show HEAD:<path>` files, with no offsets or fuzz. The GN dependency hunks cover the target that holds
  each patched source (checked per target after applying the patches).
- `tab_menu_model.cc.patch` and `tab_strip_model.cc.patch` merge #54's hunks into the existing patches (the
  split view call moved into the single hook) instead of duplicating them.
- Persistence design: per-tab state in a `WebContentsUserData`, saved as session extra data, re-added in
  `BuildCommandsForTab` (upstream does not re-add extra data on rebuild; verified) and applied in
  `CreateRestoredTab` before `NavigationController::Restore`. It lives in the tab itself, so I found no path
  that duplicates tabs on restart (including several windows).
- Zen's six close behaviours match `zen-preferences.ftl` and the `onCloseTabShortcut` switch. Toast strings,
  the "n / 12" badge, "Back to pinned url" / "Separate from pinned tab", and "Edit Essential URL" / "Reset
  Essential Tab" match `zen-general.ftl` and `zen-vertical-tabs.ftl`. Essentials show no reset button, as in
  Zen (`onLocationChange` skips `zen-essential`).
- Spaces: Essentials are Chromium pinned tabs (outside every group, so visible in all Spaces); Zen-pinned tabs
  are group members at the top of their group, so they belong to their Space. Matches Zen.
- macOS: Cmd+W and the menu bar's Close Tab run `IDC_CLOSE_TAB` -> `chrome::CloseTab`
  (`browser_command_controller.cc:769`), so the `browser_commands.cc` hook covers them. No Linux-only code
  outside the e2e scripts. Accel is `EF_PLATFORM_ACCELERATOR` (Cmd on macOS).
- Colours come from `kColorKoanPinnedTab*` IDs and `SidebarRowCornerRadius()`, with the exception in N1.
  The freezing exemption works: `PageLiveStateDecoratorHelper` resets `IsPinnedTab` on insert and replace, and
  Koan re-sets it from a posted task (`OnTabsAdded`).

---

# Blockers

## B1. Back-to-opener can close a workspace-pinned tab (regression of #46's "never for pinned tabs")

- Where: `src/browser/koan_tab_hooks.cc:64-67` (`ShouldCloseTabOnBack`, only reads the pref), with upstream
  `chrome/browser/ui/tabs/back_to_opener/back_to_opener_controller.cc:243-247` and `:289-292`; the new pinned
  kind is in `koan_pinned_tabs.cc:331-338` (`IsPinnedInSpace`).
- Problem: upstream only blocks back-to-opener when `is_pinned_`, which tracks Chromium's `IsPinned()`. After
  this PR, Ctrl+Shift+D (and the menu's Pin) no longer sets Chromium's pinned flag, so a pinned tab is
  eligible. `docs/ZEN_PARITY.md` still claims "never pinned ones".
- Scenario: the pref `koan.tabs.close_on_back_with_no_history` is on by default. Middle-click a link in tab A
  to open tab B, press Ctrl+Shift+D on B. B has an opener relationship (set on its first user-gesture
  navigation from A). Press Back/Alt+Left in B, or use "Reset Pinned Tab" first, which leaves a single history
  entry because of `should_clear_history_list`: B, now a pinned tab, closes and A is activated. The same
  happens after a reset or restore, since the history is one entry.
- Fix: make `HasValidOpener()` false for Zen-pinned tabs. `ShouldCloseTabOnBack(WebContents*)` already
  receives the contents; add a header-only declaration (the `//koan/browser:tab_hooks` pattern, no tab strip
  deps) such as `bool IsZenPinnedContents(content::WebContents*)` defined in `:impl`, or a small
  `WebContentsUserData` flag kept in the tab_hooks target that `KoanPinnedTabController` updates. Add an e2e
  check (link opened in a new tab, pinned, Back leaves it open).

## B2. "Close Other Tabs" and "Close Tabs Below" close workspace-pinned tabs (regression for Ctrl+Shift+D tabs)

- Where: upstream `chrome/browser/ui/tabs/tab_strip_model.cc:3877` (`GetTabsClosedByCommand`:
  `tab->IsPinned()`) and `:3839` (`HasIndicesClosedByCommand`: `IsTabPinned(i)`). Neither is touched by the
  PR's `tab_strip_model.cc.patch`.
- Problem: before this PR, a Ctrl+Shift+D tab was a Chromium pinned tab, which both commands skip. Now it is
  an ordinary group member. The PR protects Clear and Ctrl+Shift+K (`UnpinnedTabsForClear`,
  `UpdateClearRow`) but not the context menu's bulk close. Firefox/Zen's "Close Other Tabs" also skips pinned
  tabs, workspace-pinned ones included.
- Scenario: Space with 3 pinned tabs and 5 normal tabs. Right-click a normal tab -> "Close Other Tabs": all
  three pinned tabs close, along with their pinned URL, label and icon. "Close Tabs Below" on the first pinned
  tab closes the other pinned tabs and the normal ones.
- Fix: two more one-line conditions in the existing `tab_strip_model.cc` patch:
  `!koan::IsZenPinnedTab(tab)` next to `IsPinned()` in both functions (the enabled state in
  `HasIndicesClosedByCommand` must agree). Alternative without new hunks: handle `CommandCloseOtherTabs` /
  `CommandCloseTabsToRight` in `ExecuteKoanTabMenuCommand` and filter there, but the enabled state still needs
  the hook. Add an e2e check.

---

# Should fix

## S1. "Reset" is skipped when only the URL fragment differs (hash-routed apps such as Gmail)

- Where: `koan_pinned_tabs.cc:155-160` (`LeftPinnedUrl` uses `EqualsIgnoringRef`), used by
  `RunCloseBehaviour` (`:788`), the menu enabling (`:1019-1021`) and `OnRestoringTab` (`:1147-1152`).
- Problem: ignoring the fragment is right for deciding whether to show the slash (Zen does the same in
  `onLocationChange`). But Zen's reset is unconditional (`#resetTabToStoredState` in `onCloseTabShortcut`,
  `resetPinnedTab`, startup restore), and its "Reset Pinned Tab" / "Replace with Current URL" items are always
  enabled for a pinned tab. Koan gates all of them on `IsAwayFromPinnedUrl`.
- Scenario: Gmail pinned at `.../mail/u/0/#inbox`, user moves to `#sent`. Ctrl+W (default
  reset-unload-switch) does not reset: the tab is discarded and comes back at `#sent`. The context menu's
  "Reset Pinned Tab" and "Replace with Current URL" are disabled, so the user cannot pin `#sent` either. With
  "restore to pinned URL" on, the startup reset is skipped too.
- Fix: for the actions (close behaviour reset, menu Reset/Replace, startup restore) compare with full URL
  equality or just always reset when a pinned URL exists; keep `LeftPinnedUrl` only for the indicator. Enable
  the two menu items for any pinned tab.

## S2. The "close" behaviour closes without recording a historical tab

- Where: `koan_pinned_tabs.cc:775-779`.
- Problem: `CloseWebContentsAt(index, CLOSE_USER_GESTURE)` omits `CLOSE_CREATE_HISTORICAL_TAB`
  (`tab_enums.h:35-37`: "You almost always want to set this"; upstream's `CloseSelectedTabs` passes
  `CLOSE_CREATE_HISTORICAL_TAB | CLOSE_USER_GESTURE`, `tab_strip_model.cc:3506`).
- Scenario: setting "Close tab", Ctrl+W on a pinned tab closes it; Ctrl+Shift+T cannot bring it back.
- Fix: pass `CLOSE_USER_GESTURE | CLOSE_CREATE_HISTORICAL_TAB`.

## S3. Reopening a closed tab or window loses the pinned state

- Where: no hook for `BrowserLiveTabContext::GetExtraDataForTab` (`chrome/browser/ui/browser_live_tab_context.cc:197`,
  which only calls `glic::PopulateGlicExtraData`); `OnRestoringTab` already reads `extra_data` in
  `CreateRestoredTab`, which is also the tab restore path.
- Problem: the tab restore service builds `extra_data` only from that function, so Koan's key is never
  written for closed tabs and windows.
- Scenario: Cmd+Shift+W closes a window with pinned tabs (macOS: closing the last window keeps the app
  alive), Cmd+Shift+T reopens it: the pinned tabs return as plain tabs, with no pinned URL and no label or
  icon. The same for a pinned tab closed by "Close Tab" and reopened.
- Fix: one more hook in `GetExtraDataForTab` adding `kTabSessionDataKey -> GetTabSessionData(contents)` (the
  hook header already has both); `from_session_restore` is false on that path, so "restore to pinned URL"
  correctly does not apply. Mention in REBASE.md.

## S4. Restored-on-demand pinned tabs count as "loaded": close button and middle click misbehave, "Unload" is a no-op

- Where: `koan_pinned_tabs.cc:812` (`HandleCloseRequest`), `:741-742` (`Discard`), `:1018` (Unload enabled),
  `koan_pinned_tab_views.cc:249`. Koan's own definition of pending is
  `WasDiscarded() || NeedsReload()` (`koan_shortcuts.cc:58-60`).
- Problem: with #47's restore on demand, after a restart every non-visible pinned tab is "not loaded"
  but `WasDiscarded()` is false. `Discard()` goes on to `DiscardTab()`, which `DiscardEligibilityPolicy`
  refuses when there is no main frame (`discard_eligibility_policy.cc:236-239`), so nothing happens.
- Scenario: after a restart, click the close button (or middle-click) on a not-yet-loaded pinned tab, with
  default settings: if it is at its pinned URL, nothing happens (Zen: closes it, `closeIfPending`); if it
  was restored away from the pinned URL, `ResetToPinnedUrl` loads the pinned page in the background
  (waking the tab) and then discards it. "Unload" on such a tab is enabled but does nothing.
- Fix: use one shared `IsPending()` (WasDiscarded or NeedsReload) for `HandleCloseRequest`, the Unload
  enabled state and `Discard`; when the tab is already pending, skip the reset load.

## S5. Pinned section is not maintained for moves and drags (Zen unpins on drag out)

- Where: `OnTabStripModelChanged` (`koan_pinned_tabs.cc:840-885`) handles only `kInserted` and `kReplaced`;
  `LastPinnedInSection` (`:422-442`) assumes the pinned tabs form one contiguous run at the top of the group.
- Problem: moves inside a group are unconstrained in Chromium (`ConstrainMoveIndex` only separates real
  pinned tabs). Nothing reacts to `kMoved`, so a dragged pinned tab keeps `pinned_in_space` wherever it
  lands, and a normal tab can be dropped above the pinned run. Zen unpins a workspace-pinned tab dropped in
  the normal area and pins a normal tab dropped in the pinned area (`moveToAnotherTabContainerIfNecessary`);
  an Essential dropped on the pinned area becomes a workspace-pinned tab (`removeEssentials(tab, false)`),
  whereas here Chromium unpins it and `OnTabPinnedStateChanged` clears the pinned URL.
- Scenario (drag behaviour inferred from TabStripModel rules, not run): drag a pinned tab below a normal tab:
  it still shows as pinned, Clear skips it, "Pin" of the next tab computes the end of the run from the first
  gap, and ordering across restarts keeps the gap. Drag an Essential into the pinned area: it loses its
  pinned URL instead of becoming a pinned tab.
- Fix: observe `kMoved` (posted): if a `pinned_in_space` tab left the top run, unpin it
  (`UnpinFromSpace`-style); if a normal tab entered it, pin it. For Essential -> group drops, keep the URL
  (`PinInSpace` path). Alternatively state the Koan rule (pinned tabs are the first tabs, enforced on move)
  and clamp moves in the patch.

## S6. The 12-Essentials cap is bypassed (drag, Duplicate)

- Where: `CanAddToEssentials` (`koan_pinned_tabs.cc:384`) is only checked by the menu and palette. Upstream's
  `DuplicateTabAt` (`browser_commands.cc:404`) uses `ADD_PINNED` for a pinned source.
- Problem: Zen gates every path (`canEssentialBeAdded` in `addToEssentials` and drag-over). Here, dragging into
  the tiles is uncapped (the docs admit it, yet mark the row "Same"), and Duplicate Tab on an Essential
  inserts a second pinned tab: a 13th, 14th... Essential, whose pinned URL is then initialised to whatever
  the source showed.
- Fix: in `OnTabPinnedStateChanged`/`kInserted`, if the count exceeds the cap, unpin the new tab back into the
  Space (and toast), and mark the docs row "Different" until then.

## S7. e2e coverage gaps: the feature's riskiest paths are not tested; power.mjs lost its Essentials check

- `scripts/e2e/power.mjs:213-243` still presses Ctrl+Shift+D to pin, which now creates a workspace-pinned
  tab. #47's guarantee that an Essential (Chromium pinned) is never frozen is no longer covered anywhere.
  The PR text says power "now covers a workspace-pinned tab", but power.mjs is not in the diff. Pin through
  both kinds.
- `scripts/e2e/pinned.mjs:339-340`: "the Essential stays an Essential ... `PinnedUrl !== ''`" is trivially
  true, because `kInserted` calls `InitPinnedState` for every restored Essential with its current URL. The
  Essential is never navigated away before the restart, so persistence of the Essential's pinned URL is
  untested (and with a persistence bug it would silently adopt the away URL).
- No check that the restart did not duplicate tabs (compare the tab count and URL multiset before/after;
  `rowOf` takes the first match), the exact scenario the issue cares about.
- Only the default close behaviour is tested; the other five (notably "close", which has S2) are not.
  "Ctrl+W on an Essential" (`:264`) only counts rows: it does not check the switch or the unload that the
  comment above it claims.
- Not tested: the "n / 12" badge (the file header claims it), Remove from Essentials, Unpin, Replace/Edit/Reset
  from the menu, Unload, Change Icon, switching Spaces (that a pinned tab is hidden in another Space and
  Essentials are not), unload-then-close of a pending tab (S4), and the cases of B1/B2/S1/S5.
- Fix: add these; a Space-switch check is the cheapest test of the "per Space" requirement.

## S8. Conflicts with in-flight work (#57 Spaces, #42 folders)

- `patches/chrome/browser/ui/tabs/tab_menu_model.cc.patch` and `tab_strip_model.cc.patch`: #57 rewrites the
  same files. #57 puts its hook (`koan::AddZenTabMenuItems`, header `koan_tab_menu.h`) in the middle of
  `Build()`, with a `koan_submenu_` member (`tab_menu_model.h.patch`), and adds a hunk in the
  focus-exited toast; #59 uses `koan::AddKoanTabMenuItems` with `koan_tab_menu_hooks.h` at the end of `Build()`
  and also drops the `diff --git`/`index` header lines of these patches. Both PRs edit the include line, the call
  site and the first two `tab_strip_model.cc` hunks, so whichever lands second needs a manual merge into
  one hook. The "Edit Pinned URL" submenu that #59 skipped can use #57's `koan_submenu_`.
- Behaviour of #57 on #59's model: "Move To" a Space (moves selected tabs into another group) will drop a
  `pinned_in_space` tab anywhere in the other Space, breaking `LastPinnedInSection`'s contiguity (S5);
  #57's `MoveTabsToSpace` should call `MoveToPinnedSection` for pinned tabs. #57's "Unload Space" duplicates
  `UnloadTabs`; #57's emoji picker (`ZenEmojiPicker`) makes this PR's grid picker (`KoanIconPicker`)
  redundant. #57's "last tab of a Space closed -> fresh tab" interacts with the "close" behaviour of the only
  tab. Unpinning with "every unpinned tab belongs to a Space" is compatible (`RemoveFromEssentials` already joins
  the focused group).
- #42: "unload all tabs of a folder" is correctly deferred and documented. Folders nested in a Space will need
  `LastPinnedInSection`'s group logic to know about nested groups; Zen's "Add to Essentials" is hidden for
  tabs in a folder (`zenAddEssential.hidden = ... || !!contextTab.group`), which does not apply yet because every
  Space tab is in a group.

## S9. Pinning a tab in a split view (uncertain)

- Where: `PinInSpace` -> `MoveToPinnedSection` -> `MoveWebContentsAt` (`koan_pinned_tabs.cc:444-464`).
- Problem: `MoveTabToIndexImpl` calls `MaybeRemoveSplitsForMove` when a split member moves, and nothing in the
  PR handles splits. Zen pins the whole split group (`onCloseTabShortcut`/`pinTab` flatMap
  `split-view-group`). Chromium's own pin path has `SetSplitPinnedImpl` for this reason.
- Scenario: Ctrl+Shift+D while a split is active may dissolve the split (or move only one member). I could
  not confirm the exact outcome without running it. Close behaviour with a split has the same gap (Zen
  closes/unloads both members, Koan the active one).
- Fix: when `tab->IsSplit()`, move the tabs of the split together
  (`TabStripCollection::MoveTabsRecursive` is what `SetSplitPinnedImpl` uses), or refuse to Zen-pin split tabs.

---

# Nits

- **N1. Hard-coded colour and sizes.** `koan_pinned_tabs.cc:179` paints the emoji icon with `SK_ColorBLACK`
  (colour IDs only; a monochrome fallback glyph is black on a dark sidebar; use a colour ID or the tab's
  foreground). `EmojiImageSource` uses literal 16 / 13, and `koan_pinned_tab_views.cc` has local
  `kIconColumns`, `kIconButtonSize = 32` and `SetBetweenChildSpacing(8)` instead of `koan_sidebar_metrics.h`.
- **N2. Command id range and comment.** `koan_pinned_tabs.cc:99-112` says 47001+ is "clear of ... extension
  items (49000+)", but upstream reserves 47000-48999 as `IDC_CONTENT_CONTEXT_CUSTOM_FIRST`
  (`chrome/app/chrome_command_ids.h:321`) for web content context menus. It does not collide in the tab
  menu, but the comment and REBASE.md row 77 are wrong; a lower free range would avoid doubt. The ids are also
  cast into `TabContextMenuCommand` (an enum of 0-30), the same unspecified-value precedent as #54's 3001.
- **N3. Stale comment.** `koan_pinned_tabs.h:76-77` says Ctrl+W lives in `KoanShortcutController`; it is the
  `chrome::CloseTab` hook. The `HandleCloseShortcut` comment at `:129` is right.
- **N4. REBASE.md rows.** Rows 76-77 (new) and row 55 (#54's "Tab context menu") describe the same two
  files and hooks; merge them into one entry. The new `sessions/BUILD.gn.patch` and the two new `ui/BUILD.gn`
  hunks have no row (row 55 only names `ui/tabs/BUILD.gn`).
  ZEN_PARITY.md's back-to-opener row ("never pinned ones") is untrue until B1 is fixed.
- **N5. Repeated favicon queries.** `OnTabChangedAt` (`koan_pinned_tabs.cc:895-899`) calls `FetchPinnedIcon`
  whenever `pinned_icon` is empty, and it stays empty if the history database has no icon for the pinned
  URL, so every tab-changed notification (title, loading, audio) of an away pinned tab starts another query.
  Track "asked for this URL" in `KoanTabState`.
- **N6. Pinning a not-yet-committed tab.** `InitPinnedState` (`:388-395`) uses the last committed entry,
  which for a brand-new tab is the initial `about:blank` entry. Pinning it before the first commit pins
  `about:blank` (uncertain how reachable this is for a user). Skip pinning/Initialising until
  `!entry->IsInitialEntry()`, or fall back to the first commit.
- **N7. `closeIfPending` differs from Zen.** Zen closes an unloaded pinned tab only on a middle click and
  only for behaviours with "unload" (`ZenPinnedTabManager.mjs:174-178, 408-433`, pref
  `wheel-close-if-pending`). `HandleCloseRequest` (`:810-817`) also closes it from the close button and for
  "reset"/"switch"/"reset-switch".
- **N8. Unknown close behaviour value.** An unrecognised pref value (hand edit, future sync) makes Ctrl+W on a
  pinned tab do nothing (`RunCloseBehaviour` falls through). Fall back to the default.
- **N9. Reset then discard loads the page first.** `RunCloseBehaviour` selects another tab, then navigates the
  now background tab to the pinned URL and discards it after `DidFinishNavigation`. Zen unloads first and
  resets the stored state, so no network load happens. For slow or authenticated pinned URLs this is wasted
  work (and if the navigation never commits, e.g. 204 or download, the tab stays loaded and away). Consider
  discarding first and replacing the discarded tab's entries (as `OnRestoringTab` does for restore).
- **N10. Include order.** `koan_sidebar_tabs.cc:5-6` adds an include directly under the main header, without a
  blank line and before the standard headers.
- **N11. Multi-selection and Ctrl+W.** Zen runs the behaviour for all selected pinned tabs; `HandleCloseTabCommand`
  considers the active tab only. With an unpinned active tab and a pinned selected tab, upstream's
  `CloseSelectedTabs` still closes the pinned one (including Essentials, as the confirmation toast only
  shows when all are pinned).
- **N12. Hover sublabel and the modifier key.** Zen switches the text when Ctrl/Cmd is pressed or released
  while the pointer stays on the button (`_onAccelKeyChange`); `KoanTabRow` only updates on mouse move.
- **N13. Zen's "Reset Pinned Tab to Pinned URL" shortcut** (`zen-pinned-tab-reset-shortcut`, unbound by
  default, `ZenKeyboardShortcuts.mjs:985-995`) is not in the shortcut table (#46), so it cannot be bound.
- **N14. Accessibility.** The reset button has a fixed name "Reset and pin tab" (Zen's tooltip string) and is
  `ACCESSIBLE_ONLY` with no keyboard path (the menu and palette are the alternatives); the sublabel is a
  painted view with no accessible text; a tab's pinned/Essential state is not part of the row's accessible
  name. Upstream tab views do not announce pinned either, so this may be acceptable; uncertain.
- **N15. Startup mode other than "continue".** With Settings > On startup set to open the new tab page,
  Chromium restores Essentials through `PinnedTabCodec` (URL list from prefs), not the session, so an
  Essential comes back with its URL at quit time as the "pinned URL" (uncertain, not run). The default
  (continue) is unaffected (`session_startup_pref.cc.patch`).
- **N16. A layer per row.** `KoanTabRow` calls `SetPaintToLayer()` for every tab row, including the hidden
  ones (the common case). Create the layer when the button first shows (#47 is about no hidden repaints and
  compositing work; uncertain impact).
- **N17. Zen's `Reset Pinned Tab` on an unloaded tab** loads the page (`LoadURLWithParams` on a discarded tab)
  where Zen only rewrites the stored state; only reachable after an away-unload (uncertain).
