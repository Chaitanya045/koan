# Window sync (#75, Z7 of #161): design and upstream audit

Zen 1.24b keeps every normal window of a profile showing the same Spaces,
Essentials and pinned tabs. This note is the design ("one owner, deferred
moves", as the #161 audit asked) and the map of the Chromium 158 behaviour it
sits on. It is written before the code; each PR of the series links here.

## What Zen does (1.24b source)

`src/zen/sessionstore/ZenWindowSync.sys.mjs` (`nsZenWindowSync`), prefs in
`prefs/zen/window-sync.yaml`:

| Pref | Default | Meaning |
|---|---|---|
| `zen.window-sync.enabled` | true | Windows sync at all. |
| `zen.window-sync.sync-only-pinned-tabs` | false | Only pinned tabs and Essentials sync; normal tabs stay per window. |
| `zen.window-sync.prefer-unsynced-windows` | false | Swaps what New Window and New Blank Window open. |
| `zen.window-sync.open-link-in-new-unsynced-window` | true | A link opened "in a new window" opens a blank (unsynced) one. |
| `zen.session-store.restore-unsynced-windows` | true | Blank windows come back after a restart. |

- **What syncs.** Spaces (`propagateWorkspacesToAllWindows`: name, icon,
  theme, order), and tabs: every tab exists in every synced window with the
  same id. It syncs open, close, move, pin/unpin, add to / remove from
  Essentials, label and icon changes, hide/show, folders (tab groups: create,
  rename, move, remove) and split views. With `sync-only-pinned-tabs` only
  pinned tabs and Essentials are copied (`on_TabOpen` returns early for
  others; `on_TabPinned` creates the copies, `on_TabUnpinned` closes them).
- **One page per tab.** A synced tab has one live browser (`_zenContentsVisible`);
  the copies in the other windows are `about:blank` lazy browsers.
  `on_TabSelect` and window `focus` queue a docshell swap
  (`#enqueueSwap`, one at a time) so the focused window's selected tab gets
  the live browser (`swapBrowsersAndCloseOther`, then `about:blank` is loaded
  in the other one). A swap in flight is skipped for a tab
  (`#inflightSwapTabs`). Re-entrancy: events from another window are
  dropped while one window's event is handled (`#eventHandlingContext`).
- **Window close.** `SSWindowClosing` moves every live browser of the
  closing window to the most recently used synced window
  (`#moveAllActiveTabsToOtherWindowsForClose`), synchronously, then fixes the
  titles once the session flushed (`WindowCloseAndBrowserFlushed`).
- **Unsynced ("blank") windows.** `cmd_zenNewNavigatorUnsynced`
  (Accel+Shift+N, File > New Blank Window, the palette's "New Blank Window",
  the app menu) opens a window with `zen-unsynced-window`:
  `gZenWorkspaces.privateWindowOrDisabled` is true there, so it has no
  Spaces, no Essentials and no sync, like a private window. Private windows,
  popups and taskbar tabs are never synced.
- **Session.** `ZenSessionManager.sys.mjs` keeps one sidebar object (Spaces,
  tabs, folders, split views) for all synced windows and restores every
  synced window from it; unsynced windows are restored on their own when
  `restore-unsynced-windows` is on.

## Koan's scope

Koan syncs **Spaces, Essentials and pinned tabs** (Zen with
`sync-only-pinned-tabs` on). **This departs from Zen 1.24b's default**, which
syncs every tab. The choice is a pref, `koan.window_sync.sync_only_pinned`
(default true for now), so it can be flipped once normal tab sync exists.
Normal tabs stay per window, as the issue's
"cheap first step" allows: copying every tab into every window doubles the
tab strip work of each window for each window, and Koan's empty tab, Glance
and split views would all need copies. Folders (#60) sync
with the pinned tabs, through the same model: `FolderRecord`s (name,
collapsed, Space, parent, in sidebar order; `collapsed` isn't synced live, as
in Zen (#262): `PlanFolders()` leaves a folder that differs only in it out of
`update`, `SetFolders()` saves it without calling it a change, and only a
folder a window creates (a new window, a restore) takes it) and each pinned item's
`folder_id`. A folder's sync id is its session token, the same in every
window (a folder made by the sync takes the other window's token), so a
placeholder joins its folder through the page's copied session data, and
windows agree again after a restart. Each window keeps the tokens of its
synced folders in its session data (`koan.window_sync.folders`), so a joining
window's own folders are added to the model, not deleted. A window reports
its folders right before its items (`ReportFolders`, on the folder
controller's change callback too) and applies them first
(`ApplyFolders`: unpack the removed ones, add or update in target order,
then the order), and puts the tabs in their folders after the items
(`ApplyFolderMembership`). A folder deleted elsewhere is unpacked here: its
tabs follow their own sync. Koan's folders have no icon (#60 left Zen's
icon picker out). Split views are not synced: a synced tab in a split
keeps its page in its window.

## Design: one owner, deferred moves

- **One owner.** `KoanWindowSync` (koan/browser/ui/views/koan_window_sync.h)
  is per profile (user data on the regular `Profile`) and owns the canonical
  state, `window_sync::WindowSyncModel`
  (koan/browser/window_sync/koan_window_sync_model.h): the ordered Spaces,
  the ordered synced tabs ("items") and, for each item, the window with its
  page. Items and Spaces have sync ids (UUIDs) that are the same in every
  window; a window maps them to its own tab group ids and tabs.
- **Windows observe it.** Each synced window has a `KoanWindowSyncClient`
  (owned by `KoanBrowserViewController`). It *reports* the window's state
  after the user changes it (`ReportSpaces`, `ReportItems`) and *applies* the
  model when asked (`ApplySpaces`, `ApplyItems`). State, not events: a
  window reports its whole list, the model diffs it, and a window is
  reconciled to the whole target (`PlanSpaces`, `PlanItems`: remove, add,
  update, order). Missed or doubled notifications can't drift windows apart,
  and the plan is pure and unit tested.
- **Deferred.** A report never changes another window. The model marks the
  other windows out of date and a posted task (`Flush`) reconciles one
  window per task. `TabStripModel` CHECKs (release) against being changed
  from its own notifications (`ReentrancyCheck`, tab_strip_model.cc), and
  every report comes from one; a posted task is never inside one. A window
  that closes before its turn is just dropped from the lists.
- **Re-entrancy.** While a window is reconciled, its reports are ignored
  (`IsApplying`): what changes is the sync's own doing, and Koan's
  controllers in that window (Spaces' `EnsureOneSpaceShown`, pinned tabs'
  `Normalize`) skip their user-facing rules for it. A window that is out of
  date can't report either (its state is stale and would undo the newer
  change): its reports count again once it is reconciled. The user can't
  change an out of date window in between in practice (one task).
- **Joining.** A new synced window is out of date from the start when the
  model has state, so it takes the other windows' Spaces and items first and
  its own startup Space never overwrites them. With no state yet (a new
  profile, or the first run after updating), its first report seeds the
  model.

### Moving a page between windows

A synced tab exists in every synced window. One of them has the page; the
others have a **placeholder**: a tab whose `WebContents` never navigated
(nothing loads, no network), shown in the sidebar with the live tab's title
and favicon (`koan::AdjustTabData`, the existing tab_data.cc hook) and the
item's custom label and icon. The row follows the page: a `kAll` change of
the live tab (`OnTabChangedAt`; Zen's ZenTabLabelChanged / ZenTabIconChanged,
which Zen also takes only from the window with the page) makes every other
window's placeholder ask for a UI update
(`NotifyNavigationStateChanged(INVALIDATE_TYPE_TAB)`, coalesced by Browser),
and its row reads the live tab's `TabUIHelper` again
(`AdjustPlaceholderTabData`). Nothing is copied into the placeholder.

The page moves **on activation**: when a window shows a placeholder (the
user selects it, or the window is activated with it selected, Zen's
`TabSelect` and `focus`), the client asks the model (`RequestShow`) and the
sync queues a move. Moves are deferred like reconciles: one per task, each
re-checked when it runs (both windows still synced, the tabs still there,
the item still live where it was; otherwise it is dropped or it loads in
place). Coalescing: a second request for the same item replaces the queued
target.

A move swaps `WebContents`, not tabs, with
`TabStripModel::DiscardWebContents()` (the replace that Chromium's discard
uses): in the window that has the page, a fresh placeholder replaces the
live contents and is returned; in the window that shows the tab, the live
contents replace its placeholder, which is deleted after the task. Each
window keeps its own `TabModel` (so its tab handles, Space membership,
Essentials position and session id stay put), `Browser::OnTabReplacedAt()`
re-delegates the contents to the new window, and session service and tab
restore see a replace. The order is fixed: source first, then target
(`TabLifecycleUnitSource::OnTabReplaced` DCHECKs that the incoming contents
carry no lifecycle unit; the source's replace takes it away).

**Containers.** A placeholder is created in the live tab's container
(`containers::SiteInstanceForReplacement()`, as Chromium's discard does in
Koan), so a container's page never swaps into, or loads in, the default
partition. **Agent-container tabs are never synced**
(`containers::IsAgentContainerContents()`): they are not items, and a tab an
agent leases keeps its page where it is while leased.

With no live page anywhere (its window closed without a rescue, after a
crash, after a restart that restored it nowhere), the placeholder loads the
item's URL in place and becomes live.

### Teardown

- **Window closing.** `UnloadController::OnWindowClosing()` (one-line hook,
  PR 3) asks the sync to hand the window's pages over before
  `CloseAllTabs()`: each live item of the window moves to the most recently
  active other synced window (`OnWindowClosing`). It runs before the tab
  strip's own close (its `ReentrancyCheck` is not held there) and only when
  not quitting (`browser_shutdown::IsTryingToQuit()`). If a beforeunload
  dialog then cancels the close, the window keeps placeholders, which is
  harmless.
- **A synced tab closed.** A window notes the synced tabs the user closed
  (a removal that deletes the contents, outside `closing_all()` and outside
  the sync's own changes) and sends them with its next report
  (`ReportItems(..., closed)`). The model remembers them
  (`WasClosed()`, until the item comes back: a reopened tab keeps its id),
  and every other window closes its tab, page or placeholder, without
  Recently Closed (Zen's `TabClose`). A Space left without tabs by it gets a
  new tab first. Unpinning a synced tab, or taking it out of the Essentials
  without pinning it, counts as a close (#262, Zen's `on_TabUnpinned` calls
  `on_TabClose` with only pinned tabs synced): `ReportItems()` adds every
  tab that lost its id to `closed`, except one in a split view, which
  stays as a normal tab where its page is.
- **Window gone.** `RemoveWindow` drops the window from every list and
  forgets the pages it had; queued moves from or to it are dropped when they
  run.
- **Profile gone.** The sync is user data of the profile and outlives every
  window of it.

### Session restore

The model is saved in a profile pref (`koan.window_sync.state`, like
`koan.workspaces.list`), not in the session: the session keeps each window's
own tabs. A placeholder has no navigation entries, so session restore drops
it; the live tab comes back in its window. After a restart every synced
window is reconciled to the saved model: missing items get placeholders,
and the one restored live tab stays live. Which window has a page is not
saved.

### Blank windows across restarts

A blank window marks its session entry (window session data
`koan.window_sync.blank`), like Zen's `isZenUnsynced`. Session restore and
a reopened window (`BrowserLiveTabContext::Create`) call
`koan::WillRestoreWindow()` before `CreateBrowserWindow`, so the window is
marked blank before its controllers decide whether it has Spaces (the
existing `OnRestoringWindow()` hook runs after creation). Zen's
`restore-unsynced-windows` is on by default; Koan has no "off" (Zen then
drops blank windows from the restore).

`prefer-unsynced-windows` (Zen's `BrowserWindowTracker.openWindow` patch
swaps the flag of every window it opens) is
`koan.window_sync.prefer_blank_windows`: Koan's command policy takes
`IDC_NEW_WINDOW` and opens a blank window, and New Blank Window opens a
synced one.

## Upstream audit (Chromium 158)

| Upstream | Decision | Why / test |
|---|---|---|
| `TabStripModel::ReentrancyCheck` (release CHECK) | **reuse** as the rule | Every cross-window change runs in its own posted task. Mac step: change a Space in one window while another window is closing. |
| `TabStripModel::DiscardWebContents()` | **reuse** for page moves | Keeps tab identity per window. Guarded by the Mac steps of PR 3 (move, close mid-move). |
| `TabLifecycleUnitSource::OnTabReplaced` DCHECK | **reuse**, order source → target | REBASE row of the move code. |
| `Browser::OnTabReplacedAt()` (delegate, find bar, loading UI) | **reuse** | The moved page's dialogs and find bar follow the window. |
| `DetachTabAtForInsertion` / `InsertDetachedTabAt` (tab drag) | **not used** | Moving tabs would empty strips (Koan's empty tab, `TabStripEmpty`) and change tab handles. |
| `TabGroupSyncService` / saved groups | **lock** (as before) | Spaces stay out of saved groups (`DisconnectSavedGroups`). |
| `BrowserCollectionObserver` | **not used** | Activation comes from the window's widget, which Koan observes already. |
| Session restore of windows | **reuse** | Placeholders have no entries and are dropped; the model reconciles. |
| Session restore / reopened window creation (`CreateRestoredBrowser`, `BrowserLiveTabContext::Create`) | **hook** (2 lines each) | A blank window comes back blank: marked before the window exists. |
| `chrome::NewEmptyWindow` / `IDC_NEW_WINDOW` | **reuse** (PR 4); **redirect** with `prefer_blank_windows` (command policy) | New Blank Window opens a normal window marked unsynced before its BrowserView exists. |
| Incognito, popups, apps, devtools | **lock** | `KoanWindowSync::ShouldSync()`: normal windows of a regular profile only. |
| macOS: Cmd+Shift+N (Chrome's Incognito) | **override** (PR 4) | Zen's New Blank Window; Koan already moves Private to Cmd+Shift+P. |
| macOS: File menu | **override** (PR 4) | Adds New Blank Window under New Window. |

## PRs

1. The model and the observers (this note, `WindowSyncModel`, `KoanWindowSync`,
   `KoanWindowSyncClient`, prefs). Nothing is applied yet.
2. Spaces sync.
3. Essentials and pinned tabs: placeholders, page moves, window close.
4. New Blank Window and Zen's link prefs.
