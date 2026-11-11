# Patch audit against the "Upgrade-safe code" rules (#183)

Date: 2026-11-07. Koan `develop` aa2ef917 (+ this branch's REBASE.md rows), pin
158.0.8127.2. Findings only: nothing here is refactored. Each finding has a
proposal (fix, consolidation or "keep") and a rough size; the owner is whoever
next touches that file, or a dedicated chore PR.

The rules are in `.agent/notes/agent-brief.md` ("Upgrade-safe code"): 1 prefer
extension points, 2 tiny boring patches (a call into `//koan`, no copied logic,
no reformatting, a REBASE row), 3 break loudly (symbols, not strings; a test
for behaviour), 4 no private poking, 5 same code on every milestone.

## The numbers (`scripts/ci/patch_report.py`)

114 patches, 1085 changed upstream lines. 62 patches are 5 lines or fewer, 33
are 6-15, and 19 are bigger. The seven biggest are a third of the lines (347)
and the twelve biggest nearly half (498). 786 lines are under
`chrome/browser/ui`. 30 patches are `BUILD.gn` dependency lines (64 lines),
six are `.mm` (macOS), and four are Linux-only (23 lines). No `#if
CHROMIUM_VERSION` fork anywhere (rule 5 holds).

Before this PR, five patches had no hook-table row (blink `core/BUILD.gn`,
`performance_manager/BUILD.gn`, `browser_web_contents_delegate.cc`'s `BUILD.gn`
and name, `native_theme_features.cc`); the rows are added here, and CI now
fails a new patch without one.

## Summary

| # | Finding | Rule | Rebase risk | Proposal | Size |
|---|---|---|---|---|---|
| A1 | Layout patches carry mirror and swap logic inline (`browser_view_tabbed_layout_impl.cc` 73 lines, `browser_view_layout_impl.cc` 42, `vertical_tab_strip_region_view.cc` 58) | 2 | high: the biggest and the most edited upstream files | move the logic into `//koan`, leave one call per hunk | M |
| A2 | `vertical_tab_strip_state_controller_impl.cc`: nine hunks of `if (koan::OwnsSidebar())` switching upstream's state machine off piece by piece | 2, 3 | high: a new upstream save/restore path is silently not switched off | one policy hook at the setters; a guard test for "never saved" | M |
| A3 | `freezing_policy.{h,cc}`, `cannot_freeze_reason.{h,cc}`: 58 lines of Koan logic (timer, new reasons) inside a component | 2 | high, documented exception | move the logic out of the component, keep an accessor | L |
| A4 | `saved_tab_group_model_listener.cc` 38 lines: a helper with eight call sites that erases entries of upstream's private listener map | 2, 4 | high: depends on the map's type and the CHECK it works around | one hook call; use the service's public disconnect | M |
| A5 | Three omnibox providers wrap upstream `FormatUrl(...)` calls, re-indenting upstream lines (`zero_suggest_verbatim_match_provider.cc`, `recently_closed_tabs_provider.cc`, `history_url_provider.cc`) | 2 | medium: hunks fail on any reformat | fix upstream or sanitize in one place | S |
| A6 | `layout_constants.cc`: a table of Zen's numbers inside the patch | 2 | low | `koan::SidebarLayoutConstant()` | S |
| A7 | `tab_group_view_vertical_layout.cc`, `vertical_tab_strip_top_container.cc`: logic in the `CalculateProposedLayout` bodies (33 and 41 lines) | 2 | high: this body was rewritten in M158 | keep the hook calls; move the branches into `//koan` | M |
| A8 | `browser_animation_controller.cc`: a counter inside upstream's class to work around a CHECK that #176 also removed on Koan's side | 2 | medium | decide which fix stays; drop the other | S |
| A9 | `appearance_page.html.ts`: rows hidden by editing upstream's settings markup | 2, 3 | medium: any markup refactor breaks it silently | hide in Koan's own layer or add a settings test | S |
| A10 | Four Linux-only patches (23 lines) while Linux builds are disabled | 5 | medium: they still conflict | delete them | S |
| B1 | `kCtrlTabMru` is a string literal `"browser.ctrl_tab_mru"` in `koan_settings_rows.cc`, and its comment says `prefs::kCtrlTabMru` | 3 | medium: a rename is silent | use `prefs::kCtrlTabMru` | XS |
| B2 | `"koan.bookmarks_bar.show"` as a string, twice, in `bookmark_bar_controller.cc.patch` | 3 | low | use the symbol from `koan_prefs.h` | XS |
| B3 | URL literals for Chromium pages (`chrome://history`, `chrome://downloads`, `chrome://settings`, `chrome://flags`, `chrome://extensions`) in 7 files | 3 | low | use the `chrome::kChromeUI*URL` constants | S |
| C1 | 17 comparisons in 6 files find upstream views by class name (`"VerticalTabStripRegionView"` 7 times, `"FlexLayoutView"` 3, seven others), and some downcast the result | 4 | high: one rename breaks one place, found only at runtime | one `koan::` finder per view and a test that finds each | M |
| C2 | `koan_native_window_move_mac.mm` reads AppKit's private `_tilingStateController` ivar and `tilingState` selector | 4 | OS-version risk, not a rebase risk | keep, add a Mac step that fails loudly | XS |
| C3 | Copied upstream sequences: compact mode mirrors `ReparentTopContainerFor{Start,End}OfImmersive` | 2 | high, documented | keep; add the guard in behaviour-guards.md | S |
| D1 | Upstream behaviour Koan relies on without a guard | 3 | see `behaviour-guards.md` | add the missing guards (the "none" rows) | L |

Sizes: XS under an hour, S a few hours, M about a day, L several days.

## Rule 2: copied logic and large hunks

### A1. Layout mirroring and swapping lives in the upstream files

`browser_view_tabbed_layout_impl.cc` (73 lines, 11 hunks) and
`browser_view_layout_impl.cc` (42, 5) hold real code, not calls: the swap of
`leading_exclusion`/`trailing_exclusion` (done three times in one function),
the mirrored `clip_insets` built by hand, `std::swap` of corner orientations,
a `koan_bounds` rect computed in place, and a reflow condition. They are the
biggest patches and sit in the two most edited layout files. Every one of these
is "do X to a value and mirror it".

Proposal: `koan::MirrorExclusions(params)`, `koan::MirrorInsets(insets)`,
`koan::MirrorCorners(corners)` and `koan::PlaceTopContainer(...)` in
`//koan` (all pure functions of the values, unit-testable), each hunk becoming
one call. `vertical_tab_strip_region_view.cc` (58, 9): the `koan_below_snap`
block in `OnResize` is a decision about what a drag may do; move it behind
`koan::ResolveSidebarDrag(...)` returning the new width and whether it hid
the sidebar.

### A2. The state controller is switched off by 9 hunks

`vertical_tab_strip_state_controller_impl.cc` (40 lines, 9 hunks) wraps
`koan::OwnsSidebar()` around: the initial collapsed state, `IsEnabled()`, the
setters, the lock handling, the expand-on-hover answer, and both places the
collapsed state is saved (session extra data and the pref). A new save path
added upstream is not covered, and nothing fails: Koan would start persisting a
collapse state again.

Proposal: put the policy behind one function Koan owns (the setters already
go through `IsSidebarCollapseRequestAllowed()`), keep one hook per upstream
entry point, and add a guard for the invariant "the collapsed state is never
saved" (`scripts/mac/vtabs.sh MODE=old` reads the saved state back after a
quit; check it also covers the session-extra-data path).

### A3. Freezing: Koan logic inside a component

58 lines over four files in `components/performance_manager`: a one-hour
`OneShotTimer`, two new `CannotFreezeReason` values, the `kVoting` removal,
and `OnIsPinnedTabChanged`. A component cannot depend on `//koan`, which is
why the logic sits in the patch (documented in REBASE.md). It is the one
patch with real behaviour and no `//koan` call at all, and
`CannotFreezeReason` is a list the next milestone is likely to grow.

Proposal (needs a check against the policy classes before deciding): keep the
reasons in `//koan` as a `GraphOwned` policy registered from the existing
`chrome/browser/performance_manager` hook, and leave `FreezingPolicy` with a
public `AddCannotFreezeReason(page_node, reason)`/`Remove...` pair (a
smaller, more stable surface than the timer and state). If that is not
possible, at least add a unit test next to the logic (`freezing_policy`
tests do not run today, see REBASE.md).

### A4. Saved tab groups: a helper that edits a private map

`saved_tab_group_model_listener.cc` (38 lines, 12 hunks): the anonymous
function `DropKoanDisconnectedListeners()` does `std::erase_if` on
`local_tab_group_listeners_`, called at eight entry points, to avoid a CHECK
in upstream when Koan disconnects a Space from a saved group through the
service's public API. It encodes upstream's invariant (a listener exists only
for groups the service still maps) and its map type.

Proposal: the listener class has its own `DisconnectLocalTabGroup()` (the
comment in the patch says it "pairs that with dropping the group's
listener"). Check whether Koan's `KoanWorkspaceController::DisconnectSavedGroups()`
can reach and call it instead of the service's `RemoveLocalTabGroupMapping()`;
then no listener exists for a disconnected group and the eight calls go. If it
cannot be reached, make the patch one method on the listener class
(`DropListenersUnmappedBy(service)`) called from one place. Not verified here:
whether that method is reachable from `//koan`.

### A5. Three providers re-indent upstream statements

`zero_suggest_verbatim_match_provider.cc` (9 removed lines),
`recently_closed_tabs_provider.cc` (5), `history_url_provider.cc` (3): each
replaces an upstream `match.contents = url_formatter::FormatUrl(...)` with
`AutocompleteMatch::SanitizeString(url_formatter::FormatUrl(...))`, and
clang-format re-wraps the call. The patch is a bug fix for an upstream
DCHECK (#166: a URL ending in `%20` yields contents with a trailing space).
Three copies of one fix, each failing if upstream re-wraps the statement.

Proposal: send the fix upstream (the DCHECK is theirs), and meanwhile keep
the hunks to the smallest form: leave the statement alone and add one line
after it (`match.contents = AutocompleteMatch::SanitizeString(match.contents);`),
which survives any re-wrapping.

### A6. Layout constants

`layout_constants.cc` (18 lines): a `switch` with Zen's numbers (36, 46, 48,
radius 14) inside upstream's `GetLayoutConstant`. Proposal:
`if (auto v = koan::SidebarLayoutConstant(constant)) return *v;`, the table in
`//koan` (it already holds `look::StyleRadius`).

### A7. Layout bodies that were rewritten in M158

`tab_group_view_vertical_layout.cc` (33 lines, 5 hunks) and
`vertical_tab_strip_top_container.cc` (41, 11) both sit inside
`CalculateProposedLayout()`, in files upstream split or rewrote for M158 (the
ports in #89). Proposal: where the hunk computes (`koan_space`,
header visible/height), hand the whole decision to one `koan::` function
returning a small struct; the hunk then only reads it.

### A8. Two fixes for one crash

`browser_animation_controller.cc` (14 lines) adds a `start_count_` to
upstream's class so a `kEnded` listener that starts the next motion no longer
trips `CHECK(current_motion_)`. #176 fixed the same crash on Koan's side
(taking the keep-expanded locks only after the collapse). Having both hides
which one is load-bearing. Proposal: keep the behaviour guard (see
behaviour-guards.md, "kEnded order") and decide: the Koan-side fix alone is
the cheaper one to carry; if the patch stays, say in its REBASE row what it
protects against that #176 does not.

### A9. Hiding upstream settings rows by editing their markup

`appearance_page.html.ts` hides the horizontal-tabs row and the vertical-tabs
toggles by adding `hidden` to upstream's Lit template (7 changed lines).
It does not fail to apply when the markup changes shape; the rows just
reappear. Proposal: a settings e2e step that loads
`chrome://settings/appearance` and asserts neither row is visible (the
`settings.mjs` suite is the place), or hide them through Koan's own
`chrome://koan-settings` layer.

### A10. Linux-only patches

`native_theme_features.cc` (14), `channel_info_posix.cc` (3),
`chrome_paths_linux.cc` (3), `shell_integration_linux.cc` (3). Koan is a Mac
product and Linux builds are disabled, so these only cost rebase work (the
first of them has a `#if BUILDFLAG(IS_LINUX)` with a changed default). The
scrollbar patch is kept on purpose, for the Linux e2e screenshots (its
REBASE row says so), so it goes when that harness goes; the other three have
no such reason and can go now. Delete each with its REBASE row.
`session_startup_pref.cc` (4) is cross-platform and stays.

## Rule 3: strings where symbols exist

* **B1.** `src/browser/ui/webui/koan_settings_rows.cc:71`:
  `constexpr char kCtrlTabMru[] = "browser.ctrl_tab_mru";` The comment above it
  names `prefs::kCtrlTabMru` (`chrome/common/pref_names.h`). Use the symbol.
* **B2.** `patches/chrome/browser/ui/bookmarks/bookmark_bar_controller.cc.patch`
  uses `"koan.bookmarks_bar.show"` twice (a registrar key and a read) while
  `kBookmarksBarShow` is in `koan/browser/koan_prefs.h`. Koan's own, but a
  typo in either string is silent; the patch already depends on `//koan`.
* **B3.** `chrome://history`, `chrome://downloads`, `chrome://settings`,
  `chrome://flags`, `chrome://extensions` appear as literals in
  seven files (`koan_library_history.cc`, `koan_library_downloads.cc`,
  `koan_shortcuts.cc`, `koan_glance_controller.cc`, `koan_vertical_tabs_policy.h`,
  `koan_main_menu_mac.mm`, `koan_settings_handler.cc`). Chromium has
  `chrome::kChromeUIHistoryURL`, `kChromeUIDownloadsURL`, `kChromeUISettingsURL`,
  `kChromeUIFlagsURL` and `kChromeUIExtensionsURL` in `chrome/common/webui_url_constants.h`. These
  URLs are stable for users, so this is low risk.

What is already right: features and IPH overrides in
`koan_feature_overrides.cc` are all C++ symbols, commands are `IDC_*`, menu
items are found by `IDC_*` tag, and prefs are `prefs::k*`.

## Rule 4: private internals

* **C1.** Class-name lookups of upstream views: 17 comparisons in 6 files
  (`GetClassName() == "..."`; three more are Koan's own view classes):
  `VerticalTabStripRegionView` (`koan_workspaces.cc`,
  `koan_library_controller.cc`, `koan_sidebar_animation.cc` x2,
  `koan_compact_mode_controller.cc` x2, `koan_media_controller.cc`),
  `FlexLayoutView` (3), `VerticalTabStripTopContainer`, `FrameSeparator`,
  `VerticalTabStripBottomContainer`, `NewTabButton`, `TopContainerOverlayView`,
  `ImageButton`/`FrameCaptionButton`. Some results are then `static_cast` to
  upstream types (`BaseTabStripRegionView*` in `koan_workspaces.cc`). Upstream
  offers only `vertical_tab_strip_region_view_for_testing()`. REBASE.md names
  several of these ("found by class name"), but no test finds each one and
  fails when it cannot.
  Proposal: one helper per upstream view (`koan::FindSidebarRegion(BrowserView*)`
  and so on) so the class string lives once, plus one guard that asks for
  every one of them in a running window (the Mac `vtabs.sh` invariant
  `checkKoanSidebar()` is the place). The seven repeated region lookups also
  become one.
* **C2.** `koan_native_window_move_mac.mm` uses `class_getInstanceVariable(
  [NSWindow class], "_tilingStateController")` and `performSelector:@selector(
  tilingState)`: AppKit private API, guarded by `respondsToSelector` (so it
  degrades quietly). Not Chromium, but a macOS update can change it. Keep; the
  window-drag Mac step should assert the result, not only the absence of a
  crash.
* **No friend declarations, no `reinterpret_cast` of upstream types, and the
  three `const_cast`s are on `TabInterface*` handed to Koan's own controllers**
  (`koan_pinned_tabs.cc`, `koan_pinned_tab_views.cc`): harmless.

## Rule 1: extension points

Patches that add a global hook of their own where Chromium has none are the
right kind of patch and are not findings: `color_mixers.{h,cc}`
(`SetCoreColorMixerOverride`: no upstream point sits before the derived
mixers), `omnibox` `AddAutocompleteProviders`, tab menu, session data. The
ones that could be extension points but are patches:

* **Settings page rows (A9)** could be Koan's own settings page.
* **`layout_constants.cc` (A6)** is the only upstream-supported way to size a
  vertical tab row; a hook there is right, only the table should move.
* Feature and IPH overrides already use `koan_feature_overrides.cc`; nothing to
  move.

## Upstream behaviour relied on without a test (D1)

See `behaviour-guards.md`: each REBASE row's assumption, with the test that
fails when it changes, or "none".

## Proposed order if these are done

1. B1, B2 (minutes), A10 (delete Linux-only patches).
2. A8, A5, A6, A9 (small, mechanical).
3. C1 (one helper + one guard), then A1/A7 (layout logic into `//koan`) as part
   of the next milestone rebase, when the files are open anyway.
4. A2, A4, A3 each with its own test, before the milestone after next.
