# PR #66 review: media controller, toasts, download animation, welcome, window dragging (Fixes #44)

Branch `feat/zen-ui-pieces` at 83eec50, read-only review (nothing built or run). Paths are relative to
`src/browser/ui/views/` unless they start with `scripts/`, `docs/`, `patches/` or `src/`.
"Verified" = I read the code and the Chromium (67e29b82) or Zen source. Anything I could not confirm without running it is marked "unverified".

Result: 1 Blocker, 6 Should-fix, 9 Nits.

## Checked and fine (so nobody re-checks)
- **Lifetimes.** Controller members are destroyed in the right order (`koan_browser_view_controller.h`: media, download_animation, window_drag, welcome; the drag controller's `Unretained(download_animation_)` is safe because the animation is destroyed after it).
  - All new controllers are torn down in `DestroyFor` (`koan_browser_view_controller.cc:105-111`).
  - `KoanMediaController::RemoveCard` destroys the card after its view (`koan_media_controller.cc:1014-1027`).
  - `Dismiss` and `RemoveCardSoon` are deferred, so a card is never deleted inside its own button callback.
  - `AddMouseEventCallback` has a `CHECK(!contains)` (`render_widget_host_impl.cc:2049`). `WatchWidget` guards it with `host == widget_host_` and the host observation is reset on swap, so no double add.
- **Hook patches apply.** I fetched the pristine files at 67e29b82.
  - `BUILD.gn.patch` hunk `-33,6` lands on `//chrome/browser/ui/views/accessibility` through `//components/constrained_window`. Context matches.
  - `download_toolbar_ui_controller.cc.patch` hunks `-63,6` and `-503,6` match lines 63-68 and 503-508 of the original.
  - Both are minimal and have a row in `docs/REBASE.md`. The hook sits before the `ShouldRenderRichAnimation()` check (see S4).
- **No symlinks and no stray files** (`git ls-tree -r HEAD | grep '^120000'` is empty). The only non-code additions are `docs/screenshots/*` and `.agent/notes` text.
- **Idle cost (#47).** No repeating timers while idle.
  - The card clock runs only while expanded, playing and with a progress bar (`koan_media_controller.cc:487`).
  - The overlay widget exists only during an animation (`koan_download_animation.cc:432-446`).
  - The drag controller does nothing for moves in `kIdle`.
- **Shared hooks.** `KoanToast` is extended (not duplicated). There are no new tab menu items, session data or pin logic, so `TabMenuFeature`, `koan_session_data` and `IsPinnedForUser` are not needed here.
- **Prefs.** The new prefs are not syncable (`koan_prefs.cc`).
- **Mac guards.** The Linux-only window-move code and its deps are under `BUILDFLAG(IS_LINUX)` / `is_linux` (`koan_window_drag.cc:37-43, 303-325`; `BUILD.gn`).
  - `NativeWidgetMac::RunMoveLoop` and `IsMoveLoopSupported` exist in this tree.
  - A `TYPE_CONTROL` widget with `accept_events = false` becomes a child NSWindow that ignores the mouse (`native_widget_mac.mm:341`), so the overlays don't take clicks on Mac.
  - The e2e uses Ctrl (CDP), which is fine on the Linux bench; no new in-product accelerators were added.

## Blockers

### B1. Ports of Zen's MPL-2.0 code and strings carry only the MIT header; `THIRD_PARTY_NOTICES.md` is unchanged
- **Where.** All four new `.cc`/`.h` pairs have `// Copyright 2026 The Koan Authors ... MIT`. `THIRD_PARTY_NOTICES.md` is not in the PR (its only Zen text says Zen is "referenced only to describe the user experience"). `.cache/zen-src/README.md` says "do not copy code into Koan".
- **What is copied (verified against the Zen source):**
  - `koan_welcome.cc:73-95`: `kTitleLine1/2`, `kBack`, `kSkip`, `kYes`, `kNo` and all page titles and descriptions are verbatim from `localization/zen-welcome.ftl` (including "Dive in!" at `:971`).
  - `koan_welcome.cc:123-133`: the nine Essentials (URLs, order and hex colours) are the table at `ZenWelcome.mjs:21-31`.
  - `koan_welcome.cc:1054-1059`: Zen's Wikipedia and eBay exclusion.
  - `koan_window_drag.cc:63-117`: `kIsEmptySpaceScript` ports `ZenWindowDragChild.sys.mjs` check by check. The roles set (`:90-93`) is the same 20 roles in the same order as `kInteractiveRoles` (`ZenWindowDragChild.sys.mjs:31-52`). The 4/50 px thresholds and 5% come from the same file and `zen-default-prefs.js:111`.
  - `koan_download_animation.cc:56-66, 284-348`: the keyframe maths (60 steps, `easeInOutQuad`, scale 0.5 to 1.8 to 0.45, the opacity ramp, the 0.8 room limit, 1200 cap) follows `ZenDownloadAnimation.mjs:216-340`.
  - `koan_media_controller.cc:76-96, 884-960`: `FormatTime` ports `formatSecondsToTime`. The 900000 s live cut-off, 1 s check, 500 ms reveal, 3 cards and 2 peek levels mirror `ZenMediaController.mjs:14-21, 266-293, 520-527`.
- **Fix.**
  - Put the MPL-2.0 header (credit to the Zen Browser authors, with the "If a copy of the MPL..." notice and a link to the original file) on `koan_welcome.*`, `koan_window_drag.*`, `koan_download_animation.*` and `koan_media_controller.*`, next to the MIT line.
  - Add a Zen entry to `THIRD_PARTY_NOTICES.md` naming these files and the MPL-2.0 text/URL, and state that the MPL covers those files only. Change the "Zen is referenced only to describe the user experience" sentence.
  - Or decide on a clean-room rewrite, but the strings and tables above would still need rewording.

## Should-fix

### S1. Welcome screen: no keyboard path, no visible focus, no screen-reader feedback
- **`koan_welcome.cc` (whole file; `Show()` at `:1330-1345`).**
  - The screen is a `TYPE_CONTROL` child widget, which is not activatable (`widget.cc:245`). Nothing moves focus into it: there is no `RequestFocus` / initial focus, `OnKeyPressed` or accelerator anywhere in the file (grep). Focus stays on whatever is focused in the page behind it, so Tab, Enter and Space do not reach the buttons.
  - The custom `views::Button` subclasses (`KoanWelcomeButton :234`, `StartButton :276`, `BackButton :311`, `Option :349`, `Swatch :517`, `EssentialTile :626`) paint only a hover state. There is no `FocusRing`. On Mac their default focus behaviour is `ACCESSIBLE_ONLY` (`platform_style.h:90`) and the code never sets another.
  - `ShowPage` deletes every button on each page change (`:948`, `RemoveAllChildViews`), so even a focused button would lose focus on Next, and nothing announces the new page. The tagline's characters are all `SetIsIgnored(true)` (`:1120`), so a screen reader only gets the dialog name "Welcome to Koan".
  - All nine swatches are named "Color" with no checked state (`:523`). The Option "radio buttons" are not in a group (`:363`).
  - Escape does nothing.
- **Fix.**
  - Make the widget activatable (or give it focus another way) and focus the Start button, then the page's primary button after each page.
  - Add `views::FocusRing::Install` plus `SetFocusBehavior(ALWAYS)` to the custom buttons.
  - Handle Enter/Space/arrow keys for the options.
  - Announce each page title (`AnnounceAlert`, or re-focus the heading).
  - Give swatches distinct names ("Color 3 of 9") and a selected state.

### S2. Welcome covers the whole window with no way to move or close it
- **`koan_welcome.cc:1381-1386`**: `LayoutWidget` sets the overlay to `browser_view_->GetLocalBounds()` and `KoanWelcomeView` has no drag or caption area. While it shows (about 6 pages, and "Skip for now" exists only on Essentials, `:977`), Koan's own window controls on Linux are under it and the window can't be dragged by mouse.
- **Mac: unverified.** The overlay is a child NSWindow covering the same rectangle, so the traffic lights may be covered too. The PR says nothing ran on a Mac.
- **Fix.** Leave the caption/window-controls strip uncovered (or make the top strip a window-drag region), and let Esc skip the whole flow.

### S3. Media cards: keyboard and screen-reader gaps
- **`koan_media_controller.cc`.**
  - Collapsed, the title, pip, close and progress rows are laid out at negative y (`:542`). They are visible and keep focus-traversal order (on Linux `ALWAYS`), so Tab lands on an invisible Close or PiP button.
  - Expansion happens only on `OnMouseEntered` (`:727`), so keyboard users can never see the rows.
  - `KoanMediaProgressBar` (`:277-340`) has no accessible role, name or value and no key handling; it can only be dragged with a mouse.
  - The site icon button is named "Go to tab" for every card, with no site name (`:374`).
- **Fix.**
  - Expand the stack on focus-within.
  - Hide (`SetVisible(false)`) or disable the collapsed rows.
  - Make the progress bar a slider (`ax::mojom::Role::kSlider`, value text, Left/Right to seek).
  - Name the card or icon with the title or site.

### S4. Reduced motion: Koan's animations replace one that respected it
- **`patches/.../download_toolbar_ui_controller.cc.patch`** puts `koan::ShowsDownloadAnimation()` before Chrome's `gfx::Animation::ShouldRenderRichAnimation()` check (patch lines 14-16). `grep ShouldRenderRichAnimation|PrefersReducedMotion` finds nothing in the new code. A user with macOS "Reduce motion" (or Linux animations off) used to get no download animation and now gets the 1 s flying icon plus the tile slide (`koan_download_animation.cc:284-348`).
- The welcome typing, text sliding, media card fades and expand also ignore it.
- **Fix.** In `KoanDownloadAnimation::Play()` return early if `!gfx::Animation::ShouldRenderRichAnimation()`. For the welcome and media cards, skip or shorten the durations the same way. Consider moving the check into `ShowsDownloadAnimation()` so Chrome's check still runs when Koan won't play.

### S5. Welcome Essentials load nine sites at once; Zen creates lazy tabs
- **`koan_welcome.cc:1391`** `chrome::AddTabAt(..., /*foreground=*/false, ..., /*pinned=*/true)`. This starts a real load of every chosen site (up to nine, e.g. Discord, Slack, Figma) on first run, on a laptop's first minutes.
- Zen adds them with `createLazyBrowser: true`, writes the icon and history into `TabStateCache` and removes `pending` (`ZenWelcome.mjs:430-445`), so nothing loads until the tab is selected. Koan has restore-on-demand infrastructure (`kKoanRestoreTabsOnDemand`) for exactly this (#47).
- **Fix.** Add the tabs discarded or lazy (e.g. through the restore path with a navigation entry, as session restore does) and give them the favicon, so the Essentials tiles show icons without a network burst. Note that `InitPinnedState` (`koan_pinned_tabs.cc:500`) reads the visible URL, so the pinned URL still comes out right.

### S6. e2e coverage is thin where the PR's claims are strongest
- **`scripts/e2e/welcome.mjs:61-63`**: "pins the chosen Essentials" only checks that `obsidian.md` and `github.com` appear in `pages()`. It never checks that the tabs are pinned, so an unpinned tab would pass.
  - Also untested: Back, Skip for now, picking a search engine (and that the default changed), the colour actually changing the theme, import YES running, the seen pref surviving a restart (a second launch must not show it), and any keyboard use (see S1).
- **`scripts/e2e/window_drag.mjs`**: only checks what the page sees (headless has no window manager), so neither `StartDrag` branch (`handler` null, early return at `koan_window_drag.cc:315`) is exercised. The pref-off case, the maximized threshold, page zoom and form controls, ARIA roles and iframes are untested.
- **`scripts/e2e/media.mjs`**: single card only. No test of stacking (2 to 4 tabs), previous track, close, seek, a sound shorter than 1 s getting no card, the tab closing, or the sidebar collapsed to icons.
- **`scripts/e2e/download_animation.mjs:25-26`**: checks that Chrome's arrow is absent once at 100 ms only, and never covers the toolbar-visible path (button target).
- The workspace-rename toast has no test (the PR says so).
- **Fix.** Add the pinned check (`TabStripModel` pinned state through the views or a page query), a restart for the seen pref, and a second media tab.

## Nits

- **N1. Download tile can be missed by a download during its exit.** `ShowTile` returns when `tile_` exists and the hold timer is not running (`koan_download_animation.cc:351-357`). A download that starts while the tile is sliding out (after the 1.2 s hold) flies to a fading tile and never brings it back. Restart the show when the tile is hiding.
- **N2. Arc without a click.** `Play()` starts from the page centre when no press was seen (`:226-240`). Zen skips the animation when `_lastClickPosition` is unset (`ZenDownloadAnimation.mjs:61-67`).
- **N3. `KoanToast::RunAction` runs the action even if the toast is gone** (`koan_toast.cc:213-228`): the closure runs and only then checks the toast weak pointer. The background-tab action captures `Unretained(tab_strip_model)` (`koan_compact_mode_controller.cc:507`). Window teardown between the click and the posted task is very unlikely, but check the weak pointer first. `toast->Hide()` after the action can also hide a toast the action just showed.
- **N4. Toast action is unreachable by keyboard.** The toast is non-activatable and the announcement doesn't mention the "Switch to tab" button (`koan_toast.cc:213`, `ShowWithAction`). Zen has the same mouse-only button, but the screen-reader text could say "press Ctrl/Cmd+... to switch" or the announcement could name the action.
- **N5. Background-tab toast misses two Zen conditions.** `zen.view.compact.show-background-tab-toast` (`ZenCompactMode.mjs:142`, `zen-default-prefs.js:103`) has no Koan pref, and Zen skips the toast while a Glance animates (`ZenCompactMode.mjs:1035`).
- **N6. Rename toast also fires when the name is cleared** (`koan_workspaces.cc:481-482`: it requires only a non-empty old title and a different new one). Require the new title to be non-empty.
- **N7. Window drag details.**
  - `<img>` and other natively draggable elements are not excluded, and there is no `dragstart` reset, whereas Zen resets the gesture on `dragstart` (`ZenWindowDragChild.sys.mjs:89-91`) and ignores a `defaultPrevented` mousedown (`:123`).
  - The 50 px "maximized" threshold is Windows/Linux snap behaviour; macOS zoomed windows just move.
  - The Mac branch (`#else`, `koan_window_drag.cc:325-352`) was never compiled by the author (Linux only; also `#else` covers other OSes).
- **N8. Media seek differs from Zen.** Zen pauses on seek drag input (`ZenMediaController.mjs onSeekDrag`); Koan doesn't pause while dragging. The controller is built for every window even without a vertical strip, so audible tabs still create a `MediaSession` observer for no UI (`koan_media_controller.cc:902-916`).
- **N9. Housekeeping.**
  - The class-name scan for the vertical strip's `FlexLayoutView` now exists three times (`koan_media_controller.cc:1073`, `koan_workspaces.cc:340`, `koan_compact_mode_controller.cc:944`).
  - `scripts/e2e/download_animation.mjs` downloads into the real `~/Downloads` (no `HOME` override in `koan_test_env.sh` / `run.sh`). Add `scripts/e2e/prefs/download_animation.json` with `savefile.default_directory` and `prompt_for_download` instead.
  - New `BUILD.gn` deps and includes are not in sorted order, and the media card expand animation relayouts the whole sidebar column every frame via `PreferredSizeChanged()` (`koan_media_controller.cc:735`, 300 ms on hover only).
  - The welcome doc says "once per profile", but `first_run::IsChromeFirstRun()` is once per user-data directory, so a second profile in the same directory never sees it.

## Parity notes (no action needed unless you want closer)
- Media: show/hide timing, 3-card stack, mute keeping the card and hiding while in PiP match `ZenMediaController.mjs`. Zen also hides for fullscreen video (`isBeingUsedInPIPModeOrFullscreen`), and Koan only for PiP; the fullscreen tab is the active tab anyway, so the card is hidden by the selected-tab rule.
- Window drag: threshold, 5% region, isolated-world check and iframe/scrollbar handling match; the click-after-drag difference is documented in the PR.
- Welcome: the flow and strings match Zen; the ad-blocker page is omitted and documented in `docs/ZEN_PARITY.md`.

## Merge recommendation
Do not merge until B1 is done. S1-S3 are a11y gaps on a first-run screen and a new sidebar widget; S4 is a regression for reduced-motion users. S5 and S6 can be follow-ups if tracked.
