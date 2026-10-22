# Review of PR #60: tab folders like Zen's (fixes #42)

Branch `feat/zen-folders` (worktree `~/.t3/worktrees/koan/feat-zen-folders`), `git diff origin/develop...HEAD`.
Nothing was built or run; everything below comes from reading the diff against upstream
(`~/work/koan/chromium/src`), Zen's source, and the in-flight PRs #57 and #59.
Items marked **(uncertain)** were not confirmed.

## What was checked and is fine

- **Hook patches.** All 7 patches apply to pristine upstream (`git show HEAD:<path>` into a scratch tree, then
  `git apply --check -p1`). They are small. The tab-menu hook is merged into #54's `tab_menu_model.cc.patch` and
  `tab_strip_model.cc.patch` (one call each, not duplicated).
- **Design claim.** Verified upstream: `TabGroupTabCollection` only accepts `SPLIT` children
  (`components/tabs/impl/tab_group_tab_collection.cc:20-22`), and `UnpinnedTabCollection` accepts `GROUP` and `SPLIT`. Groups
  cannot nest, so folders as Koan data over ordinary tabs is the only workable model.
- **Tab strip APIs.** `AddToExistingGroup` (tabs left of the group go to its start, and they take the group's
  pinnedness), `MoveWebContentsAt(index, to, select, group)`, `MoveSplitTo`, and the `TabStripModelObserver` signatures all match 158.
- **Layout manager.** Both containers (`TabGroupView`, `UnpinnedTabContainerView`) use
  `TabCollectionAnimatingLayoutManager`, so the `static_cast` at `koan_folder_controller.cc:1166` is valid today.
- **Collapse state.** `TabGroupView::is_collapsed()` is only flipped after the animation ends (`OnAnimationEnded`), exactly
  when upstream sets child visibility. So replacing `child->GetVisible()` with `!is_collapsed()` in the group patch is equivalent.
- **Colours.** Folder colours match `zen-folders.css` lines 15-17 (60% over grey / #c1c1c1, 30% over white / 60% over black, 50%
  over black / 15% over #ebebeb). All colours come from colour IDs, and radii from `koan_sidebar_metrics.h`.
- **Palette (#55).** `AddFolderCommands` registers through `KoanCommandRegistry::AddSource`.
- **macOS.** No shortcuts are added (Zen has none for folders), and there is no `Ctrl`/Linux-only code outside the e2e scripts.
  Not run on a Mac. **(uncertain)** Native Cocoa menus built from `ui::SimpleMenuModel` (element identifiers, the indented
  submenu entries, the bubble anchor of the "+" menu) have not been exercised.
- **Live folders.** ZEN_PARITY has a "Missing / out of scope (#42)" row (`docs/ZEN_PARITY.md:89`), as asked.
- **Merged work (#50/#53, #47).**
  - Clear (#50/#53): the change in `CloseUnpinnedTabs` keeps the rule that pinned tabs are never closed. In the ∞ view only
    ungrouped tabs are considered, and an ungrouped folder's tabs are exempt. No regression, but see S4 for the Clear row.
  - Freezing exemptions and restore on demand (#47): not touched. `UnloadAllTabs` uses the standard `DiscardTab`.

---

## Blockers

### B1. Delete Folder, Change Space and Convert to Space can silently delete a Space, or close the window

- **Where.**
  - `src/browser/ui/views/koan_folder_controller.cc:503-523` (`Delete`).
  - `:525-551` (`ChangeSpace`).
  - `:553-598` (`ConvertToSpace`).
- **What is wrong.**
  - A Space is a tab group, and a group goes away with its last tab. Koan knows this: `KoanShortcutController::CloseUnpinnedTabs`
    (`koan_shortcuts.cc`, "A workspace is a tab group, which goes away with its last tab; Zen's space survives being emptied")
    adds a fresh tab before it closes the last ones.
  - The three folder actions have no such guard:
    - `Delete` closes every tab of the folder in a loop.
    - `ChangeSpace` moves the folder's tabs out with `AddToExistingGroup`.
    - `ConvertToSpace` moves them out with `AddToNewGroup`.
  - Zen keeps the emptied Space in all three cases.
- **Failure scenarios.**
  1. Space "Research" holds one folder "Papers" with all its tabs. Right-click the folder, then Delete Folder: every tab
     closes, the group is destroyed, and the Space (name, emoji, colour) is gone with no tab to land on.
  2. Same Space, Change Space to "Work" or Convert to Space: the source group becomes empty and vanishes.
  3. In the ∞ view with no Spaces, select all tabs, then Move to Folder > New Folder, then Delete Folder: the last
     `CloseWebContentsAt` closes the window.
  - Ctrl+Shift+T does not bring the Space back, and a reopened tab is no longer in the folder.
- **Fix.**
  - `Delete`: if the folder's tabs (recursive) are all the tabs of its Space (or of the window, for an ungrouped folder),
    first `chrome::AddTabAt(..., group)` a fresh tab into that Space and keep it. Use the same `empties_workspace` logic as
    `CloseUnpinnedTabs`; factor it into a shared helper.
  - `ChangeSpace` and `ConvertToSpace`: if the source group would be left empty, add a new tab to it before moving, or
    refuse and toast through KoanToast (#54).
  - Add an e2e check for it (see S1).

---

## Should fix

### S1. The e2e test does not test several advertised features, and some checks are weak

- **Where.** `scripts/e2e/folders.mjs` (whole file), `scripts/e2e/palette.mjs:360-386`.
- **Not covered at all:**
  - **Drag in and out.** This is the headline of the issue, and USAGE and ZEN_PARITY document it ("Mostly"). The rule
    lives in `OnTabMoved` (`koan_folder_controller.cc:961-1006`), which has no test.
  - **Clear and Ctrl+Shift+K keeping folder tabs.** This is the #50/#53 interaction (`koan_shortcuts.cc:419-427`).
  - **Remove from Folder**, double-click rename, and the Delete-everything case from B1.
  - **Restore of anything but one ungrouped folder.** The restart test (the last section) only restores a single collapsed
    "Saved" folder in the ungrouped area (`space = -1`). It never exercises the Space-by-first-tab-index mapping
    (`Serialize` `:1227-1234`, `Restore` `:1276-1278`), subfolder parent indices (`:1286-1292`), several folders, or a
    folder inside a Space.
- **Weak assertions:**
  - "Unload All Tabs" only checks that the folder collapsed and kept its tabs. It never checks that a tab was discarded.
  - "Unpack" only checks that the row went away, not that the tab is still there and loose.
  - "Delete" counts page targets (`before - 1`), which is the only evidence the right tab closed.
- **Fix.** Add at least: a CDP-driven mouse drag (or a direct `TabStripModel` move through a test hook) into and out of a
  folder, a Clear test with a folder tab, a restart with a folder plus subfolder inside a Space, and B1's scenario.

### S2. Session restore identifies tabs by position; one missing tab shifts every later folder entry

- **Where.** `koan_folder_controller.cc:1211-1243` (`Serialize`), `:1245-1296` (`Restore`), patches
  `session_restore.cc.patch` and `session_service_base.cc.patch`.
- **What is wrong.** Folders store tab indices. Session restore builds `window.tabs` only from tabs that have navigations
  (`RestoreTabsToBrowser` loops over `window.tabs`), so a tab dropped by the session (see the scenario) shifts every later
  index. The PR documents this and calls the fix "more upstream hooks".
- **Failure scenario.** A background tab that never committed a navigation, or a blocked URL, is not in the session. After
  restart, folder "Work"'s entry `[4]` is the neighbouring tab, and a stranger's tab is in the folder, or the Space index
  (`space` = first tab of the group) points at the wrong group, so the folder lands in another Space or is dropped.
- **Fix.** #59 is adding per-tab session extra data (`koan::GetTabSessionData`, `kTabSessionDataKey`, in
  `session_service_base.cc.patch`) and so already owns the hook that makes this robust. Store a stable folder id per tab
  there (and keep only the folder tree, keyed by id, in the window data), instead of indices. Coordinate with #59 so there
  is one session hook, not two.

### S3. Folders stay active (and reorder tabs) when the sidebar is not vertical

- **Where.** `koan_folder_controller.cc:204-206` (`IsEnabled`). The header comment at `koan_folder_controller.h:115-116`
  says "feature on, normal window with vertical tabs".
- **What is wrong.** The code checks only the flag and `GetIsNormalType()`. Vertical tabs are a user toggle (the REBASE
  "Sidebar rows lookup" row notes the vertical/horizontal switch). `SyncRows` bails out unless the region is
  `VerticalTabStripRegionView` (`:1122-1125`) and the layout hooks only patch the vertical layouts.
- **Failure scenario.** Switch to horizontal tabs, then "Move to Folder > New Folder" (still offered by
  `AddTabMenuItems`) or the palette's New Folder. `Normalize` pulls the folder's tabs to the front of the Space and
  `CloseUnpinnedTabs` silently exempts them, but no row or collapse state is drawn. The tab order changes with no visible
  explanation.
- **Fix.** Check `tabs::VerticalTabStripStateController` / the strip orientation in `IsEnabled()` (or at least in
  `AddTabMenuItems`, `AppendFolderCommands` and `ShowCreateMenu`) and stop normalising when horizontal.

### S4. Clear row ignores folders: button shown for nothing, wrong arrow state

- **Where.** `src/browser/ui/views/koan_sidebar_tabs.cc:561-585` (`UpdateClearRow`) versus `koan_shortcuts.cc:419-427`.
- **What is wrong.** The PR filters folder tabs out inside `CloseUnpinnedTabs` only. `UpdateClearRow` still counts them
  through `UnpinnedTabsForClear`/`IsClosableByClear`, so the button is visible and its icon state is computed as if they
  would be closed.
- **Failure scenario.** A Space whose only unpinned tabs are in folders: the Clear button shows, a click does nothing (it
  returns early with no toast), and with a mix of folder and loose tabs the arrow/icon state (`closable == unpinned.size()`)
  is computed over the wrong set.
- **Fix.** Put the folder exemption in `UnpinnedTabsForClear` (static, so it needs the controller or a predicate), so the
  button, the count and the action all agree. This also keeps #59's `IsPinnedForUser` change a single edit.

### S5. Conflicts with the in-flight PRs #57 (Spaces) and #59 (pinned tabs)

Nothing here is wrong in #60 alone, but the three PRs cannot all merge as they are.

- **`koan_tab_menu.h` / `koan_tab_menu.cc`.**
  - #57 adds new files at the same paths (add/add conflict) with a different API:
    `AddZenTabMenuItems(...)` returning a `unique_ptr<SimpleMenuModel>`, and `ReplaceTabGroupMenuItems` for "Move To".
  - #60 has `AddKoanTabMenuItems` plus the `IsKoanTabMenuCommand*` / `ExecuteKoanTabMenuCommand` dispatcher.
- **#59 reuses #60's function names in a different header** (`koan_tab_menu_hooks.h`).
  - `AddKoanTabMenuItems(ui::SimpleMenuModel*, TabStripModel*, int, int)` has an identical signature to #60's, so both
    would be defined and fail to link.
  - `IsKoanTabMenuCommand(int)` is identical too.
  - The other two are overloads and compile, but the dispatch is duplicated in `tab_strip_model.cc.patch`.
  - Both also edit `tab_menu_model.cc.patch` and `tab_strip_model.cc.patch` at the same spots, but with different hook
    placement: #59 calls its hook at the end of `Build()`, #60 after the split items.
- **#57's "Move To" replaces the "Add to group" items.** #60 inserts "Move to Folder" before
  `CommandAddToExistingGroup` and falls back to appending (`koan_folder_controller.cc:786-794`). The position changes
  silently once #57 lands.
- **`sessions/BUILD.gn.patch` and `session_service_base.cc.patch`** are new files in both #60 and #59, so an add/add
  conflict. The BUILD.gn dep differs only by `:views`. Patches are one file per upstream file, so they must become one
  patch with two hunks.
- **`tab_group_view_layout.cc.patch`.** #57 rewrites the `unfocus_header_` hunk and the header height (`koan_space`),
  in the same hunks #60 changes.
- **The "+" button.** #57 replaces the workspace switcher with a bottom space bar and adds its own Create menu with
  Create Folder disabled. #60 rewires the switcher's "+" (`koan_workspaces.cc:259-266`, `:436-445`) and builds its own
  Create menu (`ShowCreateMenu`). One of them must call the other's menu, otherwise there will be two Create New menus.
- **Ordering.**
  - #59 keeps workspace-pinned tabs at the start of the Space's group (`MoveToPinnedSection` puts them at
    `group->ListTabs().start()`; `OnTabsAdded` moves new tabs behind the pinned run).
  - #60's `Normalize` (`:1030-1089`) puts folder tabs at that same spot. Folder normalisation pushes pinned loose tabs
    below the folders (and the next pin/add moves them back), so the "pinned tabs first" invariant fights the folder
    order. Zen interleaves folders and pinned tabs in one pinned section. Decide an order (for example folders, then pinned,
    then loose) and make one owner.
- **`IsPinnedForUser`.** #59's diff does not contain the helper yet. #60 uses `tab->IsPinned()` in `Prune` (`:1014`),
  `AddTabs` (`:395-405`), `OnTabMoved` (`:965`), the tab menu (`:758`) and `kTabNewFolder` (`:878`). After #59 that means
  Essentials only, which is what folders want, but `UnpinnedTabsForClear`/`CloseUnpinnedTabs` (S4) will conflict textually.
  #59 mentions "Unload all tabs of a folder" can reuse `KoanPinnedTabController::UnloadTabs`; do that once both land.
- **Suggested resolution.** Land #60's `koan_tab_menu.h` as the single dispatcher (it already handles IDs per feature) and
  have #57 and #59 register items through it instead of adding parallel hooks.

### S6. Possible use-after-free: rename field blur during row destruction (uncertain)

- **Where.** `koan_folder_views.cc:208-211` (blur callback bound with `base::Unretained(this)`), `:183-188`
  (`KoanFolderNameField::OnBlur`), `:262-276` (`FinishRename`).
- **What is wrong.** The PR's own review round fixed "a use-after-free from a focused rename field" by making
  `controller_` a WeakPtr, but `FinishRename` itself still runs from `OnBlur`. If the focused field is cleared while the
  row is being destroyed (row removed by `AnimateAndDestroyChildView`, or window teardown), `FinishRename` runs from
  `~View` after `KoanFolderRowView`'s own members are gone: it reads `name_field_` and copies `controller_`.
- **Failure scenario.** Start renaming, then close the window (or delete the folder from the palette-triggered path) while
  the field has focus.
- **Fix.**
  - In `~KoanFolderRowView`, detach the callback: clear the field's controller and blur handler, and drop its focus
    first (`GetFocusManager()->ClearFocus()` when `name_field_->HasFocus()`).
  - Or bind the blur callback through a `WeakPtrFactory` member declared last, not `Unretained`.
  - Verify with a window close in the e2e test while renaming.

### S7. The issue's "done when" asks for a comparison with Zen screenshots; none is included

- **Where.** PR body ("There's no Zen screenshot here"), `docs/screenshots/folders-*.png`.
- **What is wrong.** The issue says "compare screenshots with Zen". The PR argues from CSS only. Fine for the colours
  (verified above), but the row layout, icon animation timing and popup placement are unverified against Zen.
- **Fix.** Attach side-by-side screenshots (the three states in the PR), or state in ZEN_PARITY which visuals are
  unverified.

---

## Nits

- **N1. REBASE.md misses assumptions the hooks make.** `docs/REBASE.md:62-63`.
  - It does not record that the container's layout manager is `TabCollectionAnimatingLayoutManager` (cast at
    `koan_folder_controller.cc:1166`).
  - It does not record that the group patch replaces `child->GetVisible()` with `!tab_group_view->is_collapsed()` and
    moves `bounds.set_y` after `Place()`. That is equivalent today, but it is a semantic edit.
  - It does not record that `Place` reads `TabView::GetTabInterface()` / `SplitTabView::collection_node()`.
  - The Folder session data row does not mention `sessions/BUILD.gn` (other rows write "+ `x/BUILD.gn`").
- **N2. Patch headers were stripped.** The `diff --git` / `index` lines and the hunk-header function context were removed from
  `tab_menu_model.cc.patch`, `tab_strip_model.cc.patch` and `tab_group_view_layout.cc.patch`. `apply_patches.sh export`
  will re-add them on the next export (churn).
- **N3. Drag edge case.** `OnTabMoved` (`:985-994`): a tab dropped at the end of folder A when folder B follows becomes B's
  (the neighbour after it is B's tab), and with no following folder it becomes loose. A folder's tail is not a drop target.
  The parity row says "Mostly" but does not mention this. Zen's `zen.tabs.folder-dragover-threshold-percent` (20%) drop
  onto the row is not possible.
- **N4. Folder name colour.** `koan_folder_views.cc:377` uses `kColorToolbarText`, while sidebar rows use the tab/Koan
  sidebar colours (`kColorKoanSidebarNewTabForeground` etc.). On a tinted sidebar the toolbar text may not match
  **(uncertain)**. Pressed state reuses the hovered colour (`:333-342`), unlike the New Tab row's
  `kColorKoanSidebarRowPressed`.
- **N5. Folder colours in the ∞ view.** The colour IDs are one window-wide accent (`koan_color_mixer.cc`
  `kColorSysPrimary`), so every Space's folders in the ∞ view look the same. The comment at `:948` ("The rows take their
  Space's colour") and the docs ("in the Space's colour") only hold with one Space focused.
- **N6. Search matching.** `koan_folder_views.cc:437-439` filters on `GetLastCommittedURL().host()`. For discarded or
  lazily restored tabs (#47) that may be empty; `GetVisibleURL()` is safer **(uncertain)**. The "time ago" uses
  `GetLastActiveTime()`, which for a restored tab is not Zen's `lastAccessed`.
- **N7. Search popup guard.** Zen skips the hover popup while the URL bar is focused or a rename is active
  (`openTabsPopup`); Koan only checks the row's own rename (`:318`).
- **N8. Zen behaviours not in the parity table.**
  - Adding a tab to a collapsed folder re-expands it unless it has the active tab (`on_TabGrouped`: `group.collapsed =
    group.hasAttribute("has-active")`). In Koan "Move to Folder" into a collapsed folder makes the tab vanish, with no
    feedback.
  - Zen's "New Folder" in the sidebar toolbar context menu (empty area) is missing.
  - Zen's `zen.folders.max-subfolders` and `search.hover-delay` are hard-coded in Koan (no settings row; fine, but not
    noted).
- **N9. Docs.**
  - ZEN_PARITY `:73` says "Clear (Ctrl+Shift+K)" without the ⌘ form, unlike USAGE's "Ctrl+Alt+V (⌘⌥V)" style.
  - The "+" tooltip lost its "(Ctrl+Alt+N)" hint (`koan_workspaces.cc:259-262`). The shortcut still exists.
  - Delete Folder says Ctrl+Shift+T reopens the tabs; they come back as loose tabs of the Space, not in the folder.
- **N10. `Restore()` is not idempotent** (`koan_folder_controller.cc:1245`). It appends to `folders_` without clearing. It
  is called once per restored window today, but a second call for the same browser would duplicate every folder. A
  `folders_.empty()` guard (or reset) is cheap.
- **N11. Unload picks the next tab by strip order** (`:605-619`). That is the first same-Space tab outside the folder,
  which is likely another folder's tab at the top of the Space, possibly hidden in a collapsed folder (the active tab
  then shows under that folder). Prefer the most recently used or the first loose tab.
- **N12. Accessibility.**
  - The row is a `views::Button`, so it is only `ACCESSIBLE_ONLY` focusable by default **(uncertain)**; check Tab
    reachability on Linux.
  - The "has active tab" dots are not part of the accessible name.
  - Tabs inside a folder carry no level information.
  - The rename field and search items are named correctly.
