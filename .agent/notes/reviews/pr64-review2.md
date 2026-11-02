# PR #64 review, round 2: Library panel (history, downloads, spaces), Fixes #43

Head `10033da` on `feat/zen-library` (5 commits over develop `f2d50aa`; `addeb09` plus a clang-format include-order commit). Read-only review: nothing built or run, and `~/work/koan/chromium` was not read.
Paths are relative to `src/browser/ui/views/` unless they start with `src/`, `scripts/` or `docs/`. Line numbers are for the head.
"Verified" = I read the code. Chromium behaviour is taken from the round-1 review (which checked the Chromium 158 source) and from the author's own code comments; I did not re-read Chromium.

## Verdict

Request changes. The round-1 list is largely fixed well (see the table). One round-1 blocker is only partly closed: **B1** still has a path where a download that turns dangerous opens by itself. One new should-fix: the macOS default keys for History and Downloads are probably shadowed by Chrome's menu. New: 0 blockers, 1 should-fix, 7 nits. No committed symlinks. Licence headers are right apart from one attribution gap.

---

## 1. Round-1 status

| Item | Status | Evidence |
|---|---|---|
| **B1** dangerous download arms auto-open | **PARTIAL** | Click and display fixed. A pre-verdict arm is not cleared once the Library's widget is freed. See Blocker B1 below. |
| S1 `AllowDeletingBrowserHistory` | FIXED | One helper, `koan_library_view.cc:277`. Gates every delete path: `koan_library_history.cc:209` (clear menu), `:323` (`MayForget`, used by the row button `:571`, `ForgetEntry` `:669`, row menu `:767`), `:701` and `:725` (Forget Site, checked again in `DoForgetSite`), `:809` (`ClearHistory`). Downloads: `koan_library_downloads.cc:466` (Clear all), `:950` (`RemoveItem`), `:1025` (`ClearAll`), `:1091` (menu). Closed tabs are still forgettable, which is right. Test: `library_history.mjs:309-315`. |
| S2 Retry from the active tab | FIXED | `koan_library_downloads.cc:956-990`: `CanResume()` then `Resume`; otherwise plain `DownloadUrlParameters`, `set_content_initiated(false)`, `DownloadSource::RETRY`, on the profile's manager. No tab involved. |
| S3 pagination O(N²) | FIXED | History pages by time: `Query(oldest_, 100, 100, append)` (`koan_library_history.cc:368-385`), rows appended into existing groups (`:510-526`, `GroupFor`), pool paged without re-querying for Most Visited. Downloads appends (`koan_library_downloads.cc:501-520`). Small stall case: nit N-a. |
| S4 RTL | FIXED | `koan_library_controller.cc:451-453`: `on_screen_right_ = IsSidebarOnRight != IsRTL`, same rule as `koan_toast.cc:215-219`. Used for bounds, slide direction (`SetProgress`), arrow and gradient (`koan_library_view.cc:971-985`, `:1044-1047` use mirrored bounds); the view's own layout stays logical. `tabs_right_rtl.mjs` checks it. |
| S5 confirmation names the range | FIXED | `koan_library_history.cc:97-104`, `:797-800` (title from the range). |
| S6 confirmation outlives the section | FIXED | `ShowLibraryConfirmation` (`koan_library_view.cc:258-275`) holds `library->KeepAlive()` until the dialog is destroyed (`koan_library_controller.cc:400-420`: the 30 s timer is not started while the count is above 0). The runner is owned by the dialog model, so it is released even if the callback never runs. Delete Space is #57's own box and does not depend on a section. |
| S7 e2e coverage | PARTIAL | New `library_history.mjs` reads real history (Forget, Clear last hour keeps the older visit, private window, policy). Good. Still missing, as the author says: Forget Site, Downloads Cancel/Retry/Open/Remove, Ctrl+click, more than one page. The B1 test does not exercise the path that is still open (B1 below). MoveSpace's forward branch is untested (N-g). |
| S8 keyboard and a11y | FIXED | Focused row shows its actions (`koan_library_view.cc:526`); buttons are out of the tab order (`:458`) and everything is in the row menu, now with Cancel / Retry Download / Review warning (`koan_library_downloads.cc:1065-1100`); status as accessible description (`:447`); tab role and selected state (`:168-177`); pane role and name (`:858-862`). New small gap: N-e. |
| S9 Spaces layering | FIXED | The section uses `GetSpaces`, `AddSpacesChangedCallback`, `RenameSpace`, `ShowIconPicker`, `EditSpaceTheme`, `UnloadSpace`, `DeleteSpace`, `SwitchTo`, `ShowCreateForm` (`koan_library_spaces.cc`, `ShowSpaceMenu` to `OpenTab`). It still reads the tab strip to list tabs and calls `ActivateTabAt` in `OpenTab`; fine. Deleting the last Space is refused (`CanDeleteSpace`, `koan_workspaces.cc:735`). |
| S10 hidden sections rebuild | FIXED | `GetVisible()` in the guard: `koan_library_spaces.cc:569`, `koan_library_downloads.cc:663-665`; History already did it (`koan_library_history.cc:331`). |
| N1 Today | FIXED | `GetWhenCutoff()` uses `LocalMidnight()` (`koan_library_view.cc:745-749`). |
| N2 closed tabs stale rows | FIXED | `koan_library_history.cc:444` clears and shows "Loading closed tabs…". |
| N3 REBASE rows | FIXED | `docs/REBASE.md:117-122`. |
| N4 parity doc | FIXED | `docs/ZEN_PARITY.md:174,179`. |
| N5 previews | FIXED | Size checked before reading (`koan_library_downloads.cc:319-326`), keyed by path and end time, pending set not invalidated on rebuild. |
| N6 Koan colour IDs for cards | FIXED | `TintColorId` → `kColorKoanLibrarySpaceTint*`, mapped in `koan_color_mixer.cc`. |
| N7 swatch at 1x | MOOT | #57's theme menu replaced it. |
| N8 stale row actions | FIXED | `Actions` struct compared in `OnDownloadUpdated` (`koan_library_downloads.cc:547`). |
| N9 Delete File unconfirmed | FIXED | `ConfirmDelete` (`:1007`). |
| N10 reduced motion | FIXED | `koan_library_controller.cc:268,374`. |
| N11 tooltip after rebind | FIXED | `RefreshTooltip` on hover and focus (`koan_library_controller.cc:126-141`). |
| N12 macOS key routing | PARTIAL | Only a `TODO(#51)` (`src/browser/koan_shortcut_table.cc:176-181`). The risk is real; see Should-fix F1. |
| N13 harness | PARTIAL | `{profile}` substitution fixed (`scripts/e2e/run.sh:73-81`). The wrapped comment is not: `run.sh:25` is 97 columns (N-f). |
| N14 palette / settings rows | FIXED | `Toggle Library` has `is_available = IsLibraryEnabled` (`koan_shortcut_table.cc:98`) and the key passes through when unavailable (`koan_shortcuts.cc:250-256`). "Show All History" is intentionally always on and falls back to Chrome's History. |

---

## 2. Blocker (1)

### B1 (remaining). A download that is armed for "open when done" before it turns dangerous still opens on Keep once the Library is gone
- `koan_library_downloads.cc:900-907` (`OnRowPressed` arms), `:532-538` (`OnDownloadUpdated` disarms), `:444` (the observer is created with the section); `koan_library_controller.cc:64,360-368,414-420` (the widget, and the section with it, is destroyed 30 s after closing).
- **What is fixed.**
  - A click on a row that is already blocked never arms: `OnRowPressed` returns early through `ShowWarning` (`:901-904`).
  - `UpdateRow` tests `IsBlocked` before "Opens when finished" (`:775`).
  - The menu "Open" is enabled only for `COMPLETE` items (`:1078-1080`, `HasFile`), so it cannot open a held-back one.
  - Keyboard (Enter/Space on the row, Cmd/Ctrl+Enter) goes through the same `OnRowPressed`. There is no other call to `OpenDownload` or `SetOpenWhenComplete` outside this file (grep over `src/` and `scripts/`).
- **What is still open.** The flag is cleared only inside `OnDownloadUpdated`, and that exists only while the section's `AllDownloadItemNotifier` (`:444`) does. The author's own comment at `:533-535` says the flag "may have been asked for before the verdict".
- **Failure scenario.**
  1. A large download is in progress and not yet flagged. The user clicks its row in the Library. It now says "Opens when finished" and `open_when_complete_` is set (this is the round-1 mechanism: `download_item_impl.cc:1334`, honoured at `:2162`).
  2. The user closes the Library (or the window). Thirty seconds later `DestroyWidget()` frees the sections and the notifier. No KeepAlive is held for this.
  3. The data finishes, Safe Browsing marks the file dangerous and the download waits in `IN_PROGRESS`. Nothing clears the flag, because the observer is gone.
  4. The user chooses Keep in Chrome's own bubble or `chrome://downloads`. The file opens at once, with no further prompt. This is exactly the round-1 outcome.
- Also reachable: arm, then close the browser window, or arm in one window and let that window's Library be freed.
- **Test gap.** `library.mjs:334-378` only clicks a row that is already blocked and checks the text. It never arms a safe-looking download, never lets the verdict arrive later, never closes the Library, and never presses Keep.
- **Fix (any one of).**
  - Do not arm at all: make the row click on an in-progress download do nothing, or open Chrome's details. Cheapest and safest.
  - Or remember which GUIDs the Library armed, and disarm them all when the section is destroyed (`~KoanLibraryDownloadsSection`) and when the browser view goes away.
  - Or move the clearing observer to something that outlives the widget (controller- or profile-lifetime), and keep the KeepAlive while any armed download is in progress.
  - Add an e2e: arm a slow download, close the Library for more than 30 s (or call `DestroyWidget` through a test hook), flip the verdict, Keep, and assert nothing opened.
- Verified by reading. The Chromium side is the round-1 finding.

---

## 3. Should fix (1)

### F1. The macOS defaults for History and Downloads (Cmd+Y, Cmd+Shift+H, Cmd+Shift+J) are registered at normal priority, so Chrome's menu probably gets them first
- `src/browser/koan_shortcut_table.cc:181-184,198-199`; `koan_shortcuts.cc:94-107` (priority logic); `docs/REBASE.md` row "Space shortcuts first on macOS".
- **What is wrong.**
  - Koan's own code says that on macOS Chrome runs its menu shortcuts before normal-priority accelerators (`koan_shortcuts.cc:95-98`). That is why only the two Space keys are `kHighPriority` (`:100-103`).
  - Cmd+Y (History), Cmd+Shift+J (Downloads) and Cmd+Shift+H (Home) are Chrome menu key equivalents. The new bindings are normal priority.
  - macOS is the only shipping platform.
- **Failure scenario.** On a Mac, Cmd+Y opens `chrome://history`, Cmd+Shift+J opens Chrome's Downloads and Cmd+Shift+H goes Home. The Library's History and Downloads keys, issue #43's headline shortcuts, do nothing for the Library. The author's `TODO(#51)` lists the same worry and defers it.
- **Fix.** On macOS register `kLibraryHistory` and `kDownloads` at `kHighPriority`. `Run()` already returns `false` when the Library is unavailable (`koan_shortcuts.cc:250-256`) for Toggle Library; do the same for the other two when they would only repeat Chrome's own command, so the key passes through in a private window. Then hand-test the three keys from a page, the Library's search field and the omnibox.
- Verified by reading the code and the comment. Not run on a Mac. The menu equivalents themselves come from round-1 N12.

---

## 4. Fresh review of the new code

### Spaces on #57's API
- **Cards, observers, lifetimes.** Verified fine.
  - `spaces_changed_` is a member `CallbackListSubscription`, so the `Unretained(this)` callback is removed first on destruction. The tab-strip observer is added and removed (`koan_library_spaces.cc` ctor and dtor).
  - A rebuild is blocked during a drag and a rename, and coalesced to 150 ms and visible-only.
  - The section dies with the widget, which dies in `OnBrowserViewDestroying`, called from `DestroyFor` before the controller is gone.
- **`MoveSpace()` (`koan_workspaces.cc:778-806`).** Arithmetic verified against the documented `MoveGroupTo` rule (index of the group's first tab, counted without the group's own tabs). Examples with A[0,2) B[2,5) C[5,6):
  - A to 1 gives `others[1]`=C start 5, minus 2 = 3, so order B A C.
  - C to 0 gives 0.
  - A to the end gives C end 6 minus 2 = 4.
  - Bounds: `to < 0`, an unknown Space, or no other Space returns early. `to` above the count falls into the "end" branch. Moving the last Space to the end gives `index == moved.start`, a no-op.
  - Active Space: `MoveGroupTo` does not change focus, and the Library's `EndDrag` re-checks `GetSpace` inside `MoveSpace`. The `to` the Library passes (`Drag`/`EndDrag`, `koan_library_spaces.cc:466-535`) is the final position among the Spaces, which matches.
  - Persistence: the order of the Spaces is the order of their tabs (`GetSpaces()` sorts by `ListTabs().start()`, `koan_workspaces.cc:459-480`), so the normal session save and restore keep it. There is no separate order pref to forget. `OnTabStripModelChanged` schedules the bar update.
  - Only a test gap: N-g.
- **Delete Space.** The Library only calls `DeleteSpace` (`koan_library_spaces.cc` `Delete`), and #57's code asks first, refuses to delete the last Space, cycles away from the active one and marks the Space as `deleting_` so it is not recreated (`koan_workspaces.cc:739-776`, `:1043-1056`). The "closes the window" worry from round 1 is gone. The menu item is disabled when `CanDeleteSpace()` is false.
- **Bottom bar.** `SetLeadingButton` / `Update` / `Layout` / `CalculatePreferredSize` all count the leading button consistently (`koan_space_bar.cc:199-308`). Private windows get `nullptr`, which clears it.

### Downloads, other than B1
- Row actions re-resolve by GUID (`RunOn`, `:602-609`), so a stale row cannot act on another item.
- `ShowWarning` closes the Library and goes to the bubble's security page, else `chrome://downloads` (`:931-947`). Right.
- `ClearAll` iterates a copy of the pointers and removes only finished items (`:1024-1034`).

### Mac shortcuts and guards
- `Toggle Library` passes through when unavailable (`koan_shortcuts.cc:250-256`). `Show All History` and `Downloads` fall back to Chrome's commands (`:257-263,273-279`). Private windows are excluded by `IsAvailable()`.
- Background open uses `EF_PLATFORM_ACCELERATOR` (Cmd on macOS) for History rows (`koan_library_history.cc:618`). No Linux-only code outside `BUILDFLAG` (`koan_library_downloads.cc:115`).
- The key priority problem is F1.

---

## 5. Nits (7)

- **N-a. History "load more" can stall on a page of already-listed rows.** `koan_library_history.cc:510-526` skips rows already in `shown_keys_`; `OnScrolled` (`koan_library_view.cc:840-845`) fires only when the scroll position changes. By Site / search / Last visited use `REMOVE_ALL_DUPLICATES` (`:393`), so a page made only of older visits to URLs already shown adds no rows, the list does not change, and the user has to scroll away and back. Fix: after `Append`, if nothing was added and not exhausted, call `FetchMore()` again.
- **N-b. A rename can get stuck.** `DoStartRename` sets `renaming_ = true` even if the Space has gone and no field was made (`koan_library_spaces.cc:740-748`). Then `Rebuild` returns early for good and `HandleEscape` (`:453-460`) swallows Esc, so the Library cannot be closed with Esc. Set `renaming_` only if `rename_field_` exists. Needs the Space deleted within one task turn of the click, so very unlikely.
- **N-c. Licence attribution gap.** `src/browser/ui/color/koan_color_mixer.cc:135-170` adds the Library's colours "(zen-library.css)" with Zen's colour-mix percentages, but the MPL header (`:5-8`) and the `THIRD_PARTY_NOTICES.md` row for this file list only "theme stylesheets" and `zen-workspaces.css`. Add `zen-library.css (the Library's colour values)` to both. (The file already has the MPL header, so licence status is right; only the list is incomplete.)
- **N-d. Unreferenced binaries, two of them showing Zen's UI.** `docs/screenshots/{library-*,zen-library-*}.png` (6 files, about 330 KB) are linked from no document. `THIRD_PARTY_NOTICES.md:47` covers only `*-zen-vs-koan*.png`, so `zen-library-history.png` and `zen-library-spaces.png` fall outside the "shown for comparison only" note. Reference them from `ZEN_PARITY.md` or drop them, and widen the glob.
- **N-e. "Drag to reorder" is a dead tab stop.** `MoveHandle` is a focusable `ImageButton` with an empty callback (`koan_library_spaces.cc:301-316,716`; platform default focus). A keyboard user lands on it and Enter does nothing; there is no keyboard way to reorder Spaces. Make it not focusable, or add Alt+Arrow handling calling `MoveSpace`.
- **N-f. Comment line over 80 columns.** `scripts/e2e/run.sh:25` is 97 columns (the author said it was wrapped). Line 70 is older.
- **N-g. `MoveSpace`'s forward branch is untested.** `library.mjs:432-445` swaps two Spaces, which only reaches the "to the end" branch; `to < others.size()` for a forward move needs three Spaces.

Process note: the PR has five commits, one of them a clang-format fix-up (`10033da`); CONTRIBUTING asks for fixes to be folded in. It will be squashed on merge, so no action needed.

---

## 6. Licence headers and symlinks

- **Symlinks:** `git ls-tree -r HEAD | grep '^120000'` returns nothing.
- **MPL rule (CONTRIBUTING.md:192-216):** every new C++ file that ports Zen material has the MPL header with the ported files listed: `koan_library.h`, `koan_library_controller.cc`, `koan_library_view.{h,cc}`, `koan_library_ui.h`, `koan_library_history.cc`, `koan_library_downloads.cc`, `koan_library_spaces.cc`, `koan_library_icons.cc`. All are in the `THIRD_PARTY_NOTICES.md` table (lines 36-42), and the header lists match the table.
- **MIT files:** `koan_library_ui.cc`, `koan_library_icons.h`, `koan_library_sections.h`, `library.icon`, `move.icon`. I read `koan_library_ui.cc` (generic state background and button helpers) and both icons (Koan's own simple paths, not Zen's sprites): nothing ported. Fine.
- **One gap:** N-c above. `koan_color_id.h` only adds names, so MIT is right.
- The e2e `.mjs` files carry no header, as the existing e2e files do.

---

## 7. Not verified (needs the Chromium source, which I did not read)

- Whether `IsDangerous() || IsInsecure()` covers every warning Chrome shows. The ones I would check are the scanning and sensitive-content states (async scanning, prompt-for-scanning, sensitive-content warning): if Chrome treats them as warnings but `IsBlocked` does not, a click on such a row arms open-when-done and the Open menu would be enabled for a completed item in such a state. Worth checking against `DownloadItem::CanOpenDownload`-style helpers and using them instead of the two flags.
- Whether the back arrow in RTL ends up pointing the right way: `kArrowBackIcon`/`kArrowForwardIcon` may flip in RTL (`koan_library_view.cc:979-985` picks by physical side only). The e2e checks only layout.
- Real-Mac behaviour of every shortcut (F1).
