# Review: PR #47 "Performance and power: restore on demand, Memory Saver, tab freezing, no hidden repaints or polling" (Fixes #45)

Branch: `perf-power` (6 commits on top of `origin/develop`, 24 files, +886/-51).
I checked the PR against pristine Chromium 158.0.8111.0 (`/home/chaitanya/work/koan/chromium/src` HEAD
67e29b8) and Zen 1.23.3b (`zen-src` plus `defaults/preferences/firefox.js` taken from Zen's `omni.ja`).
I built and ran nothing. I applied all four hook patches with `git apply --check` to pristine copies in a
temp dir, and they apply cleanly. Their `index` blob hashes match HEAD.

What I verified and found correct:
- `kInfiniteTabsFreezing` exists. It is `FEATURE_DISABLED_BY_DEFAULT`. Its params are num_protected_tabs=5,
  unfreeze_interval=1 min and unfreeze_duration=5 s.
- Enabling it through `koan::RegisterFeatureOverrides` takes effect. That hook runs during
  FeatureList setup (`ChromeBrowserFieldTrials::RegisterFeatureOverrides`), and the feature is only read
  at runtime in `freezing_policy.cc:510,1296`. A command-line `--disable-features` still wins, so the A/B
  runs are valid.
- `kKoanRestoreTabsOnDemand` is read in the `BackgroundTabLoadingPolicy` constructor. That constructor
  runs from `ChromeBrowserMainExtraPartsPerformanceManager::PostCreateThreads`, which is after FeatureList
  init.
- With min/max = 0, `ShouldLoad()` is always false, so `LoadNextTab()` drops every queued tab.
- Tabs still load when shown. `WebContentsImpl` calls `GetController().SetActive(page_visibility !=
  kHidden)`, and that calls `LoadIfNecessary()` when `needs_reload_` is set. This covers split view (both
  panes are visible). An occluded window loads when it is uncovered. No tab is lost: unloaded restored tabs
  keep their navigation entries, and the policy still fetches their favicons.
- Zen really does restore pinned tabs on demand. `firefox.js` sets
  `browser.sessionstore.restore_pinned_tabs_on_demand` to false at line 529, and Zen overrides it to true at
  line 1570.
- `SetDefaultPrefValue` on `performance_tuning.high_efficiency_mode.state` (2 = kEnabled) and
  `performance_tuning.battery_saver_mode.state` (2 = kEnabledOnBattery) runs after upstream registers
  them, so a value the user sets still wins.
- `MigrateMemorySaverModePref` only migrates an old bool that the user set explicitly, so it does not
  undo the new default.
- The medium (balanced) Memory Saver setting is 4 h (`memory_saver_mode_policy.cc:225`). Memory Saver
  never discards pinned tabs (`discard_eligibility_policy.cc:411-414`).
- `PowerMonitorDeviceSourceLinux` reports `kBatteryPower` exactly when the XDG PowerProfileMonitor
  `power-saver-enabled` is true. `HAS_BATTERY_LEVEL_PROVIDER_IMPL` is `is_win || is_mac`. The doc's
  Linux energy-saver reasoning is correct apart from the Settings claim in S1.
- Removing an `EventMonitor` from inside its own `OnEvent` is safe. `EventObserverAdapter::OnEvent` touches
  no members after calling the observer, and `ui::EventHandler`'s destructor detaches it from an
  in-flight dispatch. `WindowMonitorAura` tears itself down if the window dies first.
- `HideAwayToolbarLayer` is bound through `weak_factory_` and checks the mode and away state again, so a
  late `OnEnded` is harmless.
- A Glance card is a `TYPE_CONTROL` child of the browser's native view, so the window pre-target monitor
  receives pointer events over it.

---

## Blockers

### B1. Freezing is now on by default and freezes pinned (Essentials) tabs; Chromium has no pinned exemption for freezing
- **Where:** `src/browser/koan_feature_overrides.cc:18-22`, which enables `kInfiniteTabsFreezing`.
  Docs: `docs/PERFORMANCE.md:52-61`, `docs/USAGE.md:118-121`, and the flag text at
  `src/browser/koan_flag_entries.h:35-41`.
- **What's wrong:** Upstream `FreezingPolicy` has no `CannotFreezeReason` for pinned tabs. Its reasons
  (`freezing_policy.cc:612-1000`) are visible, recently visible, audible, recently audible, opted out,
  WebRTC, notification permission, web lock, IndexedDB lock, loading, USB/BT/HID/serial, and capture.
  Discarding does protect pinned tabs ("Pinning a tab is a strong signal the user wants to keep it",
  `discard_eligibility_policy.cc:411`). Freezing does not. Koan's Essentials are pinned tabs
  (`koan_workspaces.h:26`). Zen never freezes any tab. The PR text lists the exemptions and leaves out
  pinned tabs, so this gap is not called out anywhere.
- **Failure scenario:**
  1. The user keeps WhatsApp Web, Discord or a Slack workspace as an Essential and has not granted it
     notification permission (common).
  2. They use 5 other tabs and the Essential stays hidden for more than 5 minutes.
  3. The Essential freezes. It runs for only 5 s a minute, so its unread count in the title or favicon
     and its socket keep-alives stall for up to 60 s at a time.
  4. Then `kDiscardFrozenBrowsingInstancesWithGrowingPMF` (enabled by default, `features.cc`) can
     discard a frozen tab whose footprint grows more than 100 MB. Pinned tabs are still protected from
     that discard by eligibility, but other tabs are not; see S4.
- **Same gap, uncertain:** a paused media tab (YouTube Music, a podcast) loses its "recently audible"
  protection after 5 min and freezes. Hardware or MPRIS Play then goes to a frozen renderer. I did not
  confirm whether the media-session action unfreezes it or waits for the next 5 s window.
- **Fix, either:**
  - (a) Don't enable `kInfiniteTabsFreezing` by default yet. Keep the flag and document it as opt-in.
  - (b) Add a small hook. `FreezingPolicy` is already a `PageLiveStateObserver`. Override
    `OnIsPinnedTabChanged`, and in `OnPageNodeAdded` add a new `CannotFreezeReason::kPinnedTab` (in
    `public/freezing/cannot_freeze_reason.h`, mapped as blocking `FreezingType::kInfiniteTabs`) when
    `PageLiveStateDecorator::Data::IsPinnedTab()` is true. Then add a pinned-tab check to
    `scripts/e2e/power.mjs`: pin one busy tab and assert it never pauses for more than 3 s.
- **Docs, in either case:** say that Settings > Performance > "Always keep these sites active" also exempts
  a site from freezing. That works through `kFreezingFollowsDiscardOptOut` (on by default) and
  `FreezingOptOutChecker`, and it is the user's per-site escape hatch.

## Should fix

### S1. On Linux, energy saver's Settings section is hidden, so the new default can't be changed and the docs are wrong
- **Where:** `src/browser/koan_prefs.cc:31-37`. Docs: `docs/PERFORMANCE.md:50` ("Change it in Settings >
  Performance"), `docs/USAGE.md:122-123`, and the PR body ("Memory Saver and energy saver stay
  user-changeable in Settings > Performance").
- **What's wrong:**
  - The Performance page shows the energy-saver section only when `showBatterySettings_` is true
    (`performance_page_index.html.ts:18`). That flag comes from `BatterySaverModeManager::DeviceHasBattery()`
    (`performance_handler.cc:121`).
  - On Linux `base::BatteryStateSampler::Get()` is null because there is no battery level provider, so
    `has_battery_` stays false (`battery_saver_mode_manager.cc:217-223,337`). Settings therefore never shows
    the toggle.
  - The only other control is the toolbar `BatterySaverButton` ("turn off for now" bubble). It lives in
    `ToolbarView` (`toolbar_view.cc:550`), and Koan's default single-toolbar layout keeps the toolbar slid
    out of the window.
  - The e2e test only reads the pref through `settingsPrivate`. It does not check that the user can see or
    change it.
- **Failure scenario:** A desktop user picks GNOME's "Power Saver" profile to keep the fans quiet. Koan
  silently caps frame rates and freezes CPU-heavy background tabs. There is no indicator and no way to turn
  it off (short of `--force-device-has-battery`, `battery_saver_mode_manager.cc:498`), and the docs send
  them to a setting that isn't there.
- **Fix, either:**
  - Keep upstream's default on Linux.
  - Or keep the change but correct the docs: on Linux it can't be changed in Settings, the indicator is in
    the hidden toolbar, and here is the escape hatch. Better still, add a `chrome://flags` or Koan pref
    switch for it.

### S2. "Fixes #45" will close the issue while most of its checklist is undone
- **Where:** the PR body. Issue #45 lists:
  - measurements vs Zen and Chromium, release build;
  - release build flags;
  - `docs/BENCHMARKS.md`;
  - discarding the tabs of inactive Spaces.
- **What's wrong:** The PR's own "Not in this PR" section defers the first three. Spaces are only covered
  indirectly, after 4 h of Memory Saver. GitHub will auto-close #45 on merge.
- **Fix:** Use "Part of #45", or split the remaining items into a follow-up issue and link it.

### S3. The "no polling timers" claim isn't backed by the reported numbers
- **Where:** the PR body ("Idle, default layout and compact mode: 0 paints, 0 layouts, no Koan tasks") and
  `docs/PERFORMANCE.md:82-89,123-124`.
- **What's wrong:**
  - The old 100 ms repeating timers ran only while the toolbar, caption strip or peeked sidebar was out
    (develop's `SetToolbarRevealed` stopped `toolbar_hide_timer_` when hiding). So the idle 0 → 0 rows
    would read the same before and after; they measure nothing about this change.
  - The case the change targets (pointer resting on a revealed element) is admitted to be unmeasured.
  - There is a measurable case that went unreported: `profile_ui.mjs`'s `compact` scenario (Ctrl+Alt+S,
    then 6 s) should show about 34 `CheckSidebarPeek` timer tasks on develop and 1 one-shot after this PR,
    via the "browser main-thread tasks by origin" output.
- **Failure scenario:** none at runtime. The evidence is weak while the PR swaps simple, robust polling for
  event-driven logic. The new logic only works if every way the pointer can leave produces an Aura mouse
  event. Examples: the pointer leaving through the top edge on X11 or Wayland, a window resized by a
  caption-button click under a still pointer, and drag-and-drop loops that deliver no mouse events. I
  reasoned through these and think they are fine, but they are not tested.
- **Fix:**
  - Report develop-vs-PR task counts for the peek scenario, or drop the idle rows as evidence for this
    item.
  - Optionally add a check to `shortcuts.mjs` that no `koan_compact_mode_controller.cc` tasks run in the
    trace after the peek has hidden.

### S4. Freezing also turns on frozen-tab discarding for every background tab, regardless of the Memory Saver setting, and the docs don't say so
- **Where:** `src/browser/koan_feature_overrides.cc:22`, `docs/PERFORMANCE.md:52-61`, `docs/USAGE.md:113-121`.
- **What's wrong:**
  - `kDiscardFrozenBrowsingInstancesWithGrowingPMF` is enabled by default. A browsing instance whose pages
    are all frozen and whose private footprint grows more than 100 MB after freezing is discarded
    (`freezing_policy.cc:1110-1162`, `FreezingDiscarder` → `FROZEN_WITH_GROWING_MEMORY`). This is
    independent of the Memory Saver pref.
  - Per the upstream comment at `freezing_policy.cc:1124-1133`, a missing post-freeze measurement is
    treated as 0, so any frozen origin over 100 MB is discarded.
  - Upstream this path was only reachable for tabs frozen by voting or battery saver. With Koan's default
    it covers every background tab beyond the 5 most recently used.
- **Failure scenario:** A user turns Memory Saver off in Settings to stop tab reloads. A heavy background
  tab, such as a large Google Sheet or Figma file, still gets discarded and reloads when they open it. The
  docs tell them Memory Saver is what controls this.
- **Fix:** Document it next to freezing, saying the per-site exemption list prevents it.
  Or, if Koan wants Memory Saver to be the only discard control, disable
  `kDiscardFrozenBrowsingInstancesWithGrowingPMF` in `RegisterFeatureOverrides`.

### S5. The restart-restore path is claimed but not tested
- **Where:** `scripts/e2e/power.mjs:56-89`. Claims: `docs/PERFORMANCE.md:67-75` ("After a restart ... This
  also covers Spaces ... never load") and the PR table "(restart, reopened window)".
- **What's wrong:** The test uses Ctrl+Shift+W then Ctrl+Shift+T, which goes through
  `BrowserLiveTabContext::AddRestoredTab` → `ScheduleLoadForRestoredTabs({wc})`. Restart restore is a
  different caller (`SessionRestoreDelegate::RestoreTabs`, `session_restore_delegate.cc:102`), and the Spaces
  claim depends on Koan's tab-group focus mode, which has its own visibility handling. Neither is exercised.
  This is low risk, since the same policy gate applies, but the docs' headline claim is untested.
- **Fix:** Add a restart case:
  1. Close the browser with 1 visible tab, 3 background tabs, and 1 tab in a second Space.
  2. Relaunch with the same `--user-data-dir`.
  3. Assert the `loads:*` counters: the active tab is 2, every other tab is 1, and a tab is 2 after it is
     activated.
  `run.sh` currently deletes the profile on exit, so the restart would have to happen inside the script.

## Nits

- **N1. Numbers don't match each other.** The PR body says frozen tabs "paused for 7–17 s"; the doc
  (`docs/PERFORMANCE.md:142`) says 7–15 s. The doc says "5 of the 8 tabs" froze, but the test asserts only
  `frozen >= 3` (`power.mjs:108`). Pick one set of numbers and state the threshold rationale. Also
  `docs/PERFORMANCE.md:129-130` calls paint counts "deterministic", which is stronger than three runs on a
  shared machine support.
- **N2. `KOAN_FLAGS` can silently drop a required flag.** In `scripts/e2e/run.sh:41`,
  `--enable-features=ui-debug-tools-enable-synthetic-events ${KOAN_FLAGS:-}` is followed by any
  `--enable-features=...` from `KOAN_FLAGS`. The later switch replaces the earlier one (last value wins),
  so synthetic UI-devtools events turn off and unrelated checks fail. Merge the feature lists, or document
  that only `--disable-features` is safe there.
- **N3. Stale comment.** `src/browser/ui/views/koan_compact_mode_controller.cc:261-262` still says "the
  hide timer keeps it revealed until focus leaves". It is now the focus-change re-check plus a one-shot
  timer.
- **N4. The fragile half of the layer-hide fix is untested.** `power.mjs:27-36` only checks the steady
  single-toolbar state, where the layer is hidden by the non-animated `ApplyToolbarTransform(false)`. The
  part most likely to regress is the hide after the slide animation's `OnEnded` (`:679-681,692-700`). If an
  upstream change or preemption aborts that animation, the layer stays visible and the paints silently come
  back. Add a check that reveals the toolbar (focus something in it, e.g. F6 or Alt+Shift+T), waits for it
  to hide, then asserts 0 `ToolbarView`/`LocationBarView` paints.
- **N5. Layer visibility ignores ancestor views.** `koan_compact_mode_controller.cc:347,667` call
  `layer->SetVisible(top_container->GetVisible())`, which ignores ancestor visibility. Views'
  `UpdateLayerVisibility` folds in the visibility of ancestors up to the nearest layer, e.g.
  `TopContainerOverlayView`. This is harmless today because the overlay is always visible while a mode is
  active, but `top_container->GetVisible() && top_container->parent()->GetVisible()` (or `IsDrawn()`) would
  match upstream semantics.
- **N6. Patch formatting differs.** The new or regenerated patches have bare `@@ -a,b +c,d @@` hunk headers
  without the function context that the previous `prefs/BUILD.gn.patch` on develop had. That points to a
  different diff invocation; it is cosmetic, and `check_patches.sh` passes.
- **N7. Upstream tests would now fail.** With `kKoanRestoreTabsOnDemand` enabled by default,
  `background_tab_loading_policy_unittest.cc`, the session-restore browser tests and the
  `tab_restore_browsertest.cc:1632` expectations break if anyone runs them. They are not run today; worth a
  line in `docs/REBASE.md`.
- **N8. Unverified claim about GNOME and KDE (uncertain).** `docs/PERFORMANCE.md:49-50` and
  `docs/USAGE.md:123` say "GNOME and KDE switch it on at low battery". That depends on GNOME's "Automatic
  Power Saver" setting and KDE PowerDevil's per-state profile, which I couldn't check here. Soften to
  "desktops can switch it on at low battery".
- **N9. Freezing docs miss the 5-minute grace.** `docs/PERFORMANCE.md:54-55` and the flag text say "every
  background tab beyond the 5 most recently used". Upstream also protects a tab for 5 min after it was last
  visible (`kFreezingVisibleProtectionTime`, `CannotFreezeReason::kRecentlyVisible`). Mention it: users will
  see tabs keep running for 5 min, and it explains why the e2e test uses tabs opened in the background.
- **N10. PR test results not independently confirmed.** The only build log on disk
  (`.cache/agent-build-perf-power.log`) shows the build finishing but no e2e output, so I could not confirm
  the reported "all pass" run.
