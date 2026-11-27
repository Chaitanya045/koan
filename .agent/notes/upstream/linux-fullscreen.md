# Upstream audit: fullscreen on Linux (#518)

Date: 2026-11-27. Chromium 158 (pristine `git show HEAD:`), Koan `develop` d410395e, Zen 1.24b source plus
Zen 1.23.3b's Linux build run in a private headless GNOME (mutter) session. The macOS side is in
`tab-groups-omnibox-fullscreen.md` §3; this file covers what differs on Linux and what Koan decided.
Decisions: **lock** (never reachable), **override** (Koan's behaviour instead), **redirect** (the entry point runs
Koan's code), **reuse** (upstream as is).

## 1. How Chromium 158 does fullscreen on Linux

- **No immersive mode for normal windows.** `CreateImmersiveModeController()` (`immersive_mode_controller_factory_views.cc`)
  builds `ImmersiveModeControllerMac` on macOS and `ImmersiveModeControllerChromeos` on ChromeOS; every other platform
  gets `ImmersiveModeControllerStub` (never enabled). So nothing on Linux reveals or reparents the top container.
- **One decision hides the UI.** `BrowserWindowFullscreenController::ShouldHideUIForFullscreen()`
  (`chrome/browser/ui/fullscreen/browser_window_fullscreen_controller.cc`): false while immersive mode is enabled
  (macOS), otherwise `widget->IsFullscreen() && frame->ShouldHideTopUIInFullscreen()`. The base
  `BrowserFrameView::ShouldHideTopUIInFullscreen()` returns true; only `BrowserFrameViewMac` overrides it
  (`OpaqueBrowserFrameView` / `BrowserFrameViewLinux` don't). So on Linux it is true in every fullscreen.
- **Who asks it:**
  - `WindowFeatureController::IsFullscreen()` → `SupportsWindowFeature(kFeatureTabStrip / kFeatureToolbar /
    kFeatureLocationBar)` for normal windows: `BrowserView::ShouldDrawTabStrip()` (so `ShouldDrawVerticalTabStrip()`,
    `BrowserViewLayoutDelegateImpl::GetTabStripType()` → `kNone`), `BrowserView::IsToolbarVisible()`,
    `IsLocationBarVisible()`, and `BrowserCommandController::IsShowingMainUI()` / `IsShowingLocationBar()`
    (`UpdateCommandsForFullscreenMode()`: `IDC_FOCUS_LOCATION`, `IDC_FOCUS_TOOLBAR`, `IDC_FOCUS_BOOKMARKS`, ...).
  - `BookmarkBarController::ShouldShowBookmarkBar()` (no bar in fullscreen).
  - `BrowserCommandController::UpdateCommandsForHostedAppAvailability()` (toolbar focus, app menu).
  - `FullscreenControlHost::IsExitUiNeeded()`: the round exit button that drops from the top edge on Linux and
    Windows (`IsExitUiEnabled()` is false on macOS).
  - `ToastController` (no toasts in fullscreen).
  - (`BrowserView::Show()` asks the frame's `ShouldHideTopUIInFullscreen()` directly, to focus the location bar of a
    window shown in fullscreen; not the controller.)
- **Browser vs page fullscreen.** `FullscreenController` (`exclusive_access/fullscreen_controller.cc`):
  F11 / the ⋮ menu → `ToggleBrowserFullscreenMode()`; an extension's `windows.update({state:'fullscreen'})` →
  `ToggleBrowserFullscreenModeWithExtension()` (`IsExtensionFullscreenOrPending()`); a page's `requestFullscreen` →
  `EnterFullscreenModeForTab()` (`IsWindowFullscreenForTabOrPending()` from `SetTabWithExclusiveAccess()` on).
  Page fullscreen inside F11 doesn't change the window: `SetTabWithExclusiveAccess()`, then
  `ExclusiveAccessContext::UpdateUIForTabFullscreen()` (BrowserView: the frame's `UpdateFullscreenTopUI()`, a no-op
  off macOS, and `InvalidateLayout()`), then `PostFullscreenChangeNotification()`. Leaving it back to F11:
  `NotifyTabExclusiveAccessLost()`, `UpdateUIForTabFullscreen()`, the posted notification. Neither path calls
  `FullscreenTopUIStateChanged()` (the command controller and bookmarks bar update): on macOS
  `BrowserFrameViewMac::UpdateFullscreenTopUI()` does, on Linux nothing needed to (the UI was hidden either way).
- **Window state changes.** `BrowserView::ProcessFullscreen()` → `PrepareFullscreen()` (clears focus from the location
  bar, `FullscreenControlHost::OnEnterFullscreen()`), `RequestFullscreen()`, then synchronously off macOS
  `FullscreenStateChanged()` → `FullscreenController::WindowFullscreenStateChanged()` (posted notification, command
  controller, bookmarks bar), `ToolbarSizeChanged()`, the frame's `OnFullscreenStateChanged()`.
- **Layout.** `BrowserViewLayoutDelegateImpl::GetBrowserWindowState()` is `kFullscreen` (`kFullscreenWithToolbar` is
  macOS' "Always show toolbar" only). `BrowserViewTabbedLayoutImpl` lays out a vertical strip and a toolbar in
  `kFullscreen` when they are drawn (the macOS Koan layout uses the same state). The content-fullscreen check in
  `GetTabStripType()` is macOS-only; on Linux the strip is gone in page fullscreen because the UI is hidden.
- **Frame.** `OpaqueBrowserFrameView::GetShowWindowTitleBar()` is false in fullscreen: no caption buttons, no
  caption exclusions in `GetBrowserLayoutParams()`, and `OnPaint()` paints nothing (the client view covers it).
- **Exit bubble.** `ExclusiveAccessManager::GetExclusiveAccessExitBubbleType()`: browser fullscreen shows "To exit
  full screen, press and hold Esc" (`kPressAndHoldEscToExitBrowserFullscreen`, on); page fullscreen its own. Not
  tied to the UI decision.
- **Reserved keys.** `BrowserCommandController::IsReservedCommandOrKey()`: in any fullscreen only `IDC_EXIT` and
  `IDC_FULLSCREEN` are reserved off macOS (the macOS branch, with Koan's hook, reserves more while the toolbar shows).
- **Where the macOS hooks sit** (for reuse): `browser_view.cc.patch` (`OnImmersiveFullscreenEntered`,
  `GetWidgetForAnchoring`, `FullscreenStateChanged`, `IsBookmarkBarVisible`: all immersive or macOS-only paths, so
  they do nothing on Linux), `browser_frame_view_mac.mm.patch` (`UpdateFullscreenTopUI`, macOS only).

## 2. What Zen does on Linux

- Zen's Linux build ships Firefox's `browser.fullscreen.autohide` **true** (`defaults/preferences/firefox.js`, the
  non-macOS branch; the Mac's is false). Autohide collapses only toolbars that aren't `fullscreentoolbar="true"`;
  Zen's `TabsToolbar` (the sidebar) and its sidebar icons are (`navigator-toolbox-inc-xhtml.patch`,
  `zen-sidebar-icons.inc.xhtml`), so the sidebar stays.
- `zen.view.borderless-fullscreen` (true) and `updateElementSeparation()` (`zenThemeModifier.js`) are
  platform-neutral: `inFullscreen` + compact mode + no split view → no separation.
- Compact mode's rules are gated on `inDOMFullscreen` only; DOM fullscreen collapses the toolbox
  (`zen-browser-ui.css`, `zen-tabs.css`).
- Seen in Zen 1.23.3b (Linux) in a headless GNOME session, F11: the sidebar stays and the page keeps its 8 px gutter;
  with compact mode the page is edge to edge, the left screen edge slides the sidebar in, and the top edge shows a
  thin strip with the window's buttons (Firefox's fullscreen window controls; `zen.view.hide-window-controls`).
  A page's fullscreen inside F11 shows only the page; the left edge reveals nothing, the top edge shows Firefox's
  "This document is now full screen" notice; Esc leaves fullscreen altogether (Firefox).

So Zen on Linux matches Zen on macOS for everything #518 covers; the only Linux-specific piece is the window-controls
strip at the top edge.

## 3. Decisions

| Upstream item | Decision | How |
|---|---|---|
| `ShouldHideUIForFullscreen()` true in F11 | **override** (Koan windows only) | Hook `koan::KeepsLayoutInFullscreen(browser)` in `browser_window_fullscreen_controller.cc` (4 lines + include; `fullscreen/BUILD.gn` dep): false on Linux for a normal window with Koan's vertical sidebar whose page isn't fullscreen. Asked of the browser (type, `VerticalTabStripStateController`, `FullscreenController`), never of Koan's views, which ask `ShouldDrawVerticalTabStrip()` themselves. Popups, apps and horizontal-tab windows keep upstream's fullscreen. Why not the frame's `ShouldHideTopUIInFullscreen()`: it is also asked by `BrowserView::Show()`, which would focus the location bar (open Koan's URL bar) in a window shown fullscreen |
| Page / extension fullscreen hides the UI | **reuse** | The hook returns false only without page fullscreen, so upstream hides the strip, toolbar, bookmarks bar and disables the toolbar commands for a video, inside F11 or from a window, as before. Same test as macOS' `fullscreen_utils::IsInContentFullscreen()` (that file is macOS-only) |
| `koan::IsContentFullscreen()` false off macOS | **override** | True on Linux with the same predicate, so Koan suspends its card, hot zones, edge monitor and URL bar as on macOS |
| Strip not drawn in page fullscreen on Linux (`ShouldDrawVerticalTabStrip()` false) while macOS keeps it drawn | **override** (Koan's predicate) | `UseContentCard()` on Linux also counts a window whose page is fullscreen and whose sidebar is vertical, so compact mode stays on and nothing slides when the video ends (macOS' state) |
| `UseContentCard()` "elsewhere edge to edge", `IsBorderlessFullscreen()`, `ContentCardInsets()` page-fullscreen branch, `KeepsTopContainerInFullscreen()`, `HidesFullscreenTopChrome()` macOS-only | **reuse** (Koan's macOS code, now macOS and Linux) | Linux takes the macOS branches; `HidesFullscreenTopChrome()` lets the URL bar open in F11 (`CanOpenBar()`); its other callers are macOS-only patch paths |
| No `FullscreenTopUIStateChanged()` when a page's fullscreen starts or ends inside F11 | **redirect** (no patch) | Koan's compact controller already listens to `FullscreenController::RegisterOnFullscreenStateChanged()` on macOS; now on Linux too, and there calls `FullscreenTopUIStateChanged()` while the window is fullscreen, so the bookmarks bar and the toolbar commands (Ctrl+L's `IDC_FOCUS_LOCATION`) follow |
| Compact hover state across F11 | **override** | The same notification clears hover states, updates the pointer monitor (the 2 px screen-edge reveals, `WantsFullscreenEdges()`, already platform-neutral) and collapses the caption strip on Linux (Zen's `sizemodechange`) |
| `FullscreenControlHost` exit button at the top edge | **reuse** | Gone in F11 in Koan windows (`IsExitUiNeeded()` asks the same decision): the UI is shown, and compact mode's top edge reveals the toolbar. Stays over a page's fullscreen |
| Kiosk mode (`--kiosk`, `IsRunningInAppMode()`): a fullscreen with no browser UI | **reuse** | `KeepsLayoutInFullscreen()` is false there, so upstream hides the UI as before |
| Exit bubble ("press and hold Esc") | **reuse** | Unchanged |
| Reserved keys in fullscreen (Ctrl+T, Ctrl+W, ...) | **reuse**, follow-up | Chromium's Linux rule (page first); Koan's macOS hook is in the macOS-only branch. Firefox reserves them always. Not in #518's scope |
| Zen's window-controls strip at the top edge in fullscreen | **missing**, follow-up | Chromium's frame draws no caption buttons in fullscreen; Koan's caption strip has nothing to reveal |
| Esc from a page's fullscreen inside F11 | **reuse** | Back to F11 (Chromium); Zen leaves fullscreen altogether (Firefox, all platforms) |

No user, old profile or extension can reach a hidden-UI F11 in a Koan window: the decision has no pref, and an
extension's fullscreen counts as a page's (UI hidden, as upstream and macOS).

## 4. Tests

`src/test/browser/koan_fullscreen_browsertest.cc` (koan_browsertests, Linux): F11 keeps the sidebar, card, URL bar
(Ctrl+L enabled and opening, Ctrl+T); compact mode borderless and the sidebar still revealed; a page's fullscreen
inside F11 hides the strip, toolbar, bookmarks bar, card and URL bar, disables Ctrl+L, keeps compact mode, and
leaving it restores all of that; a page's fullscreen from a window the same. The screen-edge reveal itself needs a
real pointer: the PR's Flatpak steps.
