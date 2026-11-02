# Review of PR #91: macOS traffic lights in the sidebar and trackpad swipe between Spaces (#72)

Branch `feat/mac-traffic-lights-swipe` (2 commits on `develop`), reviewed read-only.
Chromium pin: `67e29b8287f1514f45703b969078748c5fa700f9` (158.0.8111.0). Every upstream file
below was fetched from googlesource at that commit and compared with the code the PR calls.

Result: **0 Blockers, 3 Should-fix, 7 Nits.** Likely to compile on macOS: yes (about 90%).
Nothing here has been compiled or run on a Mac, including by me.

## 1. Will it compile on macOS?

I found nothing that would fail. What I traced:

**Hook patches.** I applied all three patches with `git apply` to the pristine files, and the
resulting blob hashes equal the `index ..` lines in the patches:
- `browser_native_widget_mac.mm` -> `acd70980...`
- `BUILD.gn` -> `7ef08fcb...`
- `browser_view_tabbed_layout_impl.cc` -> `a9df73f6...`

**`browser_native_widget_mac.mm` hook.**
- `GetWindowFrameTitlebarHeight` sits at upstream lines 276-296. The hook is inside the
  `browser_view_ && GetFrameView()` branch.
- `browser_view_` is a `raw_ptr<BrowserView>` (`browser_native_widget_mac.h:128`). It converts
  implicitly to `const views::View*`, because `browser_view.h` is already included in that file.
- `*titlebar_height` is a `float*`, so assigning an `int` is fine.
- `std::optional<int>` and `build/build_config.h` come from `koan_traffic_lights.h`.
- The new `#include` is alphabetically placed.

**`BUILD.gn` hunk.** The dep `"//koan/browser/ui/views"` lands in `source_set("browser_native_widget_mac_impl")`
(upstream lines 1981-2030), inside `if (is_mac)`, between `//components/web_modal` and `//skia`.
- That target `:views` is header-only, with no cycle. The same pattern is used by the other frame patches.
- `koan_traffic_lights.h` is in the `views` target's `sources`, so `gn check` passes.
- Definitions are in `:impl`, which is linked via `//chrome/browser`. That is the same arrangement
  REBASE.md describes for hook targets.

**`koan_traffic_lights_mac.mm`.**
- Every API used is already used by Chromium in the same form:
  - `Widget::GetNativeWindow().GetNativeNSWindow()` (`browser_frame_view_mac.mm:192`).
  - `-standardWindowButton:` with `NSWindowCloseButton`, `NSWindowMiniaturizeButton` and
    `NSWindowZoomButton` (`browser_native_widget_window_mac.mm:124-134`).
  - `-setHidden:`.
- `const BrowserView*` -> `GetWidget() const` returns `const Widget*`, which is OK.
- The ternary `... : nil` is fine under ARC.
- The braced-init range-for over the three enumerators of `NSWindowButton` is fine.
- The `if (is_mac) { sources += [...] }` comes after `sources = [...]` (`BUILD.gn:106-110`).

**`koan_workspaces.cc` Mac block.**
- `ui::ScrollEvent` / `ui::EventType::kScrollFlingStart` / `kScrollFlingCancel` /
  `ui::ScrollEventPhase::{kBegan,kEnd}` / `ui::EventMomentumPhase::{NONE,END}` all exist
  (`ui/events/event.h:1059-1060`, `event_constants.h:153-202`).
- `ui::GetMouseWheelTick120ths` is unchanged from before.
- The `#if` inside the constructor initialiser list yields a valid `: controller_(c), swipe_(c) {}`.
  Member order is `controller_` then `swipe_` (`koan_workspaces.cc:197-263`).
- `build/build_config.h` is included in `koan_workspaces.cc:24`, `koan_compact_mode_controller.cc:15`,
  `koan_browser_view_controller.cc:11` and `koan_sidebar_metrics.h:15`.
- `kMacSidebarTopRowHeight` is `#if IS_MAC` and used only under `IS_MAC`
  (`koan_browser_view_controller.cc:187`).

**Linux-compiled code.** The rest (`koan_space_swipe`, controller, `koan_traffic_lights.cc`,
workspaces animation) is portable and was compiled on Linux. Its Mac differences are nil.
`traffic_lights_` is created only under `IS_MAC` (`koan_compact_mode_controller.cc:217-223`).

Caveat: `-setHidden:` and the style-mask toggle (section 3) are things only a Mac run can confirm.

## 2. Findings

### Should-fix

**S1. `KoanSpaceSwipe` relies on `ScrollEventPhase`, which macOS never sets, so the momentum handling does not work as designed.**
`koan_space_swipe.cc:45-58`
- `ui::ScrollEvent(const PlatformEvent&)` leaves `scroll_event_phase_` at `kNone`
  (`ui/events/event.cc:1205`: "TODO(bokan): This should be populating the |scroll_event_phase_|
  member but currently isn't").
- On Mac, the phases arrive only through `momentum_phase` (`ui/events/cocoa/events_mac.mm`
  `GetScrollOffsets`, lines 242-278):
  - begin and may-begin -> `MAY_BEGIN`
  - `phase` Ended/Cancelled (the finger lift) or `momentumPhase` Ended -> `END`
  - momentum updates -> `INERTIAL_UPDATE`
  - ordinary updates -> `NONE`
- Consequences:
  - The `scroll_event_phase()==kBegan` and `==kEnd` tests (lines 48, 51) are dead on macOS. New gestures
    are detected only by the 300 ms gap.
  - The first event of every gesture has `MAY_BEGIN`, so the `momentum_phase() != NONE` test (line 49)
    classes it as "lifted" and drops its delta.
  - The finger lift already has `momentum_phase END`, so lines 70-71 call `Reset()` at once. The `kEnded`
    state never survives. The system momentum that follows is not consumed (`ours` is false, line 64),
    contrary to the comment at lines 62-63 and in `koan_space_swipe.h`.
  - Leaked momentum is ignored by upstream (`vertical_tab_strip_focus_swipe_controller.cc` ignores
    `momentum_phase != NONE`, and Koan consumed everything from the lock onwards), so there is no double
    switch. But the tab list underneath receives the tail of the swipe, and the REBASE.md note ("needs the
    scroll phases ... handled events not reaching the controller behind it") is wrong about macOS.
- Fix: drive the state from `momentum_phase` only.
  - Begin = `MAY_BEGIN`, and keep its delta.
  - Lift = the first `END` or `INERTIAL_UPDATE`.
  - Consume `INERTIAL_UPDATE` while in `kEnded`. That means moving the `END -> Reset()` so it only fires
    when the previous event was already momentum, or on the real final `END`.
  - Keep `kScrollFlingCancel`.
  - Fix the comments and the REBASE.md row.

**S2. Visibility is not re-applied after the style-mask relayout.**
`koan_traffic_lights_controller.cc:90-96`
- `LayOutTrafficLights()` runs `BrowserFrameViewMac::OnTabStripStateChanged()`. That toggles
  `NSWindowStyleMaskFullSizeContentView` and calls `setFrame:display:` (upstream
  `browser_frame_view_mac.mm:191-207`). Nothing calls `SetTrafficLightsShown()` afterwards.
- It runs in a posted task after `UpdateCaptionButtons()` has already hidden or shown the lights.
- If AppKit rebuilds the titlebar widgets on a style-mask change, as Electron has to guard against, the lights
  reappear when the layout is meant to hide them. The affected case is the sidebar on the right, with the strip
  collapsed.
- This is the one part I could not confirm without AppKit. The fix is cheap and idempotent: call the
  `on_sidebar_shown_changed_` callback (or a new "relaid out" callback) at the end of `LayOutTrafficLights()`.

**S3. Zen's gesture constants and formula are ported into an MIT file.**
`koan_workspaces.cc:96-103`, `1290-1330`; the file's header is Koan MIT; it is not listed in THIRD_PARTY_NOTICES.md.
- The constants are:
  - `kSwipeSuccessFraction = 0.25` (`ZenSpacesSwipe.SUCCESS_THRESHOLD`);
  - `kSwipeMomentumSeconds` (`SUCCESS_VELOCITY_CONTRIBUTION`);
  - the resistance `1 - |x| / (4.5 w)` with a 0.5 floor (the comment cites
    `ZenSpacesSwipe#handleSwipeUpdate`; the formula appears there at lines 169-176).
- CONTRIBUTING.md says a file that ports Zen "code, data tables ... or style values (more than matching a
  behaviour or a few numbers)" takes the MPL header and a THIRD_PARTY_NOTICES row, and the ported material
  should be kept "in as few files as practical". Three constants and a verbatim formula, named in the comment,
  are on the wrong side of "a few numbers".
- Putting an MPL header on the 1,567-line `koan_workspaces.cc` is the worse option. Move the constants and a
  `ResistedOffset()` / `SwipeSucceeds()` helper into `koan_space_swipe.{h,cc}`, give that file the MPL header
  naming `ZenSpacesSwipe.mjs`, and add a row to THIRD_PARTY_NOTICES.md.
- The rest of the PR is correctly MIT:
  - `koan_traffic_lights*` reference Zen's behaviour only.
  - `koan_sidebar_metrics.h` is already MPL and already lists Zen's stylesheets as sources. The 38 px value is
    verified: `zen-tabs/vertical-tabs.css:1037-1042` (`#zen-sidebar-top-buttons`, single toolbar, macOS 38 px).

### Nits

1. `koan_workspaces.cc:96-100`: the comment says `kSwipeMomentumSeconds` is Zen's `SUCCESS_VELOCITY_CONTRIBUTION`.
   The values differ (0.08 s against 0.5). Say how 0.08 was derived, or reuse the Zen value in matching units.
2. `koan_workspaces.cc:1296`: the resistance floors at 0.5 and keeps moving. Zen stops translating once the
   multiplier would go below 0.5 (`ZenSpacesSwipe.mjs:172-176`).
3. `koan_space_swipe.cc:51-58`: the 300 ms gap ends a swipe the user is still holding. macOS stops sending events
   while the fingers are still, so a pause longer than 300 ms mid-drag switches Space or springs back. Zen has no such
   timeout.
4. `USAGE.md` / ZEN_PARITY say the Space "follows your fingers", but the drag uses `scrollingDeltaX`, which already includes the
   natural-scrolling inversion (`events_mac.mm:166`). With natural scrolling off the Space moves against the fingers.
   The docs note this for the switch direction but not for the drag.
5. `koan_workspaces.cc:1284-1289` (`CanSwipe`): while a 250 ms slide is running, `UpdateSwipe` is a no-op but
   `KoanSpaceSwipe` still consumes the gesture. A swipe begun just after another is swallowed rather than passed on.
6. `koan_traffic_lights.cc:50-62`: `GetCursorScreenPoint` plus a frame-relative rectangle ignores window
   occlusion, so another app's window over the corner counts as "over the lights". The rectangle is 74x28 (Chromium's exclusion),
   not the 38 px row, so the lower 10 px of the row do not count.
7. Unrelated e2e fix (`compact_flash`) is bundled in the PR. It is correct, but belongs in its own PR.

## 3. Behaviour

**Traffic-light placement.** All checked against upstream source, not on hardware.
- `BrowserWindowFrame` (`browser_native_widget_window_mac.mm:48-69, 100-102`) asks the host for
  `GetWindowFrameTitlebarHeight` when not fullscreen (or when `ShouldUseCustomTitlebarHeightForFullscreen()`),
  and `_shouldCenterTrafficLights` is `YES`. So the hook's 38 px does centre the lights in the row.
  macOS 26 adds `_minXTitlebarWidgetInset = 13`.
- **Default (single toolbar, sidebar left):**
  - The hook returns `max(28, 38) = 38`. 28 is `GetCaptionButtonBounds`' bottom, `10 + 18`.
  - The layout patch uses the same value for `toolbar_height` and `caption_button_width = 74 - x`.
  - `UpdateCaptionButtons` keeps the lights shown through the "sidebar is on my side and shown" clause
    (`koan_compact_mode_controller.cc:1313-1315`).
  - `ContentCardInsets` leaves the strip collapsed. Consistent.
- **Sidebar right (single toolbar):**
  - The lights stay at the window's left (Chromium's `CaptionButtonsOnLeadingEdge`; the mirror hook swaps the exclusions).
  - They are hidden until the strip or toolbar is revealed, and the strip then gets the row height
    (`koan_content_card.cc:45-49`).
  - `HasPageSideCaptionButtons` returns true for the Mac path (`koan_compact_mode_controller.cc:1286-1288`).
- **Compact mode:**
  - The hook returns nullopt (Chromium's default height).
  - The lights are hidden until `toolbar_revealed_` or the sidebar peeks (`IsSidebarShown()` is `region visible && width>0`).
  - The pointer on the lights holds the sidebar peek and the toolbar reveal open
    (`koan_compact_mode_controller.cc:419-423, 1112, 1235`).
- **Fullscreen:**
  - Hook nullopt; `UseContentCard` is false, so `DesiredMode` is `kNone`, `Deactivate` runs
    `UpdateCaptionButtons`, and the lights are un-hidden.
  - `BrowserFrameViewMac::GetBrowserLayoutParams` zeroes the exclusion width in fullscreen (upstream
    `browser_frame_view_mac.mm:228-244`), which `IsPointerOverTrafficLights` treats as empty.
  - Lights stay hidden until the posted `UpdateActive` runs, i.e. briefly during the fullscreen transition.
  - The PR labels fullscreen "Partial", which matches this.
- Compared with Zen: the 38 px row, the hide-with-toolbars behaviour and the `onLeave` pointer rule match
  `ZenVerticalTabsManager` / `zen-compact-mode.css`.

**Swipe over the sidebar versus the page.**
- The handler is a pre-target handler on `region_` only (`koan_workspaces.cc:425-429`). The page's NSView takes
  `scrollWheel:` itself (`RenderWidgetHostViewCocoa` and `HistorySwiper`). The sidebar's events come from
  `BridgedContentView scrollWheel:` -> `ui::ScrollEvent` (`bridged_content_view.mm:1043-1055`). So history swipe is untouched.
- No double switch with upstream's `VerticalTabStripFocusSwipeController` (`OnScrollEvent`, Mac branch):
  - Koan's axis lock is 10 px against upstream's 15 px. Koan consumes from the lock onwards, so upstream's
    accumulated x stays under 15.
  - Upstream ignores all `momentum_phase != NONE` events and cannot rotate on them. This is also what makes S1 harmless to
    correctness.
- Wheel path is unchanged: real wheels (non-zero `GetMouseWheelTick120ths`) go to `Handle()` and never to `swipe_`.
- Sliding from the dragged offset (`PlaySwitchAnimation`, `koan_workspaces.cc:1214-1247`) is consistent. The new Space starts at
  `distance + dragged`, the old layer slides on from where it is, and `swiping_` / `swipe_start_offset_` are reset
  on every path (including the no-animation path, via the `std::exchange` at line 1326).
  `DropSwitchLayers` is guarded by `swiping_`.
- Direction: fingers moving left (negative offset) give the next Space in LTR, matching the slide and Zen's
  "right swipe -> forward".

## 4. Lifetimes

- **`KoanTrafficLightsController`** is owned by `KoanCompactModeController`, which holds it by `unique_ptr`.
  - The `ScopedObservation` removes itself, and `OnViewIsDeleting` resets it.
  - The posted `LayOutTrafficLights` uses a `WeakPtr`.
  - `OnTabStripStateChanged` reads `GetWidget()` unguarded, but the weak pointer dies with the controller (and so with the BrowserView),
    and the NSWindow is nil-checked inside `OnTabStripStateChanged`.
  - The callback uses `Unretained(this)` on the owner of the callee, which is fine.
- **`SetTrafficLightsShown`** nil-checks the widget and NSWindow. `OnBrowserViewDestroying` runs `Deactivate`
  and so un-hides the lights.
- **No event monitors or NSNotification observers are added.** The swipe uses the existing view pre-target handler,
  removed in `OnBrowserViewDestroying` (`koan_workspaces.cc:384`) and on region change (line 420).
  A stale `kSwiping` state in the reused handler recovers on the next event through the 300 ms gap.
- Nothing is called on a closed window that I could find.

## 5. Linux

- **Unaffected.** All Mac code is behind `BUILDFLAG(IS_MAC)` / `is_mac`.
- **Shared-code changes are inert:**
  - `GetHiddenToolbarRowHeight` is the identity.
  - `traffic_lights_` is null, so the `IsPointerOverTrafficLights` call sites are never reached.
  - `PlaySwitchAnimation` uses `dragged = 0`. The `GetSlidingViews()` refactor creates both layers up front rather than
    one at a time. This is equivalent.
- **Patches are minimal:** +6 lines in `browser_native_widget_mac.mm`, one new `BUILD.gn` dep, one changed line in the layout patch.
  `docs/REBASE.md` has the new row, the extended Caption-buttons row and the Space-swipe row. The row's claim
  that the exclusion is 12,10 62x18 on macOS 26 matches upstream.

## 6. Licence headers

- All new files carry the Koan MIT header; none port Zen code or data directly.
- Exception: see S3 (ported constants and formula in `koan_workspaces.cc`).

## 7. What only a Mac can answer (not counted above)

- Does `-setHidden:` on the standard buttons survive a window resize, focus changes and fullscreen transitions?
- Does the style-mask toggle in `OnTabStripStateChanged` cause a visible flicker at window open, when
  `applied_height_` first changes from nullopt to 38?
- Do the lights jump or flicker when single-toolbar activates a moment after launch?
- Does toggling the style mask during a fullscreen transition misbehave? The task checks `IsFullscreen()` when it runs, but
  Chromium's flag may flip only after the transition begins.
- Is the 0.08 s velocity weight equivalent to Zen's feel?

## Verdict

- No code I read would fail to compile on macOS. Risk is in AppKit behaviour (S2), not syntax.
- Fix S1 (small) and S3 (move code and a notices row) before merge. Confirm S2 on a Mac.
- Keep the PR a draft until the Mac CI compiles it and the lights, the hide/show cycle and a real swipe have been run on a Mac.
