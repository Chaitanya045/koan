# PR #64 review: Library panel (history, downloads, spaces), Fixes #43

Branch `feat-zen-library` at a360d61, rebased on develop 110c80d (develop has not moved). Read-only review: nothing built or run.
Paths are relative to `src/browser/ui/views/` unless they start with `scripts/`, `docs/` or `src/`.
"Verified" means I read the code and checked the Chromium 158 source or Zen source. "Uncertain" means a behaviour I could not confirm without running it.

Checked and found correct (so nobody re-checks them):
- **Clear-history range.** `koan_library_history.cc:719-731` computes the range correctly.
  - Last hour, 12 hours, week and month are `now - N h`. Today is `now.LocalMidnight()`. Everything is `Time()`. The end is `Time::Max()`.
  - It goes through `BrowsingDataRemover::Remove(DATA_TYPE_HISTORY)`, the same path as Chrome's Clear Browsing Data. That path also expires the TabRestore entries (`BrowsingDataHistoryObserverService`) and leaves download history alone.
  - The menu items are disabled by `kAllowDeletingBrowserHistory`.
- **Incognito.** `IsAvailable()` (`koan_library_controller.cc:141`) requires a normal window and a non-OTR profile.
  - Every entry point goes through it: shortcuts, palette, button.
  - The History and Downloads sections bind to the original profile's `HistoryService` and `DownloadManager`. An OTR profile has its own DownloadManager, so nothing leaks either way.
  - The shortcuts fall back to Chrome's pages.
- **Download actions.** They are keyed by GUID and re-resolved with `GetDownloadByGuid` at the moment they run (`koan_library_downloads.cc:~540`). A stale row cannot act on the wrong item. Rows are rebuilt from the manager on open and on state changes.
- **Observer and tracker lifetimes.**
  - `ScopedObservation` and `AllDownloadItemNotifier` are cleaned up. The `OnManagerGoingDown` reset is safe (the notifier uses a WeakPtr guard).
  - `CancelableTaskTracker` members are cancelled in `Fetch` and on destruction.
  - `TabStripModel::RemoveObserver` on a never-added observer is a no-op (`base/observer_list.h:388`).
- **Idle cost (#47).** No repeating timers and nothing while closed. The widget is freed after 30 s. Download and history events return early when the Library is closed.
- **Patches and macOS guards.** No upstream patches. `BUILDFLAG(IS_MAC)` guards cover the shortcut table and the Finder wording. Nothing Linux-only sits outside guards.
- **Shortcut keys.** They match Firefox/Zen: Ctrl+H and Ctrl+Shift+H, ⌘⇧H and ⌘Y; Ctrl+Shift+Y and ⌘⇧J; Alt+Shift+L (`zen-src/content/zen-sets.js:21-25`, `ZenKeyboardShortcuts.mjs:1325-1335`).
- **Forget About This Site, history part.** `hostname_suffix` uses `DomainIs` matching because `kBrowsingHistoryImprovedHostnameSuffixMatching` is enabled by default in 158. It will not match `badexample.com`.

---

## Blockers (1)

### B1. Clicking an in-progress *dangerous* download hides the warning and arms auto-open
- `koan_library_downloads.cc:836-841` (`OnRowPressed`), `:715-721` (`UpdateRow` status), `:235` (`IsBlocked`).
- **What is wrong.**
  - A click on any `IN_PROGRESS` row calls `SetOpenWhenComplete(!...)`. That includes a download that is blocked as dangerous (`IsBlocked`: dangerous and not done).
  - `UpdateRow` then checks `GetOpenWhenComplete()` before `StatusText`, so the row's text changes from "This file may be dangerous" to "Opens when finished".
  - Chromium never resets `open_when_complete_` on validation. `DownloadItemImpl` sets it at `download_item_impl.cc:1334` and honours it at completion at `:2162`.
- **Failure scenario.**
  1. A download is flagged dangerous and waits in the in-progress state.
  2. The user clicks the row in the Library; the warning disappears.
  3. The user later chooses Keep in Chrome's download bubble. The file opens immediately, with no further prompt.
  - Chrome's own UI never offers open-when-complete on a warning.
- **Fix.**
  - In `OnRowPressed`, return early when `IsBlocked(item)`.
  - In `UpdateRow`, test `IsBlocked` before the "Opens when finished" branch.
  - Consider clearing the flag if the item is dangerous.
- Verified.

---

## Should fix (10)

### S1. Row-level deletes ignore `AllowDeletingBrowserHistory`
- `koan_library_history.cc:592-614` (`ForgetEntry`: `DeleteURLs`), `:639-657` (`DoForgetSite`: `DeleteURLs`).
- `koan_library_downloads.cc:865` (`RemoveItem`: `item->Remove()`), `:931-937` (`ClearAll`).
- **What is wrong.**
  - Only the "Clear history" submenu checks the pref (`FillTabMenu`, line 194).
  - Chrome guards the same operations: the history UI via `profile_based_browsing_history_driver.cc:43`, and chrome://downloads Remove and ClearAll via `downloads_dom_handler.cc:491,543,841` (`IsDeletingHistoryAllowed`).
  - The remover's DCHECK at `chrome_browsing_data_remover_delegate.cc:411` says all UI entry points must be disabled.
- **Failure scenario.** On a managed or supervised profile with `AllowDeletingBrowserHistory=false`, the Library's trash button, "Forget This Page", "Remove from history" and "Clear all downloads" still erase history that Chrome's own pages refuse to erase. "Forget About This Site" does the same for the history part.
- **Fix.** Read the pref once in a helper. Hide or disable those actions (row buttons and menu items) when it is false, and make the handlers no-ops. Show the downloads Remove items only when allowed, as `downloads_ui.cc:230` does.
- Verified.

### S2. Download Retry is built from the *active tab's* frame, not like Chrome's desktop Retry
- `koan_library_downloads.cc:867-909`; frame use at `:900-905`.
- **What is wrong.**
  - The retry calls `contents->GetPrimaryMainFrame()->CreateDownloadUrlParameters(...)` with `set_content_initiated(true)`.
  - The download bubble's retry (`download_bubble_ui_controller.cc:359-399`) builds plain `DownloadUrlParameters` with `content_initiated(false)`. The chrome://downloads handler uses its own frame, and its comment (`downloads_dom_handler.cc:~446`) says Retry should not be treated as initiated from the current page.
  - The Library ties the request to whatever tab is active: its frame, network isolation key, and the download-request limiter.
  - It silently does nothing when there is no active WebContents.
- **Failure scenario.**
  - Download from a.com (login cookie `SameSite=Lax`), cancel it, switch to b.com, press Retry. The request goes out with b.com's frame and isolation key. Depending on the cookie it can be sent without the session cookie and fail again, or be saved as a login page. (Uncertain: not run.)
  - It can also be counted as an automatic download by the active tab.
- **Fix.** Copy the bubble's approach: `DownloadUrlParameters(item->GetURL(), annotation)`, `set_content_initiated(false)`, `set_download_source(RETRY_FROM_BUBBLE)` (or `RETRY`), and `DownloadUrl` on the profile's manager. No tab dependency.
- The deviation is verified. The concrete cookie failure is uncertain.

### S3. History and Downloads pagination rebuilds the whole list and re-queries with a growing `max_count`
- `koan_library_history.cc:247-253` and `:331-361` (`limit_ += 100; Fetch()`), `:426-484` (`Show` clears every row and recreates them all), `:486-520` (a favicon request per row).
- Same pattern in `koan_library_downloads.cc:478-485` and `:620-640`.
- **What is wrong.** Each "load more" re-runs `QueryHistory` with `max_count = limit_`, destroys and recreates every row (about 10 views each), and re-issues a favicon lookup for each row. Cost per page grows with the page count, so scrolling N pages is O(N²). Nothing is virtualised.
- **Failure scenario.**
  - A user with 120 days of history (thousands of rows) scrolls down. By the 30th page (3000 rows) each load builds about 30k views and 3000 favicon lookups on the UI thread. The scroll visibly stalls and the stall gets worse.
  - Downloads has the same shape but is bounded by the download count.
- **Fix.**
  - History: page with a cursor. Query `begin_time = Cutoff()`, `end_time = oldest time already shown` (the history page does this), `max_count = 100`, and append rows to the existing groups.
  - Keep `Show` for resets only.
  - For Downloads, append the next slice instead of calling `Rebuild`.
  - Optional: cap the number of rows built.
- Verified by reading. I did not time it.

### S4. The panel opens on the wrong side in a right-to-left UI (regresses #56's RTL contract)
- `koan_library_controller.cc:387-393` (`on_right_ = IsSidebarOnRight(...)`, then `bounds.set_x(...)`), `:347` (`Translate` direction). `koan_library_view.cc:925-988` mirrors with absolute coordinates from the same flag.
- **What is wrong.**
  - `IsSidebarOnRight()` is a logical flag: "Tabs on the right". `koan_toast.cc:215-219` and `scripts/e2e/tabs_right_rtl.mjs:2-5` say that in RTL the sidebar is on the screen's right unless "on the right" is set.
  - The Library treats the flag as a physical side and does not consult `base::i18n::IsRTL()`.
- **Failure scenario.** In an RTL locale with the default setting, the sidebar is at the screen's right. The Library slides in from the screen's left, over the page, and its side column, back arrow and slide direction are all on the wrong edge. With "tabs on the right" set in RTL, the sidebar is at the left and the Library appears at the right.
- **Fix.** Compute `physical_right = IsSidebarOnRight(browser_view_) != base::i18n::IsRTL()` once, as `KoanToast` does, and use it for bounds, slide direction and the view's mirrored layout. Add a Library step to `tabs_right_rtl.mjs`.
- Verified by reading. Not run.

### S5. The "Clear history?" confirmation does not say which range will be erased
- `koan_library_history.cc:709-717` (`ConfirmClearHistory`; the same text for every range).
- **What is wrong.** Title and message are identical for "Last hour" and "Everything" (Zen's strings, `zen-library.ftl:41-42`). `hours` is not passed into the text.
- **Failure scenario.** The menu is one click and a dialog away from irreversible deletion. A mis-click between "Last month" and "Everything" is not catchable at the confirmation.
- **Fix.** Pass the range label into the dialog text and title: "Clear history from the last hour?" or "Clear all history?". Keep the button as "Clear history".
- The code is correct; this is a UX safety gap. Verified.

### S6. Confirmation callbacks die with the section, so an accepted confirmation can silently do nothing
- `koan_library_history.cc:709-717` and `:628-636` (`weak_factory_.GetWeakPtr()` for `ClearHistory` and `DoForgetSite`), `koan_library_spaces.cc:936-947` (`DeleteSpace`).
- `koan_library_controller.cc:351-357` (`Finish` starts the 30 s `cleanup_timer_` that destroys the widget and its sections); `:258-267` (`OnTabStripModelChanged` closes the Library on any tab insertion).
- **What is wrong.** The browser-modal dialog outlives the section when the Library closes behind it (a page opens a tab, then 30 s pass). The user then clicks the destructive button, the WeakPtr is gone, and nothing happens.
- **Failure scenario.** The user opens Clear history, a background tab opens and closes the Library, they read the dialog for over 30 s and press "Clear history". No history is cleared and no message appears.
- **Fix.** Bind the accept callback to the profile (a WeakPtr or `Profile*` captured as a `ProfileKey`) and a plain function. Or do not start the 30 s teardown while a confirmation from this Library is up.
- Low probability. The behaviour is verified by reading.

### S7. The e2e does not test the risky parts, and the "Forget" check passes whether or not anything was deleted
- `scripts/e2e/library.mjs`.
- **What is missing.** There is no check for any of these:
  - **Clear history by range.** The submenu is never opened.
  - **Incognito.** No incognito window is opened; there is no check that the keys fall back and no Library exists.
  - Forget About This Site.
  - Downloads Cancel, Retry, Open and Remove.
  - Closed tabs as a source.
  - Ctrl-click background open and its toast.
  - More than one page of history.
  - Delete Space.
  - Palette "Open Library" (#55).
  - The settings row for "Show All History" (#46).
- **The forget check is weak.** The "Forget removes the page" check (lines 214-238) compares row counts. `ForgetEntry` removes the entry from `entries_` and redraws immediately, before the history service responds (`koan_library_history.cc:600-614`). `waitFor` returns on the first match. The check would pass even if `DeleteURLs` did nothing. Nothing reads the real history afterward.
- Typing is injected with `DOM.dispatchKeyEvent` straight at nodes, and Ctrl+H is sent through the page. The path "Library search field has keyboard focus, user presses Ctrl+H or Esc" is never exercised.
- **Fix.** Add checks that read the real state:
  - A cleared range and a surviving older visit, via `chrome://history` or a second query.
  - An incognito window: the Library is absent and Ctrl+H opens `chrome://history`.
  - Forget removes the URL from `HistoryService`.
  - The row count after a re-fetch.
- The PR text says nothing about what is tested in these areas, but the issue asks for "e2e test(s)" and these are the dangerous parts.

### S8. Keyboard and accessibility: row actions are hover-only, and Retry/Cancel are not in the context menus
- `koan_library_view.cc:388,486-496` (`actions->SetVisible(hovered_ && ...)`), `:362-396` (rows).
- `koan_library_downloads.cc:954-1000` (`ShowMenu`: no Retry, no Cancel). `docs/ZEN_PARITY.md` says "Same".
- **What is wrong.**
  - Rows are focusable (Linux `FocusBehavior::ALWAYS`; macOS `ACCESSIBLE_ONLY`), but the action buttons only become visible on pointer hover. Keyboard focus does not reveal them.
  - The Downloads context menu has no Retry Download and no Cancel. Zen's menu has "Retry Download" (`zen-library.ftl`, `library-downloads-menu-retry`).
  - The row's accessible name is the title only, so status text such as "Canceled", "Failed", "File moved or missing" and the progress ring is not exposed.
  - Section tabs are plain buttons with no selected state.
  - The panel has no role or name.
- **Failure scenario.** A keyboard-only or VoiceOver user cannot retry or cancel a download, and cannot tell that a download failed.
- **Fix.**
  - Reveal the actions on focus (`UpdateHover` should also look at `HasFocus()`), or give every action a menu item.
  - Add "Retry Download" and "Cancel" to the context menu.
  - Set the accessible description from the subtitle.
  - Expose tab role and selected state for the section tabs.
  - Give the panel a role and name.
- Verified by reading. Not run with a screen reader.

### S9. The Spaces section is not "thin": it edits tab groups directly, and Delete Space may close the window
- `koan_library_spaces.cc:702-732` (`MoveSpace`: `MoveGroupTo`), `:772-803` (title/icon/colour via `ChangeTabGroupVisuals`), `:936-948` (`DeleteSpace`: `CloseAllTabsInGroup`), `:575-577,759-770` (re-encodes Koan's "icon name" title convention).
- **What is wrong.**
  - The PR says Spaces "use develop's workspace API thinly, so #57 can replace what's under it". Only `GetWorkspaces`, `SwitchTo`, `CreateWorkspace`, `IsEnabled` and `tab_strip_model()` are API. Rename, icon, colour, reorder and delete are implemented here against `TabStripModel` and the tab-group title encoding.
  - When #57 changes the representation, about 1000 lines must be rewritten and the rebase will conflict.
  - Develop's workspaces have no delete (`koan_workspaces.cc`), so the Library introduces new delete semantics in a view.
- **Failure scenario (uncertain).** Deleting the only Space closes all its tabs. If that empties the window, the window closes (possibly the last one). The dialog only says "all its tabs will be closed". I did not run this.
- **Fix.** Add `RenameWorkspace`, `SetWorkspaceIcon`, `SetWorkspaceColor`, `MoveWorkspace` and `DeleteWorkspace` (plus a `GetWorkspaceInfo` returning name, icon, colour, tabs) to `KoanWorkspaceController` in `koan_workspaces.{h,cc}`. The Library then only calls them. Refuse or specially confirm deleting the last Space. Coordinate with #57 before merging.
- The layering finding is verified. The window-close part is uncertain.

### S10. The Spaces section keeps rebuilding while it is hidden
- `koan_library_spaces.cc:541-548` (`ScheduleRebuild`: checks `shown_` and `library()->is_open()` but not whether the section is visible), `:519-537` (observers for every tab change, title or loading-state update).
- **What is wrong.** `shown_` stays true once Spaces has been shown. While the Library is open on History or Downloads, every tab title, favicon or loading change schedules a rebuild of all cards, all tabs, and a `OnPreferredWidthChanged`. This runs about 6 times a second during a page load. The result is thrown away. `OnShown` rebuilds anyway.
- **Failure scenario.** With many tabs loading (restore), the idle Library on History costs a full card rebuild every 150 ms.
- **Fix.** Add `GetVisible()` to the guard (as History does in `OnDataChanged`) and mark the section dirty instead. Do the same for the Downloads rebuilds in `ScheduleRebuild` (`koan_library_downloads.cc:608-618`).
- Verified by reading.

---

## Nits (14)

### N1. "Today" means last 24 hours in the filter and since midnight in Clear history
- `koan_library_view.cc:707-718` (`GetWhenDays`: today = 1 day), `koan_library_history.cc:308-310` and `:722-724` (`LocalMidnight`), `koan_library_downloads.cc:590`.
- After Clear history → Today, the History "Today" filter can still show yesterday-evening pages. I could not check what Zen does: `moz-src/zen/library/*.mjs` is not in `.cache/zen-src` (only the ftl and css). Make the filter use `LocalMidnight()` or document the difference.
- Uncertain parity; the inconsistency is verified.

### N2. Rows from another source stay visible under "Closed tabs" until the TabRestoreService loads
- `koan_library_history.cc:388-392`.
- `FetchClosedTabs` returns after `LoadTabsFromLastSession()` without clearing or showing "loading". The old history rows remain under the Closed tabs chip until `TabRestoreServiceLoaded` fires. Clear the list or show a placeholder first.

### N3. `docs/REBASE.md` has one row for a feature that depends on many upstream surfaces
- `docs/REBASE.md:50`.
- It lists only `contents_container()` and `tab_strip_view()`. Unlisted:
  - `sidebar->GetClassName() == "VerticalTabStripRegionView"` (`koan_library_controller.cc:368`, the existing "Workspace switcher mount" row covers the ctor only);
  - `BrowsingDataRemover::Remove` with `chrome_browsing_data_remover::DATA_TYPE_HISTORY` (a Chrome-specific bit);
  - `AllDownloadItemNotifier`, `IconManager`, `TabRestoreService::RestoreEntryById`, `BrowserLiveTabContext::From`;
  - `TabStripModel::MoveGroupTo` and `CloseAllTabsInGroup`;
  - the `//chrome/browser/browsing_data:constants` GN dep.
- Add rows so a rebase knows what to diff.

### N4. Parity doc overstates two rows
- `docs/ZEN_PARITY.md`, Downloads menu row: Zen's menu also has "Retry Download" and "Clear Download Preview" (`zen-library.ftl`). Koan has neither.
- "Forget About This Site" is in Koan's History row menu, but Zen's Library strings (`zen-library.ftl:84-91`) only have "Reopen Page" and "Forget This Page". The extra action is Firefox's sidebar one (`browser.xhtml:1118`). It also deletes cookies and site data, a bigger action than the rest of the menu. Mark it as a Koan addition. (Uncertain: Zen's menu code is not in the cache.)

### N5. Image previews read whole files into memory and are thrown away on every rebuild
- `koan_library_downloads.cc:305-311,732-751`, `:620-622` (`preview_weak_factory_.InvalidateWeakPtrs()` on each `Rebuild`).
- Each uncached image row reads up to 20 MB with `ReadFileToStringWithMaxSize` before decoding. A rebuild (every download create/finish/remove, every filter change) cancels the pending results, so those reads repeat. `previews_` is keyed by path, so a re-downloaded file at the same path keeps the old picture.
- Check the size before reading. Cache by path plus mtime. Do not invalidate pending previews on rebuild.

### N6. The Spaces card tint and colour menu use Chrome's tab-group colour IDs, not Koan IDs (#48)
- `koan_library_spaces.cc:326` (`GetTabGroupTabStripColorId`), `:893` (`GetTabGroupContextMenuColorId`).
- Everything else uses `kColorKoanLibrary*`. #48 restyling through Koan IDs will not reach the per-Space colours. Add a Koan seed-colour ID per tab-group colour, or route them through the mixer.

### N7. The colour swatch is generated at 1x
- `koan_library_icons.cc:144-155` (`ColorSwatch`: `gfx::Canvas(..., 1.f, ...)` then `CreateFrom1xBitmap`).
- On Retina (the product) the 16 px swatches in the colour menus are blurry. Use `ImageModel::FromImageGenerator` with a scale-aware canvas (the callback receives the provider only, so render at 2x and downsample, or draw with a `VectorIcon`).

### N8. After "Delete File" or a download change, the row keeps stale actions
- `koan_library_downloads.cc:697-723` (`UpdateRows` refreshes text and dimming only), `:608-640`.
- A deleted file's row shows "File moved or missing" but keeps its "Show in folder" button until the next rebuild. `OnDownloadUpdated` only rebuilds when `pending` flips.
- Rebuild also when `HasFile` or `CanRetry` changes.

### N9. "Delete File" removes the file with no confirmation
- `koan_library_downloads.cc:923-929` (`DeleteFile(DoNothing())`).
- It is irreversible, next to "Remove from history". Zen may do the same (its code is not in the cache), so uncertain. Consider a confirmation or a toast.

### N10. No reduced-motion handling
- `koan_library_controller.cc:59-65,303-309` (spring and width animation).
- Respect `gfx::Animation::PrefersReducedMotion()` and jump to the final state.

### N11. The Library button's tooltip keeps the old shortcut after a rebind
- `koan_library_controller.cc:412-423` (`CreateLibraryButton` reads the accelerator at build time), used from `koan_workspaces.cc` `Rebuild()`.
- Rebinding in settings does not change the tooltip until the switcher next rebuilds. Uncertain how often that happens.

### N12. macOS key routing needs a hands-on check (uncertain)
- `src/browser/koan_shortcut_table.cc:148-171`.
- ⌘Y, ⌘⇧J and ⌘⇧H are also Chrome menu key equivalents.
  - From `chrome_command_dispatcher_delegate.mm` the pre-dispatch offers the FocusManager only `kHighPriority` accelerators; Koan registers `kNormalPriority`.
  - Existing ⌘ shortcuts (⌘⇧C) work, so this probably works too. But "press ⌘Y again to close" while the Library's own search field has focus is a different path from a web-page focus, so test it.
  - ⌘⇧H also shadows Chrome's Home even in incognito and with the flag off (it opens History, `koan_shortcuts.cc:235-240`). That matches Zen but is a visible change.

### N13. Small test-harness points
- `scripts/e2e/run.sh:10` is now over 80 columns.
- `sed "s#{profile}#$prof#g"` breaks on a profile path containing `&` or `\`, and the `PREFS` environment variable path (`run.sh:77`) is not substituted.

### N14. Command Palette and settings rows (#55/#46) show up unconditionally
- `src/browser/koan_shortcut_table.cc:~79,86` (`kLibrary` "Toggle Library" and `kLibraryHistory` "Show All History" have no availability predicate).
- With the Library flag off or in an incognito window, the settings page still lists them, and the keys open Chrome's History. Add `features::IsLibraryEnabled` as the `is_available` predicate for consistency with the other feature-owned rows.
