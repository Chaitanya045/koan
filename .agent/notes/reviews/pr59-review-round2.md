# Round-2 review of PR #59: pinned tabs and Essentials (fixes #38)

Branch `feat/zen-pinned-essentials` at `a95a967` (6 commits), diff `origin/develop...HEAD` (merge base `8b8490b`,
56 files, +4500/-79). Read-only: nothing was built or run. Everything below was read against upstream Chromium
158.0.8111.0 (`~/work/koan/chromium/src` at `67e29b82`, the commit in `CHROMIUM_VERSION`). Findings that depend on
runtime behaviour I could not observe say so.

Paths: `koan_pinned_tabs.cc` is `src/browser/ui/views/koan_pinned_tabs.cc`; other `koan_*` files are in the same
directory unless a path is given; upstream paths are relative to `chromium/src`.

## Verdict

No blockers. B1 and B2 are fixed, the pinned audit is complete for every path I could find, and the patches are
sound. There are 6 should-fix items (one is a reachable browser crash, one silently resets a tab on restart, one
is a gap in the shared API that #60 will hit). Mergeable, but I would fix the first four before #57 and #60 build on it.

## Checked and fine

**B1 (back-to-opener)**: fixed. `ShouldCloseTabOnBack()` (`src/browser/koan_tab_hooks.cc:65-71`) now ends in
`!IsPinnedForUser(contents)`. The existing back-to-opener patch routes both `CanGoBackToOpener()` and
`HasValidOpener()` through it (`back_to_opener_controller.cc` patch hunks at upstream lines 252 and 278), so Back, the back button
state and the back/forward menu agree. Upstream's own `is_pinned_` still covers Essentials.

**B2 (Close Other / Close Tabs Below)**: fixed. `tab_strip_model.cc.patch` adds `koan::IsPinnedForUser()` beside
`IsTabPinned(i)` in `HasIndicesClosedByCommand` (upstream `:3839`) and beside `tab->IsPinned()` in
`GetTabsClosedByCommand` (`:3877`). These are the only two callers of the pair (`ExecuteContextMenuCommand`
`:2905-2930`, `IsContextMenuCommandEnabled` `:2698`, and `chrome::CanCloseTabsToRight/CanCloseOtherTabs` both go
through `IsContextMenuCommandEnabled`), so the enabled state and the action agree.

**Flag off (`kKoanPinnedTabs`)**: the new `tab_strip_model.cc.patch` changes nothing.
- `IsPinnedForUser(tab)` is `tab->IsPinned() || (IsPinnedTabsEnabled() && IsPinnedInSpace(tab))`
  (`koan_pinned_tabs.cc:419-422`), i.e. exactly `IsTabPinned(i)` / `tab->IsPinned()` with the flag off.
- `ExecuteKoanTabMenuCommand` runs for every command, but `ExecutePinnedTabMenuCommand` returns false when
  `KoanPinnedTabController::ForTab()` is null (flag off, `:452`); `AddPinnedTabMenuItems` and
  `IsPinnedTabMenuCommandEnabled` do the same. The split view feature is the only other entry and behaves as in #54.
- The same holds for the other hooks: `OnTabViewCreated` (`koan_pinned_tab_views.cc:676`), `AdjustTabData`
  (`koan_pinned_tabs.cc:1399-1400`), `OnRestoringPinnedTab` (`koan_pinned_tabs.cc:1423`), `HandleCloseTabCommand` / `HandleTabCloseRequest` (via `ForTab`).
  The session hooks only add or restore keys that start with `koan.`, so they are no-ops without data.
- The #54 split hook moved from the middle of `TabMenuModel::Build()` to its end. `AddZenSplitTabMenuItems` finds
  its items by command id (`koan_split_view.cc:26-37`) and `Build()` has no early return, so the menu is unchanged.

**Audit of upstream pinned special-cases**: complete as far as I can find. I grepped `IsPinned()` / `IsTabPinned(`
in `chrome/browser/ui`, `chrome/browser/performance_manager`, `components/performance_manager` and
`chrome/browser/resource_coordinator`:
- Close commands: the two above. `CommandCloseAllTabs` (`:3207`) only keeps the pinned *home* tab and is not in
  Koan's menu. `chrome::CloseTab` (`browser_commands.cc:1515`, the "close pinned tab" toast) comes after Koan's hook.
- Back-to-opener: above.
- `PageLiveStateDecoratorHelper` (`page_live_state_decorator_helper.cc:170-193`) resets `IsPinnedTab` on insert,
  replace and `OnTabPinnedStateChanged`. Koan re-sets it after each: `OnTabsAdded` (posted from `kInserted` and
  `kReplaced`), `PinInSpace`, `ClearPinnedState`, `Normalize`. Its consumers are `DiscardEligibilityPolicy:168,412`
  (Memory Saver; `EXTERNAL` discards ignore it, so Koan's own Unload still works) and Koan's freezing patch
  (`OnIsPinnedTabChanged`).
- Clear and Ctrl+Shift+K: `UnpinnedTabsForClear`, `UpdateClearRow`.
- Duplicate (`browser_commands.cc:402`): Essential gets `ADD_PINNED` and the cap; a Space-pinned tab's duplicate is
  a normal tab moved below the run by `OnTabsAdded`.
- Drag: `Normalize` (see S2 for its flaw). `TabStripModel:1387` (new tabs go after pinned tabs) is handled by `OnTabsAdded`.
- The remaining `IsPinned()` sites (`browser_commands.cc:845,1951` group-all, `:1837` split NTP, `tab_strip_model.cc`
  focus-mode and move constraints, `restartability_monitor.cc`) do not need the Zen kind.

**S1, S2, S3 (hook), S6, S7 (power.mjs)**: fixed as claimed.
- S1: `RunCloseBehaviour` compares the exact URL (`:1028`); the menu items are enabled for any pinned tab;
  restore compares the exact URL (`:1433-1436`).
- S2: `CLOSE_USER_GESTURE | CLOSE_CREATE_HISTORICAL_TAB` (`:1011-1013`).
- S3: the `GetExtraDataForTab` hook (`browser_live_tab_context.cc.patch`) merges the session store into upstream's
  map (`std::map::merge(map&&)`), is called before the tab is destroyed, and the data returns through
  `CreateRestoredTab` with `from_session_restore == false` (`browser_live_tab_context.cc:383,442,470`), so
  restore-to-pinned-URL correctly does not apply. `OnRestoringTab` runs after the entries are built and before
  `NavigationController::Restore`, and `selected_navigation` is a by-value `int`, so the hook may change it.
- S6: all three ways in are covered once `Normalize` runs: the menu/palette (`CanAddToEssentials`), a drag or an
  extension/command pin (`OnTabPinnedStateChanged`), and Duplicate (`kInserted` schedules it). With exactly 12 the
  check is `> 12`, so nothing is evicted. The victim is the tab scheduled first, which in each of these paths is
  the new one.
- S5 lifetimes (excluding the findings below): tab closed during the debounce: `TabHandle::Get()` is null, and a
  tab detached for a cross-window drag has `GetIndexOfTab() == kNoTab`, both skipped. Window close:
  `OnBrowserViewDestroying` (`:463-469`, called from `KoanBrowserViewController::DestroyFor` `:84`) stops the
  timer and removes the observer; the timer is a member, so it cannot outlive the controller. `Normalize` iterates a
  local copy, so moves it triggers only re-arm the timer.

**S4 pending notion, S9 pin path, N-series**: `IsPending()` (`WasDiscarded() || NeedsReload()`) is used for the
close request, Unload, `Discard()` and the reset. A discarded-replace carries `KoanTabState` through `kReplaced`
and the generic store through `AboutToBeDiscarded` (exists in 158, `web_contents_observer.h:1040`; the only caller
is `tab_lifecycle_unit.cc:299`, and the discard is the only `kReplaced` source in 158).

**Patches**: the 13 patches added or changed by the PR (and all 68 in the tree) pass `git apply --check` against
pristine `git show HEAD:<path>` files in a scratch dir, with no offset or fuzz (`patch --dry-run` on the 13).
Each new GN dep is in the target that holds the patched source: `sessions:impl` (session_service_base.cc),
`browser_tabrestore_impl`, `browser_live_tab_context_impl`, `browser_commands_impl`, `tab_menu_impl`,
`tab_strip_impl`, `ui/tabs:impl` (tab_data.cc), and `views/tabs/common:impl` already had the dep (tab_view.cc,
tab_strip_collection_controller.cc). No GN cycle: upstream only depends on the header-only `//koan/browser/ui/views:views`,
whose headers need only `base`, `ui/base`, geometry and `ui/menus`; `:impl` is linked through `chrome/browser:browser`.
The hunks are minimal (one call or one include per site). All 13 are covered by REBASE.md rows (new rows at
`docs/REBASE.md:114-120`, the merged "Tab context menu" row, and the "Shared hooks" section at `:33`).

**macOS**: no Linux-only code in `src/`; the only platform use is `EF_PLATFORM_ACCELERATOR` (Cmd) for the reset
button's separate-page click (`koan_pinned_tab_views.cc:500`). Cmd+W and the menu bar's Close Tab go through
`IDC_CLOSE_TAB` -> `chrome::CloseTab`, which has the hook. The e2e scripts are test-only.

---

# Blockers

None.

---

# Should fix

## S1. A reset applied when the tab is selected is never saved, so the next restart resets the tab again (S4 persistence)

- Where: `koan_pinned_tabs.cc:795-808` (`ResetToPinnedUrl`) and `:1139-1148` (selection handler); `:1441-1446`
  (restore).
- Problem: resetting a tab that is not loaded sets `reset_pending = true` and saves it (`:798-799`). When the
  tab is selected, the selection handler calls `ResetToPinnedUrl` again. Now the tab is activated, so it takes the
  normal branch: `state->reset_pending = false` (`:803`) and `LoadURLWithParams`, with no `SaveTabState`. The
  session's `koan.pinned_tab` entry (and the in-memory `KoanTabSessionData` the session rebuild writes from) still
  says `"reset":true`. `OnRestoringPinnedTab` honours that flag whenever the restored page is not the pinned URL.
- Scenario: restart (every non-visible pinned tab is now unloaded). Right-click an away pinned tab >
  "Reset Pinned Tab" (always enabled): `reset_pending` is saved. Select the tab; it loads the pinned URL. Browse
  to another site and quit. On the next start the tab is replaced by a single entry at the pinned URL
  (`:1441-1445`), losing where the user was. It happens again after every restart for as long as nothing else
  saves the state. The same stale flag survives in a closed tab's extra data (S3), so reopening it resets it too.
- Fix: `SaveTabState(contents)` when `reset_pending` goes from true to false at `:803`. More generally, the
  generic store holds a serialized copy that every mutation must remember to write (see S5 and S6); make
  `KoanTabState` setters write through, or save in `OnTabStripModelChanged` after the selection reset.
  e2e: reset an unloaded tab from the menu, select it, navigate, restart.

## S2. `Normalize()` is time-based only, so it fires while the user is still holding the dragged tab

- Where: `koan_pinned_tabs.cc:636-640` (`ScheduleNormalize`, `kNormalizeDelay = 400 ms`), `:642-687`,
  `:1118-1120`, `:1170-1188`.
- Problem: upstream moves the model on every drag step (`tab_drag_handler.cc:404,916-919`
  `MoveSelectedTabsTo`, `tab_drag_controller.cc:2454` revert), and nothing stops the drag from pausing. The timer
  restarts on each `kMoved`, but a pause of 400 ms with the button down (hovering to decide where to drop is
  normal) lets `Normalize` run while the drag is active (`TabDragHandler::IsDragging()` is available but not
  consulted). It then mutates the dragged tab: `ClearPinnedState` (unpin, `pinned_url` and icon wiped),
  `PinInSpace` (`MoveWebContentsAt`), or `RemoveFromEssentials` (`SetTabPinned(false)` + `AddToExistingGroup`) on a
  tab the drag controller is moving. The drag controller guards its own mutations with `is_mutating_` but only
  tracks `kRemoved` / `kReplaced` from others; what a third-party move does to an active drag I could not confirm.
- Scenario (verified from the code, the drag outcome not run): a pinned Gmail tab that is away from its pinned URL
  is dragged below the first normal tab and held for half a second: `Normalize` clears its pinned state and URL.
  The user drags it back into the pinned tabs and drops it: `AbovePinnedTab` -> `PinInSpace` -> `InitPinnedState`
  pins the *current* page. The original pinned URL is lost. A 13th tab dragged over the tiles and held gets
  ejected from the Essentials (`:656-659`) while it is still in the user's hand.
- Fix: in `Normalize`, if the tab strip's drag handler `IsDragging()` (or `TabDragController::IsActive()`), re-arm
  the timer instead of acting. The end of the drag then needs its own trigger (a periodic re-check while the flag
  is set, since a drop itself sends no model notification). The reply says synthetic drags do not reach the drag
  handler, so this stays untested by e2e; say so in the PR.

## S3. Pinning a tab next to a split that has one pinned and one normal member hits upstream's `CHECK` (browser crash) or dissolves the split

- Where: `koan_pinned_tabs.cc:547-566` (`LastPinnedInSection`), `:593-612` (`MoveToPinnedSection`, the
  `MoveSplitTo` call at `:607`); upstream `tab_strip_model.cc:1141-1177` (`CHECK(!destination_split.has_value())`
  at `:1165`), `:6087-6110` and `:6144` (single-tab moves remove the destination split instead).
- Problem: `LastPinnedInSection` returns the index of the last pinned-in-Space tab and the target is `last + 1`.
  If that last tab is one member of a split whose other member is not pinned, the target index lies between the two
  members. Koan can create such a split: pin tab A, then use the Split shortcut or "Add Split View..." with the
  next tab B (`koan_shortcuts.cc:362` compares only Chromium's pinned state, so A and B match). `PinInSpace`
  only takes a split along when pinning, not when splitting.
- Scenario: A (pinned) + B (normal) in a split; later pin a split pair S3+S4 (Ctrl+Shift+D): `MoveSplitTo(split,
  index_of(B), ...)` -> `MoveBreaksSplitContiguity` returns A+B's split -> `CHECK` fails, the browser crashes.
  Pinning a single tab C instead calls `MoveWebContentsAt(c, index_of(B))`, which silently dissolves A+B's split
  (`MaybeRemoveSplitsForMove`).
- Fix: in `LastPinnedInSection` / `MoveToPinnedSection`, if `last` is in a split, extend `last` to the end of that
  split (or treat a split as pinned when any member is, and pin both members when a split is formed with a pinned
  tab). Add an e2e check for pin-after-mixed-split.

## S4. S9 is only half fixed: unpinning a pinned split leaves it inside the pinned run, and the Essentials operations still take a split apart

- Where: `koan_pinned_tabs.cc:735-750` (`UnpinFromSpace`, the `!tab->IsSplit()` guard at `:747`); `:716`, `:762`,
  `:774` (`SetTabPinned` on a single tab); upstream `tab_strip_model.cc:1429-1437`, `:5544-5553`
  (`SetTabPinnedImpl` -> `MaybeRemoveSplitsForMove`, which removes the split of a tab whose pinned state changes;
  `SetTabsPinned` `:5495-5540` is the split-aware variant).
- Problem 1: `UnpinFromSpace` clears both members of a split but does not move them below the run (it moves only
  non-split tabs). If other pinned tabs follow the split, they are now behind two normal tabs.
- Scenario 1: pin split pair S1+S2, then pin X (it goes after the pair). Unpin S1: the order is S1, S2, X with only
  X still `pinned_in_space`. `InPinnedRun(X)` is false and `LastPinnedInSection` now returns -1, so X shows a pinned
  row among normal tabs, new pins go above S1, and the next move of X (any drag of one row) unpins it and wipes its
  pinned URL.
- Problem 2: "Add to Essentials", "Remove from Essentials", and Pin/Unpin of an Essential call
  `SetTabPinned(index, ...)` on one tab. For a tab in a split that dissolves the split (upstream's own menu uses
  `SetTabsPinned`). `docs/ZEN_PARITY.md:73` says "Pinning or unpinning a tab of a split view takes the other tab
  along" without that limit.
- Scenario 2: split view with A+B; right-click A > Add to Essentials: A becomes an Essential tile, B stays in the
  Space, and the split is gone.
- Fix: move the pair with `MoveSplitTo` in `UnpinFromSpace`; use `SetTabsPinned({indices of the split})` in the
  three Essentials paths (or `SetSplitPinnedImpl`'s public equivalent). Scope the docs row until then.

## S5. An Essential that gets its pinned URL on insert is never saved, so the pinned URL changes at the next restart

- Where: `koan_pinned_tabs.cc:1096-1109` (`kInserted`: `InitPinnedState` at `:1101`, no `SaveTabState`); compare
  `:1177-1182` (`OnTabPinnedStateChanged`) and `:752-765` (`AddToEssentials`), which save.
- Problem: any Chromium-pinned tab that arrives without Koan data gets `pinned_url = <its URL now>` in memory only.
  That covers Duplicate of an Essential (`ADD_PINNED`), a tab pinned by an extension that does not go through
  `OnTabPinnedStateChanged`'s save, and every Essential created before this PR (#50's pinned tiles). Nothing writes
  it to the session afterwards.
- Scenario: a user upgrades with two Essentials from before this PR. Open Gmail Essential, navigate to another page,
  quit. Next start: no data in the session, so `InitPinnedState` adopts the away page as the pinned URL; no reset
  button, Ctrl+W "resets" to the away page. Same for the duplicate of an Essential.
- Fix: `SaveTabState` in the `kInserted` branch when `InitPinnedState` actually initialised (it returns early if a
  URL is set). e2e: duplicate an Essential, navigate it, restart.

## S6. The shared session API has no per-window counterpart, so #60 cannot adopt it as-is (and the three PRs conflict on the same patch files)

- Where: `koan_tab_session_data.h` (per-tab only); #60's `session_service_base.cc.patch` and `session_restore.cc.patch`
  (`CreateAddWindowExtraDataCommand` in `BuildCommandsForBrowser`, `RestoreFolderSessionData` keyed by tab index
  within the window), `GetFolderSessionData` in `koan_folder_controller.cc` (a single `"koan.folders"` blob).
- Problem: the reply says folders "can key membership by tab instead of tab index". Membership can, but a folder's
  own state (id, name, collapsed, parent, an empty folder) belongs to the window, and #60 stores it that way today.
  #59's API offers no window-level data, no hook in `BuildCommandsForBrowser`, `RestoreTabsToBrowser`, or
  `BrowserLiveTabContext::GetExtraDataForWindow` (a reopened window keeps its tabs' data but no window data).
  #60 will have to keep its two session patches, which collide with #59's files of the same name
  (`patches/chrome/browser/sessions/BUILD.gn.patch` and `session_service_base.cc.patch` are *new files* in both PRs).
- Scenario: #60 rebased on #59 with only per-tab keys: folders with no tabs, folder names and collapsed state are
  lost on restart (or stay on the old index-keyed path and drift when tabs are reordered between save and restore).
- Fix: add `SetWindowSessionData` / `GetWindowSessionData(browser, key)` with the three hooks to #59's REBASE.md
  section, or state in "Shared hooks" that window-level data is out of scope and #60 owns those hooks. Either way,
  list the collisions for the rebase: #57 and #60 both add `koan_tab_menu.{h,cc}` and rewrite
  `tab_menu_model.cc.patch` / `tab_strip_model.cc.patch`; #60's "Move to Folder" id is 3002, inside split view's
  range (3000-3099), and must move to `TabMenuFeatureId::kFolders` (3300+).

### Generality of the shared API (item 3)

- Tab menu: general enough for #57 and #60. A feature registers a `TabMenuFeature` (`add_items`, `is_enabled`,
  `execute`) in `kFeatures` with its own 100 ids; `FeatureOf` routes by range; `execute` sees every command, so a
  feature may override one of upstream's (as Pinned does for Pin/Unpin); `AddSubmenu()` gives a submenu whose
  commands go through the same path. This covers #57's "Move To" (it replaces upstream's group items found by
  command id; up to 99 Spaces) and #60's "Move to Folder". Limits: each feature needs an edit in two shared files
  (`kFeatures` here, the id enum in the header), and there is no dynamic checked/visible state (not needed yet).
- Pinned check: `IsPinnedForUser(tab | contents)` is what both need, and a pinned tab moved to another Space keeps
  its place at the top through `Normalize`. The only wrinkle is where it lives: `koan_pinned_tabs_hooks.h` is the
  hooks header of one feature (nit N5).
- Per-tab session data: general for per-tab state (S6 above for the rest). `SetTabSessionData` stores a
  serialized copy, so each feature must remember to call it after every mutation; S1 and S5 are that mistake
  made twice in the one feature written against it.
- Documented: REBASE.md "Shared hooks for Koan features" (`docs/REBASE.md:33-66`) describes all three accurately and
  lists the three session hooks and the id ranges.

---

# Nits

- **N1. Palette "Reset Pinned Tab" is still gated on the fragment-insensitive "away" test.**
  `koan_commands.cc:248` adds the command only when `IsAwayFromPinnedUrl(tab)`, which uses `LeftPinnedUrl`
  (ignores the fragment). For Gmail at `#sent` (pinned at `#inbox`) the menu and the shortcut reset it but the
  palette does not list it, the case S1 of round 1 was about. Gate on `IsPinnedForUser` + a valid pinned URL.
- **N2. A restore-time reset at the pinned URL keeps back history.** `OnRestoringPinnedTab` (`:1433-1445`) skips
  replacing the entries when the selected entry is already the pinned URL, so a `reset_pending` or
  restore-to-pinned-URL tab with three entries keeps all three (Zen's reset leaves one). Replace the entries
  whenever there is more than one.
- **N3. Pending normalization is dropped on close, and nothing normalizes at startup.** `OnBrowserViewDestroying`
  stops the timer (`:463-469`). Quitting within 400 ms of a drop persists the un-normalized state (e.g. a
  `pinned_in_space` tab below normal tabs), and no code re-checks the run after a session restore, so it stays
  until that tab is moved. Run one `Normalize` for the window's pinned tabs after a restore.
- **N4. REBASE.md has no step to re-audit new upstream pinned checks.** B1 and B2 were exactly "a new
  `IsPinned()` / `IsTabPinned()` call site the Zen pinned kind does not know". `docs/REBASE.md:114` lists only the
  known sites. Add to "After every rebase" a `git grep -nE 'IsTabPinned\(|->IsPinned\(\)'` over
  `chrome/browser/ui`, `chrome/browser/performance_manager` and `components/performance_manager`, diffed against the
  previous revision.
- **N5. Placement.** `IsPinnedForUser` (shared) is declared in the pinned-tabs hooks header, which `tab_hooks`
  now has to depend on (`src/browser/BUILD.gn:72`); a small `koan_pinned.h` would keep #57/#60 from depending on a
  feature's hook list. `IsPinnedTabsEnabled()` is declared under `IsGlanceActivationClick`'s comment
  (`koan_features.h:47`) instead of with the other `Is*Enabled()` functions.
- **N6. Three existing patches were rewritten without `scripts/apply_patches.sh export`'s format.**
  `tab_strip_model.cc.patch`, `tab_menu_model.cc.patch` and `ui/tabs/BUILD.gn.patch` lost their
  `diff --git` / `index` lines and the `@@` function context, while `export` writes `git diff HEAD --full-index`
  (`scripts/apply_patches.sh`, `export` mode). They apply, but the next `export` rewrites every hunk header, which
  makes the next real change harder to read. (19 of the 68 patches are headerless already.)
- **N7. ZEN_PARITY says "At most 12 Essentials: Same"** (`docs/ZEN_PARITY.md`) while the row itself says Zen
  refuses the drop and Koan accepts it and ejects the tab 400 ms later. "Different".
