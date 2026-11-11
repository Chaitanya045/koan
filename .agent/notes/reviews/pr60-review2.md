# Review of PR #60, round 2: tab folders like Zen's (fixes #42)

Head `2dd65de` on `feat/zen-folders`, rebased on develop `eebe874` (merge-base = origin/develop, so the diff is clean).
Worktree `~/.t3/worktrees/koan/feat-zen-folders`, read only. Nothing was built or run.
The author's reply is in `gh pr view 60 --comments` (two comments: round 1 answers, then the #59 migration).
"New Folder" in the #57 Create menu is deferred, as agreed, and not counted.

Method: `git diff origin/develop --stat` and the full diff; the controller, views and hooks read in full; checked against
upstream (`~/work/koan/chromium/src` at 67e29b8) and the Zen sources (`.cache/zen-src`). Both layout patches were tested with
`git apply --check -p1` against pristine upstream files and apply cleanly.

## Totals

- Blockers: 0
- Should-fix: 2
- Nits: 4

Round 1: B1, S1, S2, S3, S4, S5, S6, S7 and N1 to N11 are FIXED. N12 is PARTIAL (accepted by the author, see below).

---

## 1. Round-1 items

### B1. Delete Folder / Change Space / Convert to Space emptying a Space: FIXED

- `KeepSpace()` at `src/browser/ui/views/koan_folder_controller.cc:563-597`. It counts the Space's tabs minus the leaving tabs.
  If none would remain it adds a tab at the end of the group with `chrome::AddTabAt(..., space)`.
  It is foreground only when the closing set holds the active tab.
- Called from `Delete` (`:605`, `closing=true`), `ChangeSpace` (`:633`) and `ConvertToSpace` (`:668`).
- Without a Space, `closing=true` counts the whole model, so the window stays open (`:572-573`, `:589`).
- It is not called for ungrouped tabs moving into a Space (`:568-571`), which is right.
- `ConvertToSpace` with an empty folder adds a tab first (`:656-667`).
- E2E: `scripts/e2e/folders.mjs:246-282`.

### S1. E2E gaps: FIXED, with gaps left as nit N4

- `scripts/e2e/folders_drag.mjs` drags with real X11 input (`lib/xdrag.py`). It covers in, out, and loose to loose.
  It skips without Xvfb.
- Clear: `folders.mjs:283-306`. Remove from Folder: `:311-315`. Delete: `:231-245`.
- Unpack checks the tab is kept: `:178-183`. Unload checks discarded: `:186-201`.
- Restore twice, with a Space, a collapsed folder and a subfolder: `:356-405`. Close during rename: `:408`.
- Not covered: double-click rename (author says UI DevTools cannot send it).

### S2. Session restore by tab index: FIXED

- Each folder tab carries its folder's random token under `koan.folders` in the per-tab store: `Save()` `:1390-1404`.
- The folder tree is in the per-window store: `Serialize()` `:1406-1431`, written with `SetWindowSessionData` at `:1393`.
- A tab that joins by token as it is inserted: `OnTabsAdded` `:1143-1154`.
- A Space is mapped by its place among the window's Spaces, but only for folders with no tabs. Folders with tabs go where their
  first tab comes back: `FinishRestore` `:1156-1186`. This removes the index shift.
- A folder with no tabs left (all dropped by the session) still lands in the right Space by place.
- `restoring_` blocks `Prune` until the tabs are in, so `FinishRestore` is posted first in the same task: `:1289`, `:1445-1447`.
- `Restore()` is idempotent, because duplicate tokens are skipped (`:1453-1455`).
- Parent order: `Serialize` writes in `folders_` order. That order always has parents first, because folders are only appended
  and `Unpack` / `ChangeSpace` re-parent upwards or to 0. `Restore` relies on it (`:1464-1469`).

### S3. Folders active with horizontal tabs: FIXED

- `IsEnabled()` (`:239-246`) also needs `ShouldDisplayVerticalTabs()`. A mode-change subscription (`:252-257`) calls `Changed`.
- Gated by `IsEnabled()`: `OnTabMoved` (`:1033`), `Normalize` and `SyncRows` (`:1295`), the layout hooks (`FromView` `:209`),
  tab menu (`FromBrowser` `:221`), palette (`koan_commands.cc:347`) and the "+" menu (`koan_workspaces.cc` `OnNewButtonPressed`).
- Folders are still kept and saved when horizontal.

### S4. Clear row: FIXED

- `UnpinnedTabsForClear` (`src/browser/ui/views/koan_shortcuts.cc:467-473`) excludes `KoanFolderController::IsInFolder`.
- The sidebar button (`koan_sidebar_tabs.cc:569`) and the action (`koan_shortcuts.cc:426`) both use it.
- The earlier filter in `CloseUnpinnedTabs` is gone.

### S5. Conflicts with #57 / #59: FIXED (the #57 part is deferred)

See section 2.

### S6. Rename blur lifetime: FIXED

- `weak_factory_` is the last member (`koan_folder_views.h:105`). The blur callback is bound through it (`koan_folder_views.cc:209-211`).
- The destructor clears the field's controller (`:224`).
- The posted `Rename` is bound to the controller `WeakPtr` (`:277-280`).
- E2E: `folders.mjs:408`.

### S7. Zen screenshots: FIXED

- `docs/screenshots/zen-folders-{open,collapsed-active,search}.png` and Koan's counterparts are added.
- `docs/ZEN_PARITY.md` has a "Folders" section stating what matches and what differs.

### Nits from round 1

- N1 REBASE: FIXED. The "Folder rows" row records the layout-manager cast, the `is_collapsed()` substitution, the `set_y`
  move and what `Place` reads.
- N2 patch headers: FIXED. Both patches are in git format with `index` lines and hunk context.
- N3 drag end of folder: FIXED as documented. See also Should-fix 2.
- N4 pressed colour: FIXED (`koan_folder_views.cc:347`). The name colour stays `kColorToolbarText`, justified in the reply.
- N5: FIXED. ZEN_PARITY "Folder icon" row.
- N6: FIXED (`GetVisibleURL()` at `koan_folder_views.cc:448`).
- N7: FIXED. `ShowSearch` skips while a `Textfield` has focus (`koan_folder_controller.cc:932-935`).
  `IsViewClass` walks parent metadata, so the omnibox view counts.
- N8: FIXED. `MoveTabsToFolder` expands a collapsed folder unless it has the active tab (`:729-732`).
- N9: FIXED. ⌘ forms are in the docs.
- N10: FIXED, by the token de-duplication rather than a `restored_` flag.
- N11: FIXED. `UnloadAllTabs` ranks by same Space, then last active (`:690-721`).
- N12: PARTIAL, accepted. The name now says ", showing the active tab". Level information is not done.

---

## 2. Shared-hook migration (#59), point by point

- **Per-tab session store.** FIXED. The folder token is stored with `SetTabSessionData(contents, kFolderSessionKey, token)`
  (`:1400-1401`) and read with `GetTabSessionData` (`:1147`).
- **Per-window session store.** FIXED. `SetWindowSessionData(browser, kFolderSessionKey, data)` (`:1393`).
  `OnRestoringFolders` is registered as a `WindowRestoreHook` (`koan_session_data.cc:41-43`).
  The old placeholder comment is removed.
- **Own session patches dropped.** FIXED. `git diff origin/develop --stat` shows no `sessions/BUILD.gn`,
  `session_service_base.cc`, `session_restore.cc` or `browser_tabrestore.cc` patch in this PR. The files under
  `patches/chrome/browser/sessions/` are #59's own, not duplicates.
- **Own tab-menu copy dropped.** FIXED. There is no `koan_tab_menu.{h,cc}` copy, no `tab_menu_model` or `tab_strip_model` patch
  in the diff. `koan_tab_menu.cc:44-45` adds one table row for `kFolders`.
  The only upstream patches in the PR are `tab_group_view_layout.cc.patch` (edited) and `unpinned_tab_container_view_layout.cc.patch` (new).
- **`kFolders` (3300+).** FIXED. `kTabMoveToFolder = FirstTabMenuCommand(kFolders)`, with 3301 / 3302 and folders from 3310
  (`koan_folder_controller.cc:110-115`).
  The submenu comes from `context.AddSubmenu()` (`:850`). Execution returns true for the whole range (`:1665-1675`).
- **`IsPinnedForUser`.** FIXED. Used in `Prune` (`:1194`), `OnTabMoved` (`:1093`, `:1105`), `Normalize` (`:1255`),
  `FolderRows::Place` (`:1522`), `OnTabChangedAt` (`:1069`) and `AddTabs` (`:448`).
  Plain `IsPinned()` stays only where Essentials is meant: `AddTabs` `:446` and the "space for Essentials" choice at `:853`, `:907`.
- **Move into a folder.** FIXED. `AddTabs` calls `RemoveFromEssentials` or `UnpinFromSpace` (`:443-451`).
- **Order (pinned, folders, loose).** FIXED. `Normalize` starts after the Space's pinned run (`:1247-1257`).
  `FolderRows::Place` skips pinned tabs before adding rows (`:1522-1525`).
  E2E: `folders.mjs:317-340`.
- **`UnloadTabs`.** FIXED (`:717-719`).
- **Layout patches.** Both apply cleanly to pristine upstream.
- **Committed symlinks.** `git ls-tree -r HEAD | grep '^120000'` returns nothing. The `chromium` and `depot_tools`
  symlinks in the worktree are untracked and ignored (#63).

---

## 3. Fresh pass: new findings

I found no blockers.
Checked and fine:

- No TabStripModel mutation happens inside an observer callback. Every observer only edits data or posts `Changed()`, so
  upstream's `ReentrancyCheck` is not tripped.
- Drag: `IsTabStripEditable()` is false only while a drag controller exists (`base_tab_strip_region_view.cc:160-164`),
  so `OnTabMoved` really sees only drags. `Normalize` waits and retries.
- Pinned and Essentials tabs are ignored by the drag rule.
- Cross-window: a tab detached to another window leaves the source folder (`Prune`: not in the model). The destination does not
  know the token, so it is ignored.
- `MoveSplitTo`'s `CHECK(!destination_split)` (`tab_strip_model.cc:1168`) cannot trip, because `Normalize` always moves to
  unit boundaries.
- The nested-folder restore ordering and Space mapping hold (see S2).
- There are no folder drag sources. Folders cannot be dragged or dropped onto anything, so "onto itself or a descendant" cannot
  happen. This is documented in ZEN_PARITY.
- Mac: no Linux or Aura-only code outside the e2e scripts. `folders_drag` is X11-only by design (test bench). No bare
  `Ctrl` shortcut text was added.

### Should-fix

#### SF1. MPL-2.0 ports of Zen code carry only an MIT header, with no notice entry

- **Where.**
  - `src/browser/ui/views/koan_folder_views.cc:1-3` (MIT header). The folder icon path data is at `:111-147`, the relative time
    at `:150-173`, and the search UI strings at `:545`, `:564`.
  - `THIRD_PARTY_NOTICES.md`: no entry; it says only that "Zen" is referenced for the experience.
- **What is wrong.**
  - Verified verbatim copies from Zen's MPL-2.0 files:
    - Back and front folder SVG shapes (`koan_folder_views.cc:111-135`) are the `d=` of `ZenFolder.mjs:42-47`
      (`M8 5.625H11.9473...`, `rect x=5.625 y=9.625 ... rx=2.375`).
    - `FormatRelativeTime` is a line-by-line port of `ZenFolders.mjs:7-29`.
    - `"No tabs matching that search 🤔"` is `localization/zen-folders.ftl:29`.
  - The Zen sources carry the MPL header. A file that includes copied MPL code must keep MPL terms for that code, with
    attribution. An MIT-only header does not do that.
  - The same applies to the Zen reference screenshots (`docs/screenshots/zen-folders-*.png`), though those are docs.
- **Fix.**
  - Put the MPL-2.0 header ("This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0 ...") plus
    "Portions ported from Zen Browser (zen-browser/desktop): ZenFolder.mjs, ZenFolders.mjs, zen-folders.ftl" on
    `koan_folder_views.cc`, or move the ported pieces into one small MPL file.
  - Add a Zen / MPL-2.0 entry to `THIRD_PARTY_NOTICES.md`, with the source files.
  - Decide this for the whole repo. Develop already has the same gap elsewhere (for example `koan_color_mixer.cc` copies Zen's
    values). This PR is just the first one with verbatim code.

#### SF2. A folder's last tab loses its folder when it is nudged or the drag is cancelled

- **Where.** `OnTabMoved`, `koan_folder_controller.cc:1089-1141`, branch `:1126-1129`.
- **What is wrong.**
  - The rule runs on every intermediate move of a drag, and it changes membership immediately (`:1133-1139`). Nothing is
    undone when the drag is cancelled (Esc) or when the tab is moved back.
  - Take a folder A whose last tab T is followed by a loose tab L.
    - Dragging T just past L: its previous neighbour is L, which is loose, so T leaves A (`:1126`).
    - Moving T back to its original place (or Esc reverting the drag): previous neighbour is A's tab, next is L, which is
      loose. The first branch (`current != 0`) is false, `next_folder` is 0, and `prev` is in a folder, so no branch fires
      and `target == current == 0`. T stays loose, though it started in A.
  - Where another folder B follows, the same case works (target is A, `:1123-1125`). The failure happens only when loose tabs follow.
  - ZEN_PARITY documents "a drop right below the last folder's last tab stays loose" for loose tabs. It does not say that a
    folder's own last tab cannot come back.
- **Failure scenario.** Drag the last tab of the last folder down a little, then press Esc (or drag it back). It is now loose.
- **Fix.** Record the folder at drag start (when `IsTabStripEditable()` goes false) and either apply the rule only when the
  drag ends, or restore the saved membership if the tab is back at its original index. A smaller fix: treat "prev is in a folder,
  next is loose" as staying in the tab's own previous folder when it matches `prev`'s folder.

### Nits

- **N1. Stale design note.** `.agent/notes/decisions.md:107` still says "Persisted as window extra data (tab indices) through
  two session hooks." Since the migration it is tokens in the per-tab store plus a folder tree in the window store, with no session patch of its own.
- **N2. Re-attach rejoins by stale token, and can list a tab twice.** `OnTabsAdded` (`koan_folder_controller.cc:1143-1154`).
  - A tab dragged out of its window and dropped back in comes back as a new insert. Its old token puts it back in its old
    folder regardless of where it is dropped, and `Normalize` then moves it up into that folder.
  - It also pushes the handle without removing it from the folder it may still be in, because `Prune` is async. The tab can
    then be in two folders until the next explicit add or remove (`GetTabs` would list it twice, and the row's count would be off).
  - Fix: erase the handle from all folders before `push_back`, and skip the token join for non-restore inserts.
- **N3. No size cap on the window JSON.** `Rename` (`:495-503`) and `Serialize` (`:1406-1431`) have no limit on folder name length or count.
  - Session commands must be below 64 KB. `SessionCommand(id, pickle)` has `DCHECK(pickle.size() < kMaxContentSize)` at
    `components/sessions/core/session_command.cc:33`, and release builds truncate when serialising, which breaks the JSON so
    that all folders are lost on restore.
  - Not a realistic case, but cheap: trim names to about 200 characters in `Rename`.
- **N4. Missing e2e for the two migration fixes.**
  - No case drops a tab from the session to prove the S2 index-shift fix.
  - No case restores a folder with no tabs into a Space other than the first (the place mapping).
  - No case switches to horizontal tabs and back (S3).
  - Also, the submenu silently stops at 90 folders (`:861-863`).

## Merge recommendation

Merge after SF1 (a header and a notices entry; it is a licence-compliance point and cheap). All round-1 blockers and
should-fix items are resolved, and the #59 migration is complete with no duplicate patches or files. SF2 and N1 to N4 can go in
a follow-up, though SF2 is worth a short fix since it is a user-visible data loss on a normal gesture.
