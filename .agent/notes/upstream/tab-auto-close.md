# Closing tabs that haven't been used (#482): design

Date: 2026-11-27, pin Chromium 158. Read only: nothing was built or run when
this was written. Code: `src/browser/koan_tab_auto_close.*` (the rule, pure),
`src/browser/ui/views/koan_tab_auto_closer.*` (the window's closer, the
Library's reopen), `koan_library_history.cc` (the closed-tabs source).

## What it copies

Arc "archived" tabs that weren't pinned and hadn't been used for 12 hours (or
24 hours, 7 days, 30 days), and listed them in its Archive. Zen has nothing
like it (two open requests, 261 and 91 upvotes). Brave has no equivalent
either; Chromium's Memory Saver unloads a hidden tab after 4 hours but keeps
its row.

## What upstream gives

| Piece | Use |
|---|---|
| `WebContents::GetLastActiveTime()` | When the tab was last *shown* (set on `WasShown`, saved as `SessionTab::last_active_time`, given back by `chrome::AddRestoredTab`). Not when it was left: a tab on screen for a day and then left reads as a day old. |
| `DiscardEligibilityPolicy::CanDiscard` (via `koan::IsTabInUse`, Unload Space's test) | Sound, picture-in-picture, capture, mirroring, devices, DevTools, form input, extension or site exemptions. It has no reason for downloads or `beforeunload`. |
| `TabStripModel::CloseWebContentsAt(…, CLOSE_NONE)` | Closes without `CLOSE_CREATE_HISTORICAL_TAB`, so the tab doesn't enter `TabRestoreService`. |
| `TabRestoreService` | Keeps 25 entries (`TabRestoreServiceHelper::kMaxEntries`, a constant) and is what ⌘⇧T reopens. |
| `chrome::AddRestoredTab` | Reopens a tab from serialized navigations and extra data, running Koan's tab restore hooks (container, Space data), as Recently Closed does. |
| `SessionRestore::IsRestoring`, `TabDragController::IsActive` | When not to check. |
| `HistoryServiceObserver::OnHistoryDeletions` | Clearing history. |

No hook patch is needed.

## Decisions (for review)

1. **Where closed tabs are kept: Koan's own list, not a raised limit.** A
   profile pref, `koan.tabs.auto_closed`, newest first, with URL, title,
   time, Space (its tab group token) and the tab's Koan session data
   (container, Space theme). Kept 120 days (the Library's History range) and
   at most 500. Raising `kMaxEntries` would need a patch to a
   `components/sessions` constant and would put automatic closes into ⌘⇧T,
   so a user's "undo close" could reopen a tab they never closed. The list
   being separate means: ⌘⇧T is untouched; the Library merges both lists by
   time. Cost: only the tab's current page comes back, not its back/forward
   list (Arc's archive also gave back only the page).
2. **No per-Space opt-out ("Keep this Space's tabs") in the first version.**
   A Space's settings live in its tabs' session data (`WriteSpace()` writes
   each key to every tab of the Space), so a new flag means touching
   `koan_workspaces.cc` in several places that other open work (#469, the
   container issues) changes too. The setting is off by default, and
   Essentials, pinned tabs and folders already keep what the user wants to
   keep. It is a follow-up on #482.
3. **One coarse timer per window**, owned by `KoanBrowserViewController`:
   every 5 minutes, and 30 s after the window opens or the setting changes,
   so a tab that aged while Koan was closed goes once the session is back
   (a check while `SessionRestore::IsRestoring` waits for the next one).
   Per window rather than one global timer: the window's controllers
   (Spaces, folders, pinned tabs) give the exemptions, and the timer goes
   with the window.
4. **"Used" is the later of when the tab was last shown and when the closer
   last saw it on screen.** The closer writes `koan.last_seen` (per-tab
   session data) for the tabs on screen at each check and for the tab left
   at each switch, at most once a minute per tab. So a tab counts from when
   it stopped being the visible tab, across restarts. A tab never shown
   counts from when it was made.
5. **The rule is one pure function** (`auto_close::WhyKept`), unit-tested
   per exemption: Essentials, pinned tabs, folder tabs, the tab each Space
   shows (and tabs selected with it), a Space's empty tab, agent tabs
   (`containers::IsAgentContents`: own or leased), in use, used within the
   limit, and a split view with any tab that stays (a split closes whole or
   not at all). "In use" is `IsTabInUse` plus a download the tab started
   still running, plus a `beforeunload` handler (closing could show "Leave
   site?" in a background tab). An unloaded tab is never "in use".
6. **One-time notice.** The first check that would close anything shows a
   toast in the profile's front window ("… will close soon. Closed tabs are
   in the Library's History.", a Settings button to Tab Management, 10 s)
   and closes nothing, and records when (`koan.tabs.auto_close_notice_time`).
   Every window's checks close only once the notice has been up a check
   interval (5 minutes; at least its 10 s with the test switch), so neither
   the window's next check right after it nor another window closes tabs
   while it shows. A shorter time chosen later applies at the next check.
7. **Never in a private window** (or any off-the-record profile): its tabs
   couldn't be found again. Normal windows only.
8. **A Space is never emptied**: a Space is a tab group that goes with its
   last tab, so if all of a Space's tabs would close, its most recently used
   one stays (its last active tab normally stays anyway).
9. **Clearing history forgets the closed tabs it covers** (all, a time
   range, or pages), as Chromium does for its recently closed list.
   Expiry doesn't count. The list is also the Library's to forget a row
   from. **One profile's history** (#474: the Library's Clear history,
   Forget This Page and Forget About This Site while it shows one profile)
   forgets only that profile's closed tabs, by the container in their
   session data, as the Library's filter reads it. Chromium's notice of
   that deletion (`history::DeletionInfo`) doesn't say which profile it
   was, so the Library calls `DeleteProfileHistory()`, which forgets that
   profile's closed tabs itself, deletes the visits with
   `ExpireHistoryBetween(…, app_id, …)`, and marks the deletion (its range
   and pages, kept with the profile until `ExpireHistoryBetween()`'s reply,
   which comes after the notice) so every window's closer lets its notice
   go by. A deletion that names no profile (Clear browsing data, an
   extension's `chrome.history`, the Library with All profiles) still
   forgets every profile's closed tabs it covers: it clears every profile's
   history. Browser test:
   `ClearingOneProfilesHistoryKeepsTheOthersClosedTabs`.
10. **Testing switch** `--koan-auto-close-tabs-after=<seconds>`: replaces
    the set time (never Never) and checks a quarter of that often (1 s at
    least), for the manual steps and the browser tests.

## Hard cases

| Case | Behaviour |
|---|---|
| A tab moved between Spaces or windows | Its times move with its WebContents and session data: unchanged. A drag that makes it the active tab shows it. |
| A tab in a profile (container, #318) | Its `koan.container` data is kept and given back on reopen, so `SiteInstanceForRestoredTab` puts it in the same container (a deleted container's tab comes back blank and closes, as from Recently Closed). |
| A tab the user unloaded | Counts by when it was last shown; never "in use". |
| Several windows | Each window judges its own tabs. |
| A restored tab with form text in its page state | Not loaded, so not "in use": it can close, and the form text is lost (the list keeps only the URL). |
| Startup without restoring the session | Nothing to age. |

## Not done

- The per-Space opt-out (decision 2).
- Clearing history while no window is open isn't seen by the list.
- **A restart resets the age of a background tab that was never shown.**
  Chromium saves a tab's last-active time only when the tab is shown, and a
  restored tab never shown gets the restore time; `koan.last_seen` is
  written only for tabs that were on screen. So a tab opened in the
  background and never looked at counts from the last start, and with the
  time set to 12 hours, someone who restarts Koan more often than that never
  has those tabs closed. It errs on the side of keeping tabs; a follow-up
  (writing `koan.last_seen` when such a tab is made) is #577.
- The notice is a toast; if the front window is minimised it can go unseen
  (it is still marked shown).
