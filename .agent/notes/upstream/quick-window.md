# Quick window for links from other apps (#472): design and upstream audit

Date: 2026-11-27. Chromium 158 (pristine `git show HEAD:`), Koan `develop` `d410395e`. Arc's Little Arc is the
reference (Arc is closed source: its behaviour as the issue lists it). Zen has no equivalent (two open requests,
zen-browser/desktop#5293 and #4633); Brave has none either.

## The look: one row drawn by Koan (2026-11-27, the review of the test run at 9b071008)

The first build was upstream's popup with Koan's buttons on top: a system-style title bar ("Quick link - Koan", an
icon, a ×) above upstream's address row, Glance's Move and Close drawn over the row's end, the page square and edge
to edge. Two closes 30 px apart, a title bar nothing else in Koan has, no card. The review asks for one row drawn by
Koan: the row is the drag area and holds the site (security chip, read-only address), Move to Space and Close; macOS
puts the traffic lights at its start and leaves Koan's Close out; Linux keeps Koan's Close as the only one; rounded
corners and Koan's colours from the existing tokens; still a popup-type window underneath.

### What upstream draws for a popup (Chromium 158, pristine)

| | Linux | macOS |
|---|---|---|
| Frame view | `BrowserFrameViewLinux` (an `OpaqueBrowserFrameView`; `BrowserFrameViewLinuxNative` on a GTK/Qt theme, which #526 turns off) | `BrowserFrameViewMac`; paints nothing for a popup (`OnPaint` returns early for non-normal, non-app windows) |
| Title bar | Drawn by the frame: `NonClientTopHeight()` adds a title row when `ShouldShowWindowTitle()`, which asks `BrowserView::ShouldShowWindowTitle()` = `WindowFeatureController::kFeatureTitleBar`, true for `TYPE_POPUP`. Icon (`TabIconView`) when `WidgetDelegate::ShouldShowWindowIcon()` (`BrowserView` sets it from `::ShouldShowWindowIcon()`, true for popups); title `views::Label` | AppKit's: `BrowserNativeWidgetMac::PopulateCreateWindowParams()` gives a popup `WindowClass::kDefault`, a standard titled window (no full-size content view), so a title bar strip with the lights and the title above the content; the title text shows while `WidgetDelegate::ShouldShowWindowTitle()` (`-[NativeWidgetMacNSWindow _isTitleHidden]`) |
| Caption buttons | min / max / close, laid out in that title row (`OpaqueBrowserFrameViewLayout::LayoutWindowControls()`, shown while `ShouldShowCaptionButtons()` = `GetShowWindowTitleBar()`); `IsFrameCondensed()` (no border, no resize edges) is also `!ShouldShowCaptionButtons()` | the traffic lights in AppKit's title bar; `GetCaptionButtonBounds()` is empty for popups ("system-drawn") |
| Corners | `GetRestoredClipRegion()` / `GetWindowRoundedCorners()`: top corners only, `kHigh` radius, as for every Chrome window on Linux | the system's |
| Top row | `ToolbarView` in `DisplayMode::kLocation`: a `FlexLayout` of the read-only `LocationBarView` (popup mode: solid `kColorLocationBarBackground`, no rounding) and the pinned actions (downloads); background `kColorLocationBarBackground`. `BrowserViewPopupLayoutImpl` puts it below the caption-button exclusion, then the contents separator, then the page edge to edge | same views, below AppKit's title bar |
| Drag | the title row (frame `HTCAPTION`); `BrowserView::NonClientHitTest()` makes everything from the toolbar down client area | AppKit's title bar |

### What Koan's frame code does, and what is off outside normal windows

Every per-window controller exists for a popup but checks `GetIsNormalType()` (or `UseContentCard()`, which does):
the content card (`UseContentCard()`: normal window with the sidebar), compact mode and its caption-button hiding and
top-edge reveal (`KoanCompactModeController`, gated on `UseContentCard()`), the single toolbar's sidebar row beside the
caption buttons, `KeepsTopContainerInFullscreen()` / `KeepsLayoutInFullscreen()`, the traffic lights' row height and
mono lights (`OwnsTitlebar()`: normal window with the sidebar), glass (`GlassFrameService` eligibility), Space themes.
The colour mixer applies to every window (`kColorToolbar` = `kColorFrameActive`, the sidebar's colour).

### Options

| | What | Patches | Gives |
|---|---|---|---|
| A. The review's fallback | Keep upstream's row; no title (hook in `BrowserView::ShouldShowWindowTitle()`) and no icon (`SetShowIcon(false)`, no patch); on Linux none of the frame's caption buttons (`OpaqueBrowserFrameView::ShouldShowCaptionButtons()`, and `IsFrameCondensed()` kept on the title bar so the border and resize edges stay); the row as the caption (a hook in `BrowserView::NonClientHitTest()`, which otherwise makes it client area, so without it nothing drags the window) | +14 / −2 upstream lines in 3 existing patches (`browser_view.cc` 2 hunks, `opaque_browser_frame_view.cc` 3, `frame/BUILD.gn` 1 dep) | Linux: one row, one close. macOS: still AppKit's title bar strip with the lights above the row: two rows. Square page, upstream's row colour |
| **B. Chosen: A + Koan's look from existing hooks** | A, plus: macOS gets a transparent title bar over full-size content as a normal window (`PopulateCreateWindowParams()`), the lights centred in the row (the existing `GetTrafficLightsTitlebarHeight()` hook) and room for them (the row's leading inset); the row in `kColorToolbar` (`ToolbarView::SetBackground()`, public, and the existing `AdjustLocationBarBackground()` hook for the address); the row's trailing inset so the address ends where the buttons start (`FlexLayout` keeps to its host's insets); the page as the content card (the existing `multi_contents_view.cc` hook asks `DrawsContentCard()` instead of `UseContentCard()`, so the rest of the normal-window layout stays off) | **+20 / −5 upstream lines in 5 existing patches**, no new patch file: A's, +4 / −1 in `browser_native_widget_mac.mm` (1 include, 1 condition), 2 lines changed in `multi_contents_view.cc` | One row on both platforms, the lights in it on macOS, Koan's colours, the page inset and rounded like the main window's. Koan code: ~150 lines in the controller, hooks and content card |
| C. Koan's own frame for quick windows | A frame view class of Koan's for popups (factory hooks in `browser_frame_view_factory_views.cc` and `_mac.mm`), drawing border, shadow, CSD resize insets, corners and the row | 2 new patches, plus a frame implementation re-doing `OpaqueBrowserFrameView` / `BrowserFrameViewLinux` (hundreds of lines) | Full control (e.g. rounded bottom corners on Linux), at the cost of a second frame to keep up at every rebase |
| D. Make Koan's normal-window frame serve the popup | Turn `UseContentCard()` / `OwnsTitlebar()` on for quick windows so compact mode's caption-button hiding, the sidebar row and the lights' row run there | small patches, but `UseContentCard()` also switches on compact mode, the fullscreen layout, Glance's and the loading bar's card geometry for that window | Pulls the sidebar and compact mode machinery into a window that has neither; each of those would need an exception |

**Chosen: B.** It is A (which the review names as acceptable) plus six changed lines of patch, and everything else comes
from extension points Koan already has: it is the smallest option that gives the single row on macOS too, and it
reuses Koan's content card, colour tokens and traffic-light hook instead of a second frame.

What B does not do, and why:
- **Linux: the frame's buttons stay hidden, not "hidden until the pointer is at the top edge".** The main window
  reveals them there because they are its only window controls; in a quick window they would come back as a second ×
  next to Koan's Close, which is what the review asked to remove. Minimize and maximize: double-click the row
  (maximize), the desktop's keys. Revealing min / max only would need the frame's caption layout per window (option C).
- **Linux: the window's bottom corners are square**, as the main window's: upstream's Linux frame rounds the top two
  only (`GetRestoredClipRegion()`). The page card inside is rounded on all four. Rounding the window's bottom corners is
  a frame change for every Koan window on Linux (a separate issue), not a quick-window one.
- **The address can't be selected or copied with the mouse**: it is the drag area (Arc's Little Arc's title is too).
  The page's own context menu, Move to Space or the share menu after the move remain.

## Where links from other apps arrive (checked in the source)

The issue's pointer ("external links arrive in `application:openURLs:`") is the Mac half only. Both platforms meet in
**`StartupBrowserCreatorImpl::OpenTabsInBrowser()`** (`chrome/browser/ui/startup/startup_browser_creator_impl.cc`):

| Platform | Path |
|---|---|
| macOS | `application:openURLs:`, Handoff (`continueUserActivity:`) and the links held until startup ends (`openStartupUrls`) → `OpenStartupTabsInBrowser()` → `OpenStartupTabsInBrowserWithProfile()` (`app_controller_mac.mm`), which picks `FindTabbedBrowser()` (creating and showing an empty window if there is none) and calls `launch.OpenTabsInBrowser(browser, kNo, tabs, kNo)` inside Koan's `ScopedExternalLinks` (#81). |
| Linux | `koan <url>`, `flatpak run dev.koan.Koan <url>` or `xdg-open <url>` while Koan runs: the new process hands its command line to the running one through the **process singleton** → `StartupBrowserCreator::ProcessCommandLineAlreadyRunning()` → `ProcessCmdLineImpl()` → `LaunchBrowserForLastProfiles()` → `LaunchBrowser()` → `StartupBrowserCreatorImpl::Launch()` → `DetermineURLsAndLaunch()` (`LaunchResult::kWithGivenUrls`) → `RestoreOrCreateBrowser()` (`USE_EXISTING`: `GetExistingBrowserForOpenBehavior()`, the most recently *active* window on the current workspace, which can be a popup) → `OpenTabsInBrowser()`. Before this PR nothing marked these as external links, so #81's "Default route for external links" did not apply on Linux. |
| Linux, cold start | Same `DetermineURLsAndLaunch()`, but a session restore (`SYNCHRONOUS_RESTORE`) opens the links itself (`SessionRestore::RestoreSession(..., tabs)`), not through `OpenTabsInBrowser()`. |

So the hook is at the start of `OpenTabsInBrowser()`. Linux marks its links as external with #500's
`ScopedCommandLineLinks` (develop, the same `ScopedExternalLinks`) around
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
7. **Top row** (changed after the review of 9b071008, see "The look" above): one row and no title bar. Upstream's
   popup row (read-only address with the security chip) in the window's colour (`kColorToolbar`), the window's
   caption (drag, double-click), and at its end two of Zen's Glance buttons (same colours: `kColorKoanGlanceButton*`;
   28 px instead of 32 to fit the row): **Move to Space** (Glance's Expand icon: the page becomes a tab) and **Close**.
   The address ends where the buttons start, so they cover neither a long address nor its page actions. On macOS the
   traffic lights sit at the row's start and Koan's Close is left out (the red light closes); on Linux the frame shows
   none of its own buttons and Koan's Close is the only one. The page below is the content card (8 px inset, the
   card's radius).
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

One new patch, `chrome/browser/ui/startup/startup_browser_creator_impl.cc.patch`: on top of #500's hunks (the include
of `koan_spaces.h` and `ScopedCommandLineLinks` in `DetermineURLsAndLaunch()`), +12 lines in two hunks: the include of
`koan_quick_window.h` and the call at the start of `OpenTabsInBrowser()`. No extension point
sees startup tabs before they are opened, and the tab strip observer (#81's approach) only sees them after the main
window has been raised and changed. `//koan/browser/ui/views` is already a dep of `startup:impl`
(`chrome/browser/ui/startup/BUILD.gn.patch`). Upstream behaviour it relies on (REBASE row): both paths still reach
`OpenTabsInBrowser()`; `GetBrowserWindowForDisposition()` still sends a popup's new tabs to `FindTabbedBrowser()`;
`omit_from_session_restore` still keeps a popup out of the session; `DetachWebContentsAtForInsertion()` still moves a
popup's page without a reload.

The look (option B above) grows five existing patches, all calls into `koan_browser_view_hooks.h`:
`browser_view.cc` (`ShouldShowWindowTitle()`: `koan::OwnsWindowTopRow()`; `NonClientHitTest()`:
`koan::IsWindowCaptionAt()` after the welcome's check), `opaque_browser_frame_view.cc` (`ShouldShowCaptionButtons()`
and `IsFrameCondensed()`), `frame/BUILD.gn` (`:opaque_browser_frame_view_impl` → `//koan/browser/ui/views`),
`browser_native_widget_mac.mm` (`PopulateCreateWindowParams()`), `multi_contents_view.cc`
(`koan::DrawsContentCard()`). Upstream behaviour it relies on: the popup toolbar is still a `FlexLayout` that keeps to
its host's insets and sets its background only in `Init()`; the Linux frame still adds a title row only when
`ShouldShowWindowTitle()`, and lays out caption buttons only when `ShouldShowCaptionButtons()`; a popup's caption
buttons are still the only exclusion `BrowserViewPopupLayoutImpl` puts the toolbar below; macOS still hides the title
text when `ShouldShowWindowTitle()` is false and centres the lights in `GetWindowFrameTitlebarHeight()` for
`WindowClass::kBrowser`.

## Tests

- Unit: `KoanQuickWindowRulesTest.*` (the routing choice, the bounds, the menu order), `KoanShortcutTable.NewQuickWindowKey`.
- Browser: `KoanQuickWindowBrowserTest.*` (through `ProcessCommandLineAlreadyRunning()`, Linux's path): default
  unchanged; quick window opened, a popup out of session restore, the main window unchanged; a route wins; non-web
  links unchanged; Move to Space keeps the same WebContents and its JS state; the Space's profile; Esc closes;
  `OneRowDrawnByKoan` (no title or icon, no caption buttons on Linux, one or two buttons, the address ending at them,
  the row's colour, the address a caption and the chip and page not, the content card).
- Mac: `scripts/mac/steps/quick_window.md` (QW1-QW12; QW2 and QW2b for the row, the lights, drag and resize).
- Linux, by hand on the build: the PR's test comments (frames in dark and light, short and long address, drag and
  resize).
