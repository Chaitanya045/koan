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
| Energy saver | When the system saves power | Below 20 % battery | None (Firefox has no energy saver) |
| Freeze background tabs | On: all but the 5 most recently used | Only CPU-heavy tabs, with energy saver on | None; Firefox throttles background timers |
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

### Energy saver

Energy saver caps frame rates and throttles background work harder.
Chromium's default turns it on below 20 % battery, but Linux has no battery
level provider in Chromium (`HAS_BATTERY_LEVEL_PROVIDER_IMPL` is Windows and
macOS only), so on Linux it never turns on. Linux does report "on battery"
whenever the desktop's power-saver profile is on
(`PowerMonitorDeviceSourceLinux`, power-profiles-daemon through the XDG
PowerProfileMonitor portal). Koan's default, "turn on when unplugged", is
therefore "turn on when the system saves power", which GNOME and KDE switch
on at low battery. Change it in Settings > Performance.

### Freezing background tabs

Chromium's `InfiniteTabsFreezing` freezes every background tab beyond the 5
most recently used. A frozen tab runs nothing, not even its workers; it
runs for 5 s each minute to catch up (notifications, connections). Tabs that
play or recently played audio, capture, use WebRTC, USB/Bluetooth/HID/serial
devices, hold Web Locks or IndexedDB locks, have notification permission or
are loading are never frozen. It is an upstream experiment, off by default
in Chromium; Koan turns it on (`koan_feature_overrides.cc`) and adds a
switch, `chrome://flags/#koan-freeze-background-tabs`.

Zen has nothing like it: Firefox only throttles background timers.

### Restoring tabs on demand

After a restart (Koan restores the last session, like Zen) or when you
reopen a closed window, only the tab you see loads; the others load when you
first open them. Chromium loads at least 4 and up to 20 background tabs at
startup (`BackgroundTabLoadingPolicy`). Zen, like Firefox, restores on
demand. Feature `KoanRestoreTabsOnDemand` (`chrome://flags/#koan-restore-tabs-on-demand`),
hook in `background_tab_loading_policy.cc`.

This also covers Spaces: tabs in Spaces you don't open after a restart never
load.

## Koan's own views

Koan's UI is native Views, so the questions are: does it paint, lay out or
wake up when nothing changes, and do its animations run on the compositor.

- **No idle timers.** The compact-mode toolbar, the window-button strip
  (single toolbar) and a sidebar opened with Ctrl+Alt+S used to check the
  pointer every 100 ms for as long as they were out, even with the pointer
  resting on them. They now watch pointer events while they are out and hide
  400 ms after the pointer leaves; nothing runs while the pointer rests or
  while they are hidden. (Only while a menu from the revealed toolbar is
  open does Koan look again every 400 ms, because closing a menu sends the
  window no event.)
- **Compositor animations only.** Every Koan animation is a layer opacity
  or transform animation run by the compositor: the compact toolbar slide,
  the floating URL bar fade, the window buttons' fade, Glance's scale and
  fade. None of them repaints or lays out views per frame. The compact
  sidebar's slide is upstream's vertical tab strip expand-on-hover
  animation.

## Measurements (dev build)

`scripts/perf/profile_ui.mjs` (run with `scripts/e2e/run.sh
scripts/perf/profile_ui.mjs`) traces the browser off-screen while it idles,
loads pages, toggles compact mode and runs busy background tabs, and prints
view paints and layouts by class, browser main-thread tasks by origin and
CPU time by process type. `scripts/e2e/power.mjs` checks the defaults and
behaviour above.

MEASUREMENTS
