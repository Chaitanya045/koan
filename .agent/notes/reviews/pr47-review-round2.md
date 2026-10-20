# Review, round 2: PR #47 "Performance and power: restore on demand, Memory Saver, tab freezing (never Essentials), no hidden repaints or polling" (Part of #45)

Branch `perf/power` at `b29d8d2` (24 commits on `origin/develop` `9b1c6d1`; merge base = develop tip, so it is
rebased). Checked against pristine Chromium 158 (`~/work/koan/chromium/src` HEAD) with `git show HEAD:<path>`.
I modified, built and ran nothing. I applied the patches with `git apply --check` to pristine copies in a scratch
directory (`/tmp/r2rev/apply-*`).

**Verdict: safe to merge once S1 and S2 are fixed.** Both are small. There are no blockers: B1 is fixed for
infinite-tabs freezing, and everything else from round 1 is fixed or answered. S1 and S2 are gaps in the new
exemptions. On the product platform (a MacBook on battery) they contradict what the PR title, docs and Settings
promise.

## What I verified and found correct

- **B1, enum and mapping.**
  - `kPinnedTab` and `kPlayedAudio` are appended after `kWebUI`, and `kMax` moves with them.
  - `CannotFreezeReason` is never persisted. There is no histogram enum in `tools/metrics` and no mojom enum.
    `discards_ui.cc:340-352` and the `Freezing` node describer only use `CannotFreezeReasonToString`.
  - The only `switch` over it is `cannot_freeze_reason.cc`, and the patch adds both cases (no `-Wswitch` hole).
  - UKM is safe. `RecordFreezingEligibilityUKM` intersects with `CannotFreezeReasonsForType(kBatterySaver)`
    before the `CHECK(Difference(...).empty())` at `freezing_policy.cc:1427`, and the new reasons are outside
    that set.
- **B1, scope.**
  - `CannotFreezeReasonsForType` removes both reasons for `kVoting` and `kBatterySaver`, so they block only
    `kInfiniteTabs`. That covers the plain branch and the memory-pressure branch.
  - The battery-saver CPU bookkeeping is unchanged: `cannot_freeze_reasons_since_last_cpu_measurement` is
    filtered by the battery-saver set at `:1194`. So voting and battery-saver freezing behave exactly as upstream.
- **B1, pinned tracking.**
  - `FreezingPolicy` registers as a `PageLiveStateObserver` in `OnPageNodeAdded`, before any tab-strip insertion.
  - `ActiveTabObserver` (`page_live_state_decorator_helper.cc:172-192`) sets the pinned state on insert, which
    covers session restore and moving a tab to another window. It also sets it on replace (discard or prerender
    swap) and on every pin or unpin. `set_is_pinned_tab` only notifies on a change.
  - The `Has()` guard keeps the add/remove `DCHECK`s in `OnCannotFreezeReasonChange` satisfied.
  - Koan's Essentials are Chromium pinned tabs, and there is no separate per-Space pinned state.
- **S4.**
  - `koan::RegisterFeatureOverrides` runs from `ChromeBrowserFieldTrials::RegisterFeatureOverrides`
    (`variations_field_trial_creator.cc:381`). That is after `InitFromCommandLine` and chrome://flags, and before
    `FeatureList::SetInstance` (`:390`).
  - `kDiscardFrozenBrowsingInstancesWithGrowingPMF` is read only at runtime (`freezing_policy.cc:1181`).
  - All three `build/*.gn` set `disable_fieldtrial_testing_config = true`, so no testing config re-enables it.
  - The override takes effect.
- **Settings row.**
  - `performance_manager::user_tuning::prefs::kTabFreezingEnabled` is a local-state bool (default true).
    `UserPerformanceTuningManager` observes it and forwards it live to `FreezingPolicy::SetFreezingEnabledByUser`.
  - The handler reads (`Settings()`), writes (`HandleSetPref`) and observes it on local state. The second
    registrar resets in `OnJavascriptDisallowed`.
  - `HandleSetPref` validates it like other rows: availability, `IsUserModifiable()` and the type.
  - `depends_on` and `note_pref` stay profile prefs.
  - Upstream chrome://settings has no control for this pref, so there is no duplicate.
- **N5.** `View::UpdateLayerVisibility` (`view.cc:2570`) folds in ancestors up to the first one with a layer.
  `TopContainerOverlayView` has no layer and its parents are always visible, so `IsTopContainerShown()` matches
  views' own result. The layer is re-shown on reveal, on floating, and on reparent (`AddChildViewAtImpl` calls
  `UpdateLayerVisibility`). On macOS that includes immersive fullscreen moving the top container into
  `overlay_widget()`.
- **Test harness.**
  - `--password-store=basic` sits in `KOAN_HEADLESS_UI_FLAGS`, which only `run.sh`, `run_profile.sh`,
    `shot_ui.sh` and `ui_session.sh` use, always with a `--user-data-dir` under `.test-profiles`.
  - `koan_test_env.sh` exits on anything but Linux, so nothing here can touch the user's real Koan, and on
    macOS it never runs.
  - `pkill -f -- "--user-data-dir=$prof"` only runs after `prof` is set (the trap is installed after
    `mktemp` succeeds).
- **Patches.**
  - All 52 patches apply to pristine files.
  - The `index` hashes of the 8 patches in this PR match the upstream blobs.
  - Every new hook has a row in `docs/REBASE.md`: freezing exemptions, restore on demand, power defaults and
    feature defaults. BUILD.gn patches are not listed by convention.
- **macOS, energy saver.** Koan no longer touches `battery_saver_mode.state`. Chromium's default is
  `kEnabledBelowThreshold` (`prefs.cc:41`) with a 20 % threshold (`battery_saver_mode_manager.cc:496`), and Settings
  shows the section on a Mac because it has a battery level provider.
- **macOS, compact mode.**
  - `EventMonitor::CreateWindowMonitor` on Mac is an NSEvent local monitor filtered to the browser NSWindow
    (`event_monitor_mac.mm:66-90`).
  - `NSEventTypeMouseExited` maps to `kMouseExited` (`events_mac.mm:53`), so a pointer that leaves the window
    produces one event. A single event is enough: from then on the one-shot timer finishes the hide by itself.
  - A keyboard peek with the pointer outside the window hides with no event at all (3 s grace).
- **Tests.** I read the e2e output in `~/work/koan/.cache/perf-power/build17.log` and `build18.log`. All power
  checks pass, including the restart, pinned and freeze-switch checks, and CI is green.
- **Round-1 N9 was mistaken.** For `kInfiniteTabs`, `CannotFreezeReasonsForType` removes `kRecentlyVisible`
  (`freezing_policy.cc:368-373`), so there is no 5-minute grace. The author is right.

---

## Blockers

None.

## Should fix

### S1. On a MacBook on battery, energy saver still freezes pinned tabs and paused players, with no wake-ups, while the PR promises they never freeze
- **Where:**
  - `patches/components/performance_manager/freezing/freezing_policy.cc.patch:9-10`, which removes both reasons
    for every type except `kInfiniteTabs` (`freezing_policy.cc:376-377` after patching).
  - The claims: PR title "(never Essentials)", `docs/PERFORMANCE.md:63-66`, `docs/USAGE.md:172`,
    `docs/ZEN_PARITY.md:102`, the Settings label at `src/browser/ui/webui/koan_settings_rows.cc:115-117`, and the
    flag text at `src/browser/koan_flag_entries.h:36-37`.
- **What's wrong:**
  - Upstream freezes CPU-heavy background tabs while energy saver is active (`freezing_policy.cc:487-505`;
    toggled from `battery_saver_mode_manager.cc:634`). On a MacBook that is the default below 20 %.
  - A browsing instance qualifies once its highest one-minute background CPU reaches 5 % at a time when no
    battery-saver reason applied. That high-water mark is never reset (`:1201-1209`).
  - Battery-saver freezing has no periodic unfreeze: `is_in_periodic_unfreeze` is only checked in the
    infinite-tabs branches. The Koan switch doesn't stop it either: `is_freezing_enabled_by_user_` gates only
    the infinite-tabs branches.
  - `PERFORMANCE.md:74-76` does say battery-saver freezing is unchanged. But the "Never frozen" list just above
    it, `USAGE.md`, `ZEN_PARITY.md`, the Settings row and the PR title all say pinned tabs never freeze.
- **Failure scenario:**
  1. On a MacBook, the user keeps Slack or Discord as an Essential (pinned) without notification permission.
  2. In the background it once had a busy minute, for example a sync after waking from sleep (5 % CPU or more).
  3. The battery falls below 20 % and energy saver turns on.
  4. The Essential is frozen outright, not even 5 s a minute, until it is shown or the Mac is plugged in. Its
     unread badge and messages stop. This is exactly B1's motivating case, on the product platform.
  5. The same applies to a YouTube Music tab paused for more than 5 minutes: the Play key goes to a frozen page.
- **Fix, either:**
  - Let `kPinnedTab` and `kPlayedAudio` also block `kBatterySaver`: remove them only for `kVoting` in
    `CannotFreezeReasonsForType`. The UKM `CHECK` still holds, because the intersection uses the same function.
  - Or keep upstream's behaviour and say so everywhere the promise is made: "never frozen to save tabs; with
    energy saver on, Chromium may still freeze a CPU-heavy pinned tab". Also drop "never Essentials" from the
    title.

### S2. `kPlayedAudio` protects the wrong tabs: viewing a paused player removes the protection, and a tab that ever made a sound keeps it until it is shown again
- **Where:** `patches/components/performance_manager/freezing/freezing_policy.cc.patch:14-25`. The reason is
  cleared when the page becomes visible while silent (`freezing_policy.cc:692-697` after patching) and set on any
  audible transition, visible or not (`:748-752`).
- **What's wrong:** the stated goal is that "a paused player still answers media keys" (`docs/PERFORMANCE.md:66-67`).
  Tying the reason to visibility fails both ways:
  - **Loses protection while paused.** Showing a silent tab clears the reason. Afterwards nothing protects a
    paused player except the 5-slot most-recently-used list: `kRecentlyVisible` is ignored for this freezing
    type, and `kRecentlyAudible` expired long ago.
  - **Never-ending protection.** The reason is also set while the tab is visible, and the only thing that clears
    it is the tab being shown again. A tab whose audio is finished stays exempt for as long as it stays in the
    background. That covers a video watched to the end, a page with an audible ad, or a chat page that played
    one ping.
- **Failure scenarios:**
  1. Paused while viewed:
     1. The user plays YouTube Music in an unpinned tab, switches to work, and pauses with the media key.
     2. Later they click the tab to look at the queue (silent, so `kPlayedAudio` is cleared) and switch away.
     3. They use 5 other tabs, and the player tab freezes.
     4. The Play key reaches a frozen page and waits up to 60 s for the next 5 s unfreeze window. The author
        confirmed that nothing in `content/browser/media/session` unfreezes the page.
  2. Exempt indefinitely:
     1. The user watches a talk on YouTube in the foreground until it ends, then never returns to that tab.
     2. The tab is never infinite-tabs frozen. Memory Saver (4 h) eventually discards it, but only if Memory
        Saver is on and the tab isn't pinned, so it runs in the background for hours. That is the heaviest kind
        of tab the 5-tab budget is meant to cap.
- **Fix:**
  - Don't clear the reason when the tab is shown.
  - Clear it when the main frame commits a new document, because the player is gone then (the policy already
    observes `OnMainFrameUrlChanged`).
  - Clear it after a long timeout since the tab was last audible, for example 1 h, kept in a timer like
    `recently_audible_timer`.
  - Add a unit-style check, or at least spell out the exact rule in the docs. "Since they were last shown" is
    what the code does, but it isn't what users or the media-key rationale expect.

## Nits

- **N1. The restart test can't fail if restored tabs are lost.** `scripts/e2e/power.mjs:178-181` only reads the
  load counters. `r2`, `r3` and `s` are 1 from their first load, so they read "1x" whether the restart kept them
  unloaded or dropped them, including the whole second Space. Round 1 asked for "a tab is 2 after it is
  activated". Do that after the restart: activate `r2` and the Space's `s` (Ctrl+Alt+Right) and assert both read 2.
  Then the Spaces claim in `docs/PERFORMANCE.md:101-102` is really covered.
- **N2. The energy-saver indicator isn't visible in Koan's default layout.** `docs/PERFORMANCE.md:46-47` says
  "while it is on, a leaf button in the toolbar says so". `BatterySaverButton` lives in `ToolbarView`
  (`toolbar_view.cc:550`), and the default single-toolbar layout (`koan.single_toolbar` = true,
  `src/browser/koan_prefs.cc:16`) keeps the toolbar slid out of the window. A MacBook user below 20 % gets capped
  frame rates and battery-saver freezing (S1) with no visible sign. Say so in the docs, or note it as a follow-up
  for the sidebar.
- **N3. A memory measurement now runs every minute for nothing.** `FreezingPolicy` adds `kMemorySummary` to its
  1-minute resource query whenever `kMemoryMeasurementInFreezingPolicy` is on (`freezing_policy.cc:247-264`). Its
  only consumer is the growing-PMF discard that Koan now disables
  (`DiscardFrozenPagesWithGrowingMemoryOnMemoryMeasurement`, `:1069-1182`). It is not a regression from
  upstream, but in a power PR it is free to also
  `DisableFeature(kMemoryMeasurementInFreezingPolicy)` next to the line at
  `src/browser/koan_feature_overrides.cc:33`. Keep `kCPUMeasurementInFreezingPolicy`, which battery-saver freezing
  needs.
- **N4. A policy-managed freeze switch looks changeable.** `docs/PERFORMANCE.md:87` says the
  `BackgroundTabFreezingEnabled` policy can manage the pref. `KoanSettingsHandler::Settings()`
  (`koan_settings_handler.cc:256`) sends no managed state, so the checkbox is enabled. A click is rejected by
  `IsUserModifiable()`, and `koan_settings.js:57-60` silently flips it back. This is the first row bound to a
  policy-controllable pref. Send `pref->IsManaged()` and disable the control, or leave a comment saying it is
  accepted.
- **N5. `docs/REBASE.md` says every patch calls into `//koan`; the freezing patches don't.** `REBASE.md:7-8`. The
  four `components/performance_manager/...freezing...` patches carry ~25 lines of logic and two enum values inside
  a component. That is justified, because `components/` can't depend on `//koan/browser`. Add to the "Freezing
  exemptions" row (`REBASE.md:53`):
  - these patches are logic and must be re-ported by hand;
  - `CannotFreezeReason` must still not be persisted to a histogram, UKM or mojom enum. If upstream starts
    recording it, Koan's appended values become out-of-range buckets.

## To check on the Mac (not findings; can't be verified on Linux)

- A Glance card is a `TYPE_CONTROL` widget (`koan_glance_controller.cc:273-275`), which on macOS is its own child
  NSWindow. The window monitor filters on `[event window] == browser window`, so the monitor sees no pointer
  events over the card. Confirm that moving from a revealed toolbar or peeked sidebar straight onto an open Glance
  card still hides them. That needs the browser window's tracking area to deliver `mouseExited`.
