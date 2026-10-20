# Performance and power

What Koan does to stay lighter than Zen without looking any different, why,
and what Zen does instead. Each choice is a Chromium mechanism Koan turns on
or a default it changes, or a fix in Koan's own views. They are all backed by
a measurement below or by the upstream code that implements them.

Release-build numbers (startup, memory with N tabs, Speedometer, Koan vs
Zen vs Chromium) come from the benchmark harness (#14) and the release
configuration (#13); they will be in `docs/BENCHMARKS.md`. The numbers here
come from the dev build (`out/Koan`: component build, DCHECKs on), so only
comparisons within it mean anything.

## Defaults

| What | Koan | Chromium | Zen (1.23.3b, Linux) |
|---|---|---|---|
| Memory Saver | On (balanced: a tab hidden for 4 h is discarded until opened) | Off | No time-based unloading: Firefox unloads tabs only on low memory, and that is off on Linux (`browser.tabs.unloadOnLowMemory`). Zen's settings list `zen.tab-unloader.timeout-minutes` (10), but nothing reads it any more. |
| Energy saver | Chromium's default (never turns on on Linux, see below) | Below 20 % battery | None (Firefox has no energy saver) |
| Freeze background tabs | On: all but the 5 most recently used, never pinned tabs or tabs that played audio | Only CPU-heavy tabs, with energy saver on | None; Firefox throttles background timers |
| Restored background tabs | Load when first opened | 4 to 20 load at startup | Load when first opened (`browser.sessionstore.restore_on_demand`, pinned tabs too) |
| Background timer throttling | Chromium's (1 wake-up/s, then 1/min after 5 min hidden) | Same | Firefox's budget-based throttling |
| Hardware video decoding | Chromium's (VA-API on by default on Linux) | Same | On (VA-API) |

### Memory Saver

Chromium's Memory Saver discards a tab that has been hidden for 4 hours
(balanced setting; 2 h with "maximum", 6 h with "moderate") and reloads it
when you open it again. Tabs in other Spaces are hidden tabs, so a Space you
haven't visited in a while gives its memory back. Pinned tabs (Koan's
Essentials), tabs that play audio, capture, have notification permission,
show a form you've typed into or update their title in the background, and
sites on the "always keep active" list are never discarded. Change it in
Settings > Performance.

Implementation: `koan::prefs::RegisterLocalStatePrefDefaults()` changes the
default of `performance_tuning.high_efficiency_mode.state` (a hook in
`chrome/browser/prefs/browser_prefs.cc`). A value set in Settings wins.

### Energy saver: Chromium's default

Energy saver caps frame rates and throttles background work harder.
Chromium's default turns it on below 20 % battery, but Linux has no battery
level provider in Chromium (`HAS_BATTERY_LEVEL_PROVIDER_IMPL` is Windows and
macOS only), so on Linux it never turns on, which matches Zen. Linux does
report "on battery" while the desktop's power-saver profile is on
(`PowerMonitorDeviceSourceLinux`, power-profiles-daemon through the XDG
PowerProfileMonitor portal), so "turn on when unplugged" would follow the
system's power saver. Koan doesn't make that the default: Settings only
shows energy saver on a device with a battery (`DeviceHasBattery()`, false
on Linux), and its toolbar button sits in Koan's hidden toolbar, so nobody
could turn it back off. Revisit once Koan's settings page (#39) can show the
switch.

### Freezing background tabs

Chromium's `InfiniteTabsFreezing` freezes background tabs beyond the 5 most
recently used. A frozen tab runs nothing, not even its workers; it runs for
5 s each minute to catch up (notifications, connections). There is no grace
period: a tab you left a moment ago is unfrozen only because it is among the
5 most recently used (upstream ignores "recently visible" for this kind of
freezing).

Never frozen:
- pinned tabs (Koan's Essentials), e.g. a chat app without notification
  permission;
- tabs that played audio since they were last shown, so a paused player
  still answers media keys (upstream only protects audio for 5 minutes);
- tabs that capture, use WebRTC, USB/Bluetooth/HID/serial devices, hold Web
  Locks or IndexedDB locks, have notification permission or are loading;
- sites on Settings > Performance > "Always keep these sites active" (the
  same list exempts them from Memory Saver; upstream
  `kFreezingFollowsDiscardOptOut`).

The first two are Koan's: upstream has no such exemptions. They are a hook in
`freezing_policy.cc` (two `CannotFreezeReason`s that only block this kind of
freezing; Chromium's battery-saver freezing is unchanged).

Upstream also discards a frozen tab whose memory grows by more than 100 MB,
whatever the Memory Saver setting. Koan turns that off
(`kDiscardFrozenBrowsingInstancesWithGrowingPMF`), so Memory Saver remains
the one switch for discarding tabs.

Freezing is an upstream experiment, off by default in Chromium; Koan turns
it on (`koan_feature_overrides.cc`) with a switch,
`chrome://flags/#koan-freeze-background-tabs`. Zen has nothing like it:
Firefox only throttles background timers.

### Restoring tabs on demand

After a restart (Koan restores the last session, like Zen) or when you
reopen a closed window, only the tab you see loads; the others load when you
first open them. Chromium loads at least 4 and up to 20 background tabs at
startup (`BackgroundTabLoadingPolicy`). Zen, like Firefox, restores on
demand. Feature `KoanRestoreTabsOnDemand` (`chrome://flags/#koan-restore-tabs-on-demand`),
hook in `background_tab_loading_policy.cc`.

This also covers Spaces: tabs in a Space you don't open after a restart
don't load (tested in `scripts/e2e/power.mjs`).

## Koan's own views

Koan's UI is native Views, so the questions are: does it paint, lay out or
wake up when nothing changes, and do its animations run on the compositor.

- **No polling timers.** The compact-mode toolbar, the window-button strip
  (single toolbar) and a sidebar opened with Ctrl+Alt+S used to check the
  pointer every 100 ms for as long as they were out, including while the
  pointer rested on them. They now watch pointer events while they are out
  and hide 400 ms after the pointer leaves, with a one-shot timer. (Only
  while a menu from the revealed toolbar is open does Koan look again every
  400 ms, because closing a menu sends the window no event.) Measured for a
  Ctrl+Alt+S peek that hides by itself: PEEK_TASKS Koan task(s) in 6 s; the
  100 ms timer meant about 34 for the same peek (3 s grace plus the 400 ms
  delay; computed from the old code, not measured). The idle rows below
  don't measure this: the old timers also stopped once everything was
  hidden. The case the change matters most for, a pointer resting on the
  revealed toolbar, can't be produced off-screen (the headless platform
  never moves the real cursor).
- **The hidden toolbar doesn't paint.** In single toolbar (the default)
  and compact mode the toolbar is slid out of the window, but it was still
  repainted on every URL, title and load change (the location bar 40 times
  in 10 s on a page that changes its title 4 times a second). Its layer is
  now hidden while it is out of the window, so the compositor skips it; the
  views stay visible, so Ctrl+L still focuses the omnibox.
- **Compositor animations only.** Every Koan animation is a layer opacity
  or transform animation run by the compositor: the compact toolbar slide,
  the floating URL bar fade, the window buttons' fade, Glance's scale and
  fade. None of them repaints or lays out views per frame. The compact
  sidebar's slide is upstream's vertical tab strip expand-on-hover
  animation, which animates the strip's width: it lays out and repaints the
  strip (Koan's URL pill and Space switcher included) every frame, about
  320 paints for one peek in and out. Making it a transform would mean
  replacing upstream's animation; left as is.

## Measurements (dev build)

`scripts/perf/profile_ui.mjs` (run with `scripts/e2e/run.sh
scripts/perf/profile_ui.mjs`) traces the browser off-screen while it idles,
loads pages, toggles compact mode and runs busy background tabs, and prints
view paints and layouts by class, browser main-thread tasks by origin and
CPU time by process type. `scripts/e2e/power.mjs` checks the defaults and
behaviour above.

All numbers: dev build, off-screen (headless Ozone, software compositing),
8-core machine shared with other builds, so only the comparisons matter.

View paints (`View::OnPaint`) and layouts in the browser process, before
and after hiding the slid-away toolbar's layer:

| Scenario | Paints before → after | Layouts | Notes |
|---|---|---|---|
| Idle 10 s, static page, default layout | 0 → 0 | 0 | No Koan task ran. |
| Idle 10 s, compact mode | 0 → 0 | 0 | No Koan task ran. |
| Active tab changes its title 4×/s, 10 s | 200 → 164 | ~1900 | The hidden location bar painted 40 times; now 0. The rest are the tab (title) in the sidebar. |
| 5 page loads | 154 → 117 | ~2900 | The hidden toolbar, reload and avatar buttons repainted on each load; now 0. |
| Peek the compact sidebar (Ctrl+Alt+S) until it hides | 321 | ~1900 | Upstream's width animation (see above). |

Browser CPU for these varied between runs by 2× on the shared machine, so
it isn't reported; the paint counts repeated within a few paints across
runs.

Busy background tabs: 8 tabs, each with a dedicated worker doing 20 ms of
work every 100 ms (worker timers aren't throttled in the background), CPU
over 20 s starting 5 s after they opened:

| | Renderer CPU |
|---|---|
| Freezing off (`--disable-features=InfiniteTabsFreezing`, Chromium's default) | 32.1 s |
| Koan (freezing on) | 18.4 s |

In `scripts/e2e/power.mjs` (same tabs, 15 s), with freezing on 5 of the 8
tabs paused for several seconds (7.3–14.9 s and 7.5–17.7 s in two runs),
i.e. from when they froze until the test opened them; the others count among
the 5 most recently used tabs, which stay unfrozen. The test asserts at least
3, since at most 5 of the 8 can be among the most recently used. With
freezing off no tab paused for more than 0.8 s. A pinned busy tab, the least
recently used, never pauses (PINNED_GAP).

Reopening a closed window with 4 tabs (`scripts/e2e/power.mjs`): with
upstream's behaviour (`--disable-features=KoanRestoreTabsOnDemand`) all
four load again; with Koan's, only the active one does, and a background
tab loads when it is opened. The test also restarts Koan with a window of
3 tabs plus one in another Space: only the active tab loads.
