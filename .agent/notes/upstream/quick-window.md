# Quick window for links from other apps (#472): design and upstream audit

Date: 2026-11-27. Chromium 158 (pristine `git show HEAD:`), Koan `develop` `d410395e`. Arc's Little Arc is the
reference (Arc is closed source: its behaviour as the issue lists it). Zen has no equivalent (two open requests,
zen-browser/desktop#5293 and #4633); Brave has none either.

## Where links from other apps arrive (checked in the source)

The issue's pointer ("external links arrive in `application:openURLs:`") is the Mac half only. Both platforms meet in
**`StartupBrowserCreatorImpl::OpenTabsInBrowser()`** (`chrome/browser/ui/startup/startup_browser_creator_impl.cc`):

| Platform | Path |
|---|---|
| macOS | `application:openURLs:`, Handoff (`continueUserActivity:`) and the links held until startup ends (`openStartupUrls`) → `OpenStartupTabsInBrowser()` → `OpenStartupTabsInBrowserWithProfile()` (`app_controller_mac.mm`), which picks `FindTabbedBrowser()` (creating and showing an empty window if there is none) and calls `launch.OpenTabsInBrowser(browser, kNo, tabs, kNo)` inside Koan's `ScopedExternalLinks` (#81). |
| Linux | `koan <url>`, `flatpak run dev.koan.Koan <url>` or `xdg-open <url>` while Koan runs: the new process hands its command line to the running one through the **process singleton** → `StartupBrowserCreator::ProcessCommandLineAlreadyRunning()` → `ProcessCmdLineImpl()` → `LaunchBrowserForLastProfiles()` → `LaunchBrowser()` → `StartupBrowserCreatorImpl::Launch()` → `DetermineURLsAndLaunch()` (`LaunchResult::kWithGivenUrls`) → `RestoreOrCreateBrowser()` (`USE_EXISTING`: `GetExistingBrowserForOpenBehavior()`, the most recently *active* window on the current workspace, which can be a popup) → `OpenTabsInBrowser()`. Before this PR nothing marked these as external links, so #81's "Default route for external links" did not apply on Linux. |
| Linux, cold start | Same `DetermineURLsAndLaunch()`, but a session restore (`SYNCHRONOUS_RESTORE`) opens the links itself (`SessionRestore::RestoreSession(..., tabs)`), not through `OpenTabsInBrowser()`. |

So the hook is at the start of `OpenTabsInBrowser()`, and Linux gets the same `ScopedExternalLinks` mark around
`RestoreOrCreateBrowser()` when the command line has URLs.

## Window: popup, not New Blank Window

| | `Browser::TYPE_POPUP` (chosen) | New Blank Window (#75, `TYPE_NORMAL` without Spaces) |
|---|---|---|
| Top row | Upstream's popup row: OS title bar with the page title, then a read-only address (security chip, URL, page actions). No tab strip. | Koan's sidebar with a plain tab list, Zen's URL bar, the full toolbar. All of it would have to be hidden. |
| New tabs the page opens | Upstream sends them to a tabbed window: `GetBrowserWindowForDisposition()` (`browser_navigator.cc`) can't open a tab in a popup and uses `GetOrCreateBrowser()` → `FindTabbedBrowser()`: **the most recent normal window**, as the issue asks. Cmd+T in the popup does the same. | Open in the blank window itself; would need a redirect. |
| Session restore | Popups are restored (`SessionService::IsRelevantWindowType`), unless `BrowserWindowCreateParams::omit_from_session_restore` (upstream's own flag, PiP uses it). Closing a popup is not recorded as a closed window (`UnloadController::ProcessPendingTabs` notifies `TabRestoreService` for `TYPE_NORMAL` only). | Restored as a blank window (`koan.window_sync.blank` session data); would need its own opt-out. |
| Spaces, window sync, Library | None: every Koan controller checks `GetIsNormalType()`; `KoanWindowSync::ShouldSync()` needs a normal window. | Already outside sync, but a normal window everywhere else. |
| URL bar | Read-only (`OmniboxViewViews` in popup mode); Koan's floating bar runs only in normal windows. | Koan's floating bar works. |
| Moving the page out | `TabStripModel::DetachWebContentsAtForInsertion()` + `InsertWebContentsAt()`, as upstream's `chrome::ConvertPopupToTabbedBrowser()`. | `DetachTabAtForInsertion()` / `InsertDetachedTabAt()` (tab dragging). |

The issue's pointer for the move (`DetachTabAtForInsertion` / `InsertDetachedTabAt`) does not work for a popup: a
`TabModel` can't move between window types (crbug.com/334281979, the comment in `ConvertPopupToTabbedBrowser()`). The
WebContents moves instead and its tab is made anew in the normal window: same page, no reload, page state kept; the
tab's own features (find bar text, ...) start fresh.

## Decisions

1. **Setting:** Settings > Tab Management > Workspaces, "Links from other apps open in": *A Space (Space routing)*
   (default, today) or *A quick window*. Pref `koan.space_routing.external_links_open_in` (`space` / `quick-window`).
2. **Which links:** a link from another app opens in a quick window only if it is http(s), matches no Space route (a
   route to "Most recent space" or to a Space the window lacks counts as a route: routing takes it as today), the
   profile is not off the record, and there is a normal window with tabs to come back to. Each link gets its own
   window. Everything else opens as today.
3. **A window that shows nothing but a new tab page** takes the link as today: macOS's
   `OpenStartupTabsInBrowserWithProfile()` closes a lone new tab page after the links open, which would close that
   window; and there is nothing in it to keep. Same rule on Linux.
4. **No window open** (macOS keeps running without one): the link opens a window as today. A quick window needs a
   window to come back to (Move to Space, the new tabs its page opens).
5. **Cold start on Linux** (the link starts Koan): restored with the session as today. On macOS the held links come
   through the same function after startup and can open a quick window.
6. **Profile (#318):** the page is created in the container of the Space the link would have gone to: the Space the
   default route for external links names, else the most recent window's active Space. Moving it keeps its partition
   (a WebContents can't change partition); in a Space with another profile its colour line shows (existing rule).
7. **Top row:** upstream's popup row (title, read-only address with the security chip) plus, over its trailing end,
   two of Zen's Glance buttons (same colours: `kColorKoanGlanceButton*`; 28 px instead of 32 to fit the row): **Move
   to Space** (Glance's Expand icon: the page becomes a tab) and **Close**. The buttons cover the address row's page
   action icons (zoom, translate), which a quick window rarely shows.
8. **Keys:** Esc closes, Accel+W closes (upstream's Close Tab on the one tab), Accel+O sends the page to the most recent
   Space (Arc's ⌘O "Open in"). The issue's "Enter sends it to the most recent one": plain Enter belongs to the page, and
   Views menus only preselect their first item on Windows (`PlatformStyle::kAutoSelectFirstMenuItemFromKeyboard`), so
   the menu lists the most recent Space first and Accel+O is the one-key path. Esc and Accel+O are normal-priority
   accelerators: a page that handles the key (preventDefault) keeps it.
9. **Move to Space menu:** the Spaces of the most recent normal window, its active Space first; the page goes to the
   end of the Space, which is shown, and the window is activated. Without Spaces there: "Move to Window"; with no
   normal window left: "Move to New Window" (`chrome::ConvertPopupToTabbedBrowser()`).
10. **Empty quick window:** "New Quick Window", ⌘⌥N on macOS (Arc's key; Chrome's New Split View key there, which Zen's
    split keys replace, and Koan's menu refresh strips it from Chrome's item), Ctrl+Alt+Shift+N elsewhere (Ctrl+Alt+N
    is Create Space). The popup's address is read-only and Koan's floating URL bar only runs in normal windows, so the
    command opens the **floating URL bar of the most recent normal window** for a quick window: what the user picks
    opens in a quick window (in that window's active Space's profile); an open tab's row still switches to it; Esc or
    a click outside opens nothing. Also in the command palette, File > New Quick Window (macOS), and the shortcut
    editor.
11. **Placement:** centred on the display for new windows (the one with the pointer / active window), 60% x 75% of
    its work area, between 480 x 360 and 1000 x 840.
12. **Agent tabs and Incognito:** unchanged. Agent control never opens tabs through `OpenTabsInBrowser()`; an
    off-the-record profile never gets a quick window.

## Hooks

One new patch, `chrome/browser/ui/startup/startup_browser_creator_impl.cc.patch` (+20 lines, three hunks: includes, a
call at the start of `OpenTabsInBrowser()`, a `ScopedExternalLinks` in `DetermineURLsAndLaunch()`). No extension point
sees startup tabs before they are opened, and the tab strip observer (#81's approach) only sees them after the main
window has been raised and changed. `//koan/browser/ui/views` is already a dep of `startup:impl`
(`chrome/browser/ui/startup/BUILD.gn.patch`). Upstream behaviour it relies on (REBASE row): both paths still reach
`OpenTabsInBrowser()`; `GetBrowserWindowForDisposition()` still sends a popup's new tabs to `FindTabbedBrowser()`;
`omit_from_session_restore` still keeps a popup out of the session; `DetachWebContentsAtForInsertion()` still moves a
popup's page without a reload.

## Tests

- Unit: `KoanQuickWindowRulesTest.*` (the routing choice, the bounds, the menu order), `KoanShortcutTable.NewQuickWindowKey`.
- Browser: `KoanQuickWindowBrowserTest.*` (through `ProcessCommandLineAlreadyRunning()`, Linux's path): default
  unchanged; quick window opened, a popup out of session restore, the main window unchanged; a route wins; non-web
  links unchanged; Move to Space keeps the same WebContents and its JS state; the Space's profile; Esc closes.
- Mac: `scripts/mac/steps/quick_window.md` (QW1-QW12).
