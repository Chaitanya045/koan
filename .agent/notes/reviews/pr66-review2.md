# PR #66 review, round 2: media controller, toasts, download animation, welcome, window dragging (Fixes #44)

Branch `feat/zen-ui-pieces` at d20c3b2, rebased on develop f2d50aa (#57, #85, #88); the merge-base equals develop's tip. CI checks (patches apply, unit tests, lint) are green. Read-only review: nothing built or run, and nothing in this PR has run on a Mac.
Paths are relative to `src/browser/ui/views/` unless they start with `scripts/`, `docs/`, `patches/` or are a top-level file. "Verified" means I read the code, plus the Chromium source (`~/work/koan/chromium/src`) or the Zen source where noted.

Result: 0 Blockers, 1 Should-fix, 8 Nits. All round-1 items are fixed except the ones the author deferred to #68.

## 1. Round-1 status

| Item | Status | Evidence |
|---|---|---|
| B1 licensing | FIXED (with a nit, N3) | see below |
| S1 welcome keyboard/a11y | FIXED in code, but no test exercises it | see below |
| S2 welcome overlay, drag/close area, Mac traffic lights | FIXED on Linux; Mac is reasoned only | see below |
| S3 media-card keyboard, slider role | FIXED | see below |
| S4 reduced motion | FIXED for every new animation (nit N2 for the toasts the PR reuses) | see below |
| S5 lazy Essentials | NOT FIXED, deferred to #68 (open, lists it) | `koan_welcome.cc:1508` still `chrome::AddTabAt(... foreground=false ... pinned=true)` |
| S6 e2e coverage | NOT FIXED, deferred to #68 (listed in detail) | `scripts/e2e/welcome.mjs` has no keyboard step, no Back/Skip, no restart |
| N1 tile missed during exit | FIXED | `koan_download_animation.cc:316-331` (`tile_leaving_` brings it back; the aborted exit is counted out by `OnAnimationDone`, `running_` stays balanced) |
| N2 arc without a click | FIXED | `koan_download_animation.cc:221-223` |
| N3 `RunAction` order | FIXED (small leftover, N1 below) | `koan_toast.cc:216-231`: the weak pointer is checked, then `Hide()`, then the action |
| N4 toast action by keyboard | FIXED as scoped | `koan_toast.cc:200-213`: the announcement is `text + " " + action_name`; the widget stays non-activatable |
| N5 background-tab toast conditions | NOT FIXED, deferred to #68 | `koan_compact_mode_controller.cc:476-512` |
| N6 rename toast when cleared | FIXED | `koan_workspaces.cc:645-656`: `trimmed.empty() \|\| trimmed == data->name` returns before the toast |
| N7 window drag details | PARTIAL, as stated | `<img>` and `[draggable]` are in the interactive list (`koan_zen_window_drag.cc` script); Mac keeps the 4 px threshold (`koan_window_drag.cc:172-175`). `dragstart`/`defaultPrevented`, the uncompiled Mac branch and the `#else` scope are in #68 |
| N8 seek pause, observer in strip-less windows | NOT FIXED, deferred to #68 | `koan_media_controller.cc:893-897` |
| N9 housekeeping | PARTIAL, as stated | unique download file removed afterwards (`scripts/e2e/download_animation.mjs:14-17, 41`); header and docs say "user data directory" (`koan_welcome.h:44-45`), but see N5. Sorted deps, the `FlexLayoutView` lookup (now 3 copies) and the per-frame relayout are in #68 |

### B1: every file that ports Zen material has the header and a row. FIXED
- The eight `koan_zen_*` files each start with the MPL-2.0 header from CONTRIBUTING.md:206-216, word for word, listing the Zen sources: `koan_zen_welcome.h`, `koan_zen_window_drag.{h,cc}`, `koan_zen_download_animation.{h,cc}`, `koan_zen_media.{h,cc}`, `koan_zen_toasts.h` (lines 1-9 of each).
- `THIRD_PARTY_NOTICES.md:36-40` has one row per file, and the Zen section (`:13-22`) says the MPL covers those files as a whole.
- The ported material really did move. In the MIT files (`koan_welcome.cc`, `koan_window_drag.cc`, `koan_download_animation.cc`, `koan_media_controller.cc`, `koan_toast.cc`) no ftl string, Essentials table, interactive-role list, keyframe maths or `formatSecondsToTime` remain (I grepped the string literals and compared with the Zen source). The files that already carried MPL headers and got new Zen material (`koan_color_mixer.cc`, `koan_sidebar_metrics.h`, `koan_settings_rows.cc`) are in the table.
- The two toast strings the review missed are now in `koan_zen_toasts.h`.
- Leftovers are only nits (N3).

### S1: welcome, keyboard and screen reader. FIXED in code (no test)
- Focus goes to Start when the screen opens (`koan_welcome.cc:1428`, `:1199`) and to each page's primary button (`:1040`). I checked in the Chromium source that this reaches the overlay: `View::Focus()` clears native focus (`ui/views/view.cc:2882-2890`), and a child widget uses the top-level's focus manager, so keys are dispatched to the focused view in the overlay.
- Esc is an accelerator on the overlay (`:1170`, `:1291-1294`). `View::CanHandleAccelerators` accepts a child widget when focus is inside it (`view.cc:1985-1988`), so it works while focus is in the screen.
- All custom buttons are `ALWAYS` focusable with a round-rect highlight path (`:168-172`); `Button` installs its focus ring itself (`button.cc:675`).
- The options are a named radio group with position and set size, a checked state and arrow keys (`:334, 341, 353-366, 461-485`). The swatches are named (`:88-96, 552`) and have a checked state (`:560`). Essential tiles are toggle buttons (`:676-683`). The dialog is named and modal (`:1165-1168`). Each page is announced (`:1042`).
- Start and Back hide only after the new button has focus (`:1346-1353`, `:1040-1041`).
- Gap: nothing tests it (S6, #68). The e2e drives the mouse through UI DevTools, and the only focus check is `active !== 'A'` (`scripts/e2e/welcome.mjs:34-36`). See N4 for the swatches.

### S2: overlay vs window controls. FIXED on Linux, Mac reasoned
- `LayoutWidget` (`koan_welcome.cc:1483-1503`) starts the overlay below `ContentCardInsets().top`, below a visible top container, and on Mac below `kWelcomeMacTitleBarHeight` (38, `koan_sidebar_metrics.h:216`). Esc skips the rest (`:1469-1474`).
- The e2e checks the overlay is shorter than the frame (`scripts/e2e/welcome.mjs:21-24`).
- Not verified: the Mac value (the PR says so too), and whether the overlay is re-laid out when only the top container moves without a BrowserView resize (compact-mode reveal); `OnContentCardChanged` covers the card inset only (`koan_compact_mode_controller.cc:819-831`).

### S3: media cards. FIXED
- Collapsed cards hide the title, artist, close, picture-in-picture, time labels and progress with `SetVisible(expanded_)` (`koan_media_controller.cc:504-516`), so Tab cannot land on them.
- The stack expands on focus-within through a `FocusChangeListener` (`:786-792, 836-854`), added and removed with the widget (`:777-784`).
- The progress bar is `Role::kSlider` with a name, a value text and a focus ring (`:263-267`); `SetValue` sets "0:07 of 0:30" (`:271-276, 635-637`); Left/Right/Up/Down seek 5 s and Home/End jump (`:327-354`). It takes focus only when seekable (`:280-284`).
- The card is a group named "title, artist" (`:387-388, 487-490`) and the icon button "Go to <title>" (`:479-481`).

### S4: reduced motion. FIXED for every new animation
| Animation | Gate |
|---|---|
| Chrome's arrow vs Koan's | `koan_browser_view_hooks.cc:28-34`: `ShowsDownloadAnimation` also needs `ShouldRenderRichAnimation()`, so with reduced motion Chrome's own check runs, as before |
| Arc and tile | `koan_download_animation.cc:170-173` (the only caller of `Play()`); see N6 on the PR reply's wording |
| Welcome: intro, exit, text slide/fade, pages fade, finish | `koan_welcome.cc:161-163`, `:184-188`, `:1202-1210`, `:1307-1310`, `:1443-1449` |
| Media card fade-in and expansion | `koan_media_controller.cc:706`, `:846-848` (snaps with `expand_.Reset`) |

The toast show/hide animation is Koan's older one and is not gated (N2).

## 2. Fresh correctness pass

### Checked and fine
- **Member order and teardown.** `media_`, `download_animation_`, `window_drag_`, `welcome_` are declared in that order (`koan_browser_view_controller.h:103-106`), so they are destroyed in reverse and the drag controller's `Unretained(download_animation_)` is safe (`.cc:66-70`). `DestroyFor` tears them all down (`.cc:108-111`) after `toast_->Close()`.
- **Download animation.**
  - All completion callbacks only post a task through the weak pointer (`koan_download_animation.cc:415-430`), so the overlay is never deleted inside its own animation.
  - A window close during an arc or the tile: `OnBrowserViewDestroying` resets the observation, the pre-target handler, the timer, `tile_` and the widget (`:156-164`). The aborted sequences' `OnAnimationDone` finds a dead weak pointer or an empty `overlay_`.
  - `running_` stays balanced in every path I traced: arc, tile in, hold, tile out, a download during the exit, two downloads close together.
  - The tile timer is a member and stops with the object.
- **Media controller.**
  - `RemoveCard` moves the card out, calls `UpdateView()` (which deletes its view), then lets the card die (`koan_media_controller.cc:1092-1102`).
  - Card buttons that delete a card (`Dismiss`, the session going inactive) post the removal (`:185-191, 1076-1090`).
  - Clicking a card calls `FocusTab`, which activates the tab, which hides that card and deletes its view inside the button callback (`:149, 1124-1143`, then `OnActiveTabChanged`, then `UpdateView`). That is supported: `ButtonController` says "We may be deleted at this point (by the listener's notification handler)" (`ui/views/controls/button/button_controller.cc:38, 53`), and the card object itself outlives its view.
  - `OnBrowserViewDestroying` removes the model observer, the timer, the sidebar observation and the view before clearing `cards_` and `watchers_` (`:915-925`). Tab removal and replacement drop the card and the watcher (`:952-966`), including a tab moving to another window.
  - The one-second check keeps the raw `WebContents*` only for comparison (`:1004-1019`).
  - No timers run while idle: the position timer exists only when expanded, playing and with a progress bar (`:522-529`).
- **Focus listeners.** `KoanMediaControlsView` adds and removes its focus listener in `AddedToWidget`/`RemovedFromWidget`/dtor (`:686, 777-784`); removing a focused card view just clears focus and collapses the stack.
- **Window drag.**
  - The mouse callback is added once per host and removed on swap, destroy and `OnBrowserViewDestroying` (`koan_window_drag.cc:100-114`; the `CHECK(!contains)` concern from round 1 still holds).
  - The posted release and the Mac move loop both go through the weak pointer and re-read the widget (`:219-232, 268-284`).
- **Toasts.** The action closure holds a `WeakPtr<TabInterface>` and `Unretained(tab_strip_model)` (`koan_compact_mode_controller.cc:494-507`), but it only runs if the `KoanToast` weak pointer is alive (`koan_toast.cc:221-231`), and the toast dies with the controller, so a window closed between click and task cannot reach the dangling model. `Close()` drops a pending action (`:330`).
- **Welcome and window close.**
  - Closing the window during the welcome calls `Close()` from `OnBrowserViewDestroying` (`koan_welcome.cc:1395-1397, 1476-1481`).
  - All delayed paths (`MaybeShow`, `Finish`, `Skip`, `ShowPagesSoon`, `PostShowPage`) go through weak pointers, so a window closed before `MaybeShow` runs or mid-fade is safe; `OnEnded` is not called for aborted sequences.
  - `Skip()` and `Finish()` defer the close past the key/click handler.
- **#57 integration.**
  - `KoanWorkspaceController::RenameSpace` is called only from the Space header's posted `FinishRename` (`koan_space_header.cc:178-183`); it ignores an empty or unchanged name and then toasts (`koan_workspaces.cc:641-657`). `scripts/e2e/spaces.mjs` now checks the toast text.
  - `FocusTab` calls `SwitchTo()` only when `IsEnabled()` and the tab is in a group (Essentials have none), then re-reads the index and activates (`koan_media_controller.cc:1130-1143`). `SwitchTo` is a no-op for a Space already shown or not a Space (`koan_workspaces.cc:503-510`).
  - A tab unloaded by a Space is replaced and drops its card (kReplaced).
  - The media view is mounted above `KoanSpaceBarView` by class name; whichever is installed later, the bar ends up last (`koan_media_controller.cc:1145-1172`).
- **Mac guards.**
  - Linux-only window-move code and its deps are under `BUILDFLAG(IS_LINUX)` / `is_linux` (`koan_window_drag.cc:34-43, 238-259`; end of `BUILD.gn`).
  - The Mac title-bar inset is `#if BUILDFLAG(IS_MAC)` (`koan_welcome.cc:1497-1499`), the card radius `#if IS_MAC` (`koan_sidebar_metrics.h`), and the maximized threshold excludes Mac (`koan_window_drag.cc:172-180`).
  - The overlays use `TYPE_CONTROL` with `accept_events = false` (`koan_download_animation.cc:247-254`), which round 1 verified ignores the mouse on Mac.
  - Nothing here was compiled or run on a Mac. The `#else` branch of `StartDrag` (`koan_window_drag.cc:260-285`) has never been built; it is listed in #68.
- **Hooks.** The new patch hunks are minimal and `docs/REBASE.md` has rows for the download hook and for the two no-patch mounts; the CI patch test passes.
- **Stray files / symlinks.** `git ls-tree -r HEAD | grep '^120000'` is empty and no added file has a mode other than 100644. Added non-code files: six screenshots under `docs/screenshots/` (all under 62 KB), e2e pages/flags, and `.agent/notes/reviews/pr66-review.md` (the round-1 review; develop already keeps such files in that folder). No `.orig`, `.rej`, editor or build leftovers.

### Should-fix

#### SF1. A second activation of "Dive in!" runs `Apply` twice (duplicate pinned tabs, two import dialogs)
- **Where.** `koan_welcome.cc:952-965` (`Go`) and `KoanWelcome::Finish` (`:1432-1467`).
- **What.**
  - `Finish` only returns early if `widget_` is null (`:1433`), and the widget lives until the 300 ms fade ends.
  - The "Dive in!" button is still there during the fade, so a double-click or a held Enter posts a second `Go(1)`. `page_ + 1 >= kWelcomePageCount` then calls `Finish` again.
  - Each call starts its own `AnimationBuilder` on the same opacity. The animator's default preemption is `IMMEDIATELY_SET_NEW_TARGET`, which runs the old sequence to its end (firing its `OnEnded`) and ends the new one at once (`ui/compositor/layer_animator.cc:703-724, 748-768`). Both `OnEnded` callbacks post `close_and_apply`.
  - The second one finds the widget already closed, but `Apply` still runs (`:1436-1442`: `Close()` then `Apply()` unconditionally while the weak pointer is alive).
  - Result: every chosen Essential is added with `AddTabAt` twice, and `IDC_IMPORT_SETTINGS` runs twice when import was chosen.
- **Fix.** Make `Finish` idempotent with a `finishing_` flag (set before the fade, return on re-entry), or skip `Apply` in `close_and_apply` when `widget_` is already gone.

### Nits

- **N1. A pending toast action can hide a later toast.** `RunAction` posts a task that calls `toast->Hide()` on whichever toast is showing when it runs (`koan_toast.cc:216-231`). If another toast replaced the first between the click and the task, the new one is hidden. Compare `view_` identity (or a toast generation) before hiding.
- **N2. The toast animations ignore reduced motion.** `ShowView`/`Hide` use `ScopedLayerAnimationSettings` with no check (`koan_toast.cc:268-273, 320-324`); layer animators do not check `ShouldRenderRichAnimation()` (I grepped `ui/compositor`). The animation is old, but this PR adds two more toasts that use it. Skip the transition when `!gfx::Animation::ShouldRenderRichAnimation()`, as the welcome and media code now do.
- **N3. Small B1 leftovers.**
  - The welcome timing constants are Zen's own numbers in the MIT file: `koan_welcome.cc:106-115` (35 ms stagger with an 800 ms start, 450 ms, 12 ms exit stagger, 30 ms text stagger, 450 ms pages fade) against `ZenWelcome.mjs:256, 331, 802-818`. The media and download timings were moved into their `koan_zen_*` headers; do the same here for consistency.
  - `scripts/e2e/welcome.mjs:28-62` quotes Zen's ftl titles ("Bring your stuff along." and the others). It is a handful of short strings, so low risk, but it is a verbatim copy in an MIT file.
  - `THIRD_PARTY_NOTICES.md:56-58` still says "Zen" is "referenced only to describe the user experience", which now contradicts the section above it. Reword it to the non-affiliation point only.
- **N4. The colour swatches are a radio group, but only Tab reaches them.** Only `KoanWelcomeOption` handles arrows (`koan_welcome.cc:353-366`); `KoanWelcomeSwatch` and `KoanWelcomeColorsView` (`:545-613`) have no `OnKeyPressed`, while the PR reply says "the options and the colours are radio groups: arrow keys move". Space/Enter pick a swatch and Tab visits all nine, so it is usable. Either add arrow handling or drop the claim.
- **N5. Stale "once per profile" wording.** `koan_welcome.h:36` ("first run of a profile") and the flag description `koan_flag_entries.h:66` ("on the first run of a profile") disagree with what the same header says at `:44-45` (first run of a user data directory).
- **N6. No download animation at all without a press.** The hook suppresses Chrome's arrow whenever Koan's is enabled and motion is allowed (`koan_browser_view_hooks.cc:30-33`), but `Play()` returns without drawing when no press was recorded (`koan_download_animation.cc:221-223`). A download started from a keyboard shortcut or a script before any click gets no animation (Chrome's toolbar arrow would have shown if its button is visible). This matches Zen for the no-click case, so low priority. Also the PR reply says `Play()` checks reduced motion itself; the check is in `OnDownloadCreated` (`:170-173`), its only caller, so only the comment is off.
- **N7. The seen pref is set before the screen shows.** `koan_welcome.cc:1410-1412`: closing the window or a crash mid-flow means the screen never comes back. Zen sets its flag when the flow completes. Probably intended; either say so in the header comment or set it in `Finish`/`Skip`/`OnBrowserViewDestroying`.
- **N8. No tests for the pure Zen helpers.** `FormatMediaTime`, `ComputeDownloadArc`/`GetDownloadArcFrame` and `IsEmptySpaceScript` are pure and now live in their own files; CI only runs the Python unit tests (`.github/workflows/ci.yml`) and the e2e does not target them directly. Optional.

## 3. Merge recommendation

Mergeable once SF1 is fixed (a few lines in `Finish`). Round-1's blocker (B1) and S1-S4 are properly fixed; S5, S6 and the rest are tracked in the open issue #68, and the nits above can go there or into a follow-up commit.
Before a release (not for merge): the macOS behaviour (title-bar inset, `RunMoveLoop`, Now Playing, focus and Esc in the child NSWindow) has only been reasoned from source and needs a run on the Mac, with the user's consent.
