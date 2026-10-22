# PR #59 review, round 3 (commit 2b7ea0c "round-2 review fixes" only)

Scope: `git show HEAD` on `feat-zen-pinned-essentials` (on top of develop 110c80d). Read-only review against upstream Chromium 158 at `67e29b8287` (`git show HEAD:<path>`); nothing was built or run.

Safe to merge: **yes**, with the four Should-fix items below tracked. There is no verified crash on a path a user reaches without a race, and no data-loss bug that affects a feature shipping in this PR (no feature uses the window store yet). Counts: Blockers 0, Should fix 4, Nits 3.

## Blockers

None.

## Should fix

### SF1. Pinning a tab now moves it down to the end of the pinned run (regression from round 1)

`src/browser/ui/views/koan_pinned_tabs.cc:612-635` (`MoveAfterPinnedTabs`), called from `PinInSpace` (:822), `Normalize` (:776 and :784-786) and `MatchSplitPinnedState` (:1311).

Round 1's `MoveToPinnedSection` returned when `target >= index`, so it only ever moved a tab up. The new `MoveAfterPinnedTabs` also moves down (`target -= size`) and does not check whether the tab is already inside the run. `LastPinnedInSection` skips the ignored tab with `continue`, so `last` is the last pinned tab of the whole run, even when the tab sits in the middle of it. Verified by reading these paths:

- **Drop among pinned tabs.** The Space has pinned P1 P2 P3 P4 and the user drops normal tab T between P1 and P2.
  - Chromium moves T there, and `Normalize` hits `AbovePinnedTab(last)` and calls `PinInSpace(T)`.
  - `MoveAfterPinnedTabs(T)` then computes `last` = P4 and moves T to after P4: P1 P2 P3 P4 T.
  - Before this commit T stayed where it was dropped.
  - ZEN_PARITY.md:86 still says "Same" for this row (a tab dropped among pinned tabs is pinned). Zen pins it in place.
- **Split view of a non-last pinned tab.**
  - Ctrl+Alt+V on P1 in P1 P2 P3 gives the split [P1 N] P2 P3.
  - `MatchSplitPinnedState` calls `PinInSpace(P1)`, which moves the whole split to the end: P2 P3 P1 N.
  - The user's pinned order changes. The e2e only splits the last pinned tab, so it does not cover this.
- **Restore re-check.**
  - `kInserted` flags every restored pinned-in-Space tab with `changed_group` (:1200). `Normalize` then calls `MoveAfterPinnedTabs` on each (:773-776), not only on tabs that are "not among" the run, as the comment at :774 and the reply to N3 say.
  - Each flagged tab is moved to the end of its run in handle order. This is the identity permutation only if the handle order equals strip order. The result is N pointless `kMoved` notifications per Space on every restart. Any tab flagged out of order, or any pinned tab not flagged, gets permuted.

Fix: the pin paths (`PinInSpace`, the restore and `changed_group` re-check, `MatchSplitPinnedState`) should move up only, or do nothing when `InPinnedRun(first)` already holds. Only the unpin paths (`UnpinFromSpace`, `RemoveFromEssentials`) need the downward move. A `bool allow_down` parameter, or an early return for a tab already in the run, is enough.

### SF2. `MatchSplitPinnedState` checks `GetSplitData()` for null, but upstream CHECKs

`koan_pinned_tabs.cc:1297-1301`.

`TabStripModel::GetSplitData` is `CHECK(split)` and never returns null (`tab_strip_model.cc:1870-1876`). The `if (!data) return;` is dead code. The task is posted from `OnSplitTabChanged` (:1286-1295) with only a `SplitTabId`. If anything removes that split before the task runs, the browser process crashes in the CHECK. Examples are another operation in the same task chain that dissolves the split (an extension `tabs.*` call, a move or group operation that breaks contiguity), or a tab closed in the same turn.

I found no in-tree sequence that does this deterministically. Within `SetEssential` and `MoveAfterPinnedTabs` the new split survives. It is a race, but the fix is one line and `ContainsSplit()` is public (`tab_strip_model.cc:1882`):

```
if (!tab_strip_model()->ContainsSplit(split)) return;
```

The `weak_factory_` guard covers the controller being destroyed, not the split.

### SF3. Window data restored by `OnRestoringWindow` is never written to the new session

`src/browser/ui/views/koan_session_data.cc:214-227`; patches `session_restore.cc.patch` and `browser_live_tab_context.cc.patch` (the `Create` hunk).

`OnRestoringWindow` fills the in-memory store (`*store = koan_data`) but does not call `SessionService::AddWindowExtraData`. A restored or reopened window has a new SessionID.

- `BuildCommandsForBrowser` runs only from `BuildCommandsFromBrowsers`, i.e. `ScheduleResetCommands`. Callers are session service creation, `MoveCurrentSessionToLastSession`, and `kWritesPerReset` (250 commands). None runs after the restored windows exist.
- `SessionService::WindowOpened` writes only type, app name, title, workspace and visibility (`session_service.cc:419-439`).
- The per-tab store handles this case: `SessionService::TabRestored` calls `BuildCommandsForTab`, which includes `koan::GetTabSessionData`. There is no equivalent for windows.

Failure scenario, once folders (#60) store data here:
1. Restart 1 restores the window's folders from the last session into the store.
2. The user quits with fewer than 250 session commands written and no folder change.
3. The new session file has no `koan.*` window entry.
4. Restart 2 comes up with no folders.

The same happens for a reopened closed window.

Fix: in `OnRestoringWindow`, write each restored entry with `AddWindowExtraData(browser->GetSessionID(), key, data)`. This is the window counterpart of what `TabRestored` does for tabs.

### SF4. The window hook is skipped when the first window is restored into the existing browser

`session_restore.cc.patch` hooks only `CreateRestoredBrowser`. Upstream restores the first window's tabs into `browser_` when `ShouldRestoreToExistingBrowser()` holds (`session_restore.cc:880-881`: `browser = browser_;`), so `CreateRestoredBrowser` is not called for that window.

- The path is reached with a visible browser, for example "Restore" in the crash bubble (`RestoreSessionAfterCrash(browser)`) and `SessionService::RestoreIfNecessary(..., browser)`.
- For that window, `GetWindowSessionData` is empty and the saved window data is dropped. The next `BuildCommandsForBrowser` then omits it from the session file as well, so it is lost for good.
- `koan_session_data.h` documents the hook as "called right after the window is created by SessionRestore ... before its tabs are restored", which is not true for this window.

Fix: also call `koan::OnRestoringWindow(browser_, window->extra_data, true)` in the `browser = browser_` branch (a second hunk in the same patch). The window store should then be overwritten or merged deliberately, because that browser may already have state.

## Nits

- **N1.** `InitPinnedState` (:496-513) now returns `true` whenever it ran, even if no URL was available and `pinned_url` stayed invalid (a blank tab with an empty visible URL). `kInserted` then sets `unsaved`, and `OnTabsAdded` writes an empty entry. This is harmless, but returning `state.pinned_url.is_valid()` would avoid the useless session write.
- **N2.** `TabDragController::IsActive()` is process-wide and is true for any drag in any window of the process, and also from mouse-down. `Normalize` in an unrelated window therefore waits too. It is bounded by the drag and not a bug, but the comment says "the user still holds a dragged tab". Say "any tab drag".
- **N3.** `.agent/notes/reviews/pr59-review-round2.md` is committed inside the feature commit, which adds 269 lines to the PR diff unrelated to the feature. If notes are meant to live in the repo this is fine; otherwise move it out.

## Checked, no issue found

- **`SetEssential()` and `RestoreSplit` preconditions.**
  - `RestoreSplit` has two CHECKs (`indices` sorted, size 2) and `AddToSplitImpl` has `CHECK(!tab->IsSplit())`.
  - `SetTabPinned` dissolves the split. A pinned-state change makes `MaybeRemoveSplitsForMove` call `RemoveSplitImpl` before the move.
  - In the unpin direction the code reverses the tabs. Unpinning the second tab first, then the first, puts both at the end of the pinned region, adjacent: `final = IndexOfFirstNonPinnedTab() - 1` each time. I traced this for a split at the end of the Essentials, in the middle, and followed by another Essentials split.
  - In the pin direction both tabs land at P and P+1.
  - The `indices[1] == indices[0] + 1` and `!tabs.front()->IsSplit()` guards mean `RestoreSplit` is never reached non-adjacent or with a still-split tab. Even then, `AddToSplitImpl` gathers non-adjacent tabs at the pivot rather than CHECKing.
  - `AddToExistingGroup` gets a sorted, unique, in-range `indices`. `MaybeRemoveSplitsForUpdate` leaves the restored split alone because both tabs are passed.
  - An Essentials split cannot be half pinned: `AddToSplitImpl` forces both tabs to the pivot's pinned state. The destination-contiguity check in `MoveTabToIndexImpl` compares the last pinned tab with the first unpinned one, which can never be the same split.
- **`LastPinnedInSection` extension and `MoveSplitTo`.**
  - `MoveSplitTo` CHECKs `!MoveBreaksSplitContiguity(start, len, to_index)`.
  - With `to_index` taken as the final index (which is what upstream means), `MoveBreaksSplitContiguity` compares the tabs on either side of the insert point. `last + 1`, after extending `last` to the end of its split, is never between two tabs of one split. `target = last + 1 - size` agrees with it for the downward move.
  - `last == -1` gives `SectionStart`, the first tab of the Space's group or the first non-pinned tab, which is a group or pinned boundary. `ConstrainMoveIndex(…, false)` cannot push it into a split.
- **`Normalize()` re-arming (item 2).**
  - It cannot spin forever unless a `TabDragController` stays active: `active()` is `state != kStopped`, and the controller clears `g_tab_drag_controller` in its destructor.
  - `SessionRestore::IsRestoring` ends when `SessionRestoreImpl` deletes itself (`session_restore.cc:411` and `:558`), which happens after the windows are processed, not after tabs load.
  - `normalize_timer_` is a member `OneShotTimer`, stopped in `OnBrowserViewDestroying` (:484) and destroyed with the controller, so it does not outlive the window.
  - Handles stay queued across re-arms, and `done` stops a tab being processed twice.
  - The `//chrome/browser/ui/views/tabs/dragging` dep exists, and `IsActive()` is a public static.
- **Patches.**
  - `git apply --check` against pristine files taken with `git show HEAD:<path>` from upstream passes for all 74 patches in `patches/`, including the 14 touched by this PR.
  - The `index` line of each of the 14 PR patches matches the pristine blob hash.
  - `session_restore.cc.patch` has two hunks (one include, one 3-line call). `BuildCommandsForBrowser`, `GetExtraDataForWindow` and `Create` each add a few lines.
  - `CreateAddWindowExtraDataCommand` takes `const std::string&`, `browser_` in `BrowserLiveTabContext` is a `raw_ref`, and `extra_data` is in scope at each hook.
  - `session_restore.cc` and `session_service_base.cc` are both in the `sessions:impl` source_set, which the existing BUILD.gn patch covers.
- **Window store lifetime.**
  - `KoanBrowserViewController` is created at the end of the `BrowserView` constructor and destroyed at the start of `~BrowserView`.
  - `BrowserView::GetBrowserViewForBrowser` is null-safe while the window is still being constructed. `WindowStore` handles a null view and a null controller.
  - Windows without Koan UI (apps, popups) get an empty map.
  - `TabRestoreService::BrowserClosing` runs from `UnloadController`, before the view is destroyed, so `GetExtraDataForWindow` still sees the store.
- **S5.**
  - `unsaved` is consumed in `OnTabsAdded`, after `SessionTabHelper` has the window id.
  - A discard (`kReplaced`) copies the state and posts `OnTabsAdded` again.
  - `CanAddToEssentials(0)` equals the `Normalize` limit condition `EssentialsCount() > 12`.
- **`OnSplitTabChanged` when the tab is gone.** `MatchSplitPinnedState` reads the tabs from the split's own data, so there is no stale tab handle. The only stale-state risk is the split id (SF2).
