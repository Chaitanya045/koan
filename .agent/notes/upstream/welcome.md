# Upstream audit: the welcome screen (chrome://koan-welcome)

Scope: Zen's welcome ported as a WebUI (#44, week 2). What Koan's welcome sits on in Chromium 158, what each upstream
piece does on the Mac, and the decision for each (**lock**, **override**, **redirect**, **reuse**). Builds on
[themes-glass-ntp.md](themes-glass-ntp.md) §3 (first run, the search choice, startup prompts) and
[zen-firefox-overrides.md](zen-firefox-overrides.md) (Z4, Z9). The first-run lockdown itself (Chromium's intro window,
the startup infobars, the crash bubble) is PR #169's; this file only adds the welcome's own pieces. Pristine source:
`git -C ~/work/koan/chromium/src show HEAD:<path>`.

## 1. Map

| # | Upstream piece | What it does (Mac) | Decision | Koan |
|---|---|---|---|---|
| W1 | `first_run::IsChromeFirstRun()` (`chrome/browser/first_run/first_run.cc`) | sentinel absent, or `--force-first-run`; false with `--no-first-run` | **reuse**: the welcome shows only then, in the first normal window of a non-OTR profile, once per session | `KoanWelcome::MaybeShow` |
| W2 | DICE first-run window (`FirstRunService::OpenFirstRunIfNeeded`) | Chromium's sign-in intro before any window | **lock** (#169: `kFirstRunFinished` registered true) | #169 |
| W3 | `koan.welcome_screen.seen` | Koan's `zen.welcome-screen.seen` | **reuse**: set once the screen is on screen, before any choice (a crash mid-welcome doesn't bring it back, as in Zen) | done |
| W4 | Window bounds: `Widget::SetBounds`, `Restore()` | | **override**: Zen's `min(1200, avail) x min(720, avail)`, centred on the window's display; a maximized or fullscreen first window is restored first | `KoanWelcome::Show` |
| W5 | Window resizing and zoom: `WidgetDelegate::SetCanResize`, `SetCanMaximize` (`NativeWidgetMac::OnSizeConstraintsChanged` → the NSWindow's style mask and min/max size) | | **override** while the welcome shows (Zen's `lockWindowSize`: min = max = the size), then back to what they were. Not restored on a window that is being destroyed | `LockWindow` |
| W6 | Native fullscreen (⌃⌘F, the View menu) | | **reuse**: still allowed; the screen covers the fullscreen window (Zen's lock doesn't stop it either). *Mac test* | — |
| W7 | Traffic lights (`standardWindowButton`) | | **override**: hidden while it shows (Zen shows none), back after; not touched on teardown | `koan_welcome_mac.mm` |
| W8 | Saved window placement (`kBrowserWindowPlacement`) | the size is saved at close | **reuse**: the 1200 x 720 window is the next launch's size, as in Zen ("keeps that size after") | — |
| W9 | Window drag | | **override**: a press outside the card hands the window to AppKit (`performWindowDragWithEvent:`, #162's `StartNativeWindowMove`), and the first move without the button sends the page's view its release (`EndNativeWindowMove`). No `RunMoveLoop` on the Mac (its release CHECKs) | `StartWindowDrag` |
| W10 | `chrome://koan-welcome` typed into a tab, a bookmark, `chrome.tabs.create` | an ordinary WebUI tab | **lock**: without a `KoanWelcomeHost` the page gets no data, shows nothing and every message changes nothing | `KoanWelcomeHandler` |
| W11 | Unregistered `chrome.send` (`WebUIImpl::ProcessWebUIMessage`) | `DUMP_WILL_BE_NOTREACHED` (a DCHECK) | **lock**: every message the page sends is registered | handler |
| W12 | Page context menu (`RenderViewContextMenu`: Back, Reload, Save as, Print, Inspect) | | **lock**: the page cancels `contextmenu` (Zen's welcome is browser chrome) | `koan_welcome.js` |
| W13 | The WebView's first frame | white until the page paints | **override**: the WebView and the page's base background are the intro's base green | `kWelcomeBaseColor` |
| W14 | Search engine choice screen (EEA; `SearchEngineChoiceService`) | a modal after first run in choice regions | **redirect**: Next on the welcome's search page records the choice (`ChoiceMadeLocation::kChoiceScreen`), once and never over an earlier record (`RecordChoiceMade` CHECKs a second choice-screen record, `NotFatalUntil::M161`). A pick sets the engine at once with `kOther`, as Zen does | handler (N10, O10) |
| W15 | A policy's default engine (`DefaultSearchProvider*`) | `CanDefaultSearchProviderBeModifiedByUser()` false | **lock**: no engines are listed, the page is skipped, and the handler refuses a pick (upstream `CHECK`s a `kOther` pick over a managed engine when the keyword data failed to load) | handler, `skip()` |
| W16 | `shell_integration::DefaultBrowserWorker::StartSetAsDefault` | macOS asks the user to confirm (LaunchServices) | **reuse**, on Next with YES only (Zen) | handler |
| W17 | `DefaultBrowserSettingEnabled` policy (local state) | the policy sets or forbids it | **lock**: the page is skipped when the pref is managed, and the handler refuses | handler, `skip()` |
| W18 | Default-browser / pin / PDF infobars on later launches | the nag | **lock** (#169, Z4): Zen asks once, in its welcome | #169 |
| W19 | `ImporterList::DetectSourceProfiles` (Safari, Firefox, a bookmarks file on the Mac) | the import dialog's list | **reuse** for the list; Chrome/Arc/Dia/Brave import is Z9, a later PR | handler |
| W20 | `IDC_IMPORT_SETTINGS` (chrome://settings/importData) | the import dialog in a settings tab | **redirect**: opened after the screen closes (over it, it would be hidden); Zen imports during the welcome with its own wizard. Nothing is imported without a browser picked plus Next | `Finish` |
| W21 | Import turns the bookmarks bar on | | **lock** (done: Koan ignores it, `kBookmarksBarShow`) | done |
| W22 | `ThemeService::SetBrowserColorScheme` | auto / light / dark | **reuse** (the picker's scheme buttons) | handler |
| W23 | The active Space's colour (Koan's Spaces) | | **reuse**; with #41/#62's picker the picker edits the Space itself | `space_color` |
| W24 | Pinned tabs (`chrome::AddTabAt(..., pinned)`) | the Essentials | **reuse**, at "Dive in!" only; they are saved with the session | `Finish` |
| W25 | Browser shortcuts and the menu bar while it shows (⌘T, ⌘L, ⌘N, ⌘W, ⌘Q, File/View items) | act on the window under the screen | **reuse** (maintainer, 2026-11-06): not blocked, as in Zen, whose welcome only hides the window's contents. *Mac test* W-14 records what each does | — |
| W26 | Incognito, guest, a second profile, a PWA window | | **lock**: OTR and non-normal windows never show it; a second profile in the same user data dir has no first run | `MaybeShow` |
| W27 | Closing the window or quitting during the welcome | | **reuse**: nothing applies but what was committed (the search engine, the default browser, the colours), as in Zen | — |
| W28 | Session restore after a crash in the welcome | #169 restores silently | **reuse**: the welcome doesn't come back (W3) | — |

## 2. What Zen suppresses in Firefox, for the welcome

| Firefox | Zen | Chromium equivalent | Koan |
|---|---|---|---|
| about:welcome, pre-onboarding, UITour | prefs off | DICE intro, IPH | #169 (W2), feature overrides |
| The default-browser prompt | asked once in the welcome; `skipDefaultBrowserCheckOnFirstRun` | startup infobars | #169 (W18) |
| The search engine choice | the welcome's page | the EEA choice screen | W14 |
| Its migration wizard's own entry points | the welcome opens it for one browser and profile | the import dialog | W20; preselecting the browser is Z9 |
| Lightweight themes | `zen.theme.disable-lightweight` | Web Store themes | outside the welcome (themes-glass-ntp §1) |

## 3. Tests

Mac steps: [scripts/mac/steps/welcome.md](../../../scripts/mac/steps/welcome.md). Each decision above has a step.
