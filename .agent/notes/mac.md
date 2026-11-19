# macOS

Facts and rules for Koan's Mac build (issue #51). The Mac is the
maintainer's daily laptop. No Linux builds or e2e (2026-11-03); Linux is only
for editing and `clang -fsyntax-only`.

## Consent first

**Every build, test run, gclient/gn command or script on the Mac needs the
maintainer's explicit OK first.** Without it, only quick read-only commands
are allowed over SSH: `ls`, `cat`, `grep`, `sw_vers`, `which`,
`bash --version`, `git ls-tree`/`git status`, and parse-only checks such as
`/bin/bash -n < script`. Nothing that writes, nothing long-running.

## Machine

- MacBook Air, Apple M4 (arm64), 10 cores (4 performance + 6 efficiency),
  no fan, 16 GB RAM, 256 GB SSD (~167 GB free on 2026-11-20).
- macOS 27.0. Xcode 27.1 at `/Applications/Xcode.app` (SDK
  `MacOSX27.0.sdk`, which is Chromium 158's `mac_sdk_official_version`;
  `actool` 27.1). Since Xcode 26 the **Metal toolchain** is a separate
  component that ANGLE's Metal shaders need (`xcrun metal`); without it the
  build stops at `angle_metal_internal_shaders_to_air` with "missing Metal
  Toolchain". Check with `xcodebuild -showComponent MetalToolchain`
  (read-only) and install it once with `xcodebuild -downloadComponent
  MetalToolchain`; not yet checked on this Mac. xcrun can't find the toolchain under the
  `CFFIXED_USER_HOME` that the scripts set (`xcodebuild -find metallib`
  fails), so `build.sh` points ANGLE at it directly
  (`angle_metal_toolchain_dir`, through `out/Koan/metal-toolchain`; see
  .agent/notes/macos-ci.md). The system-wide `xcode-select -p` is the Command Line
  Tools, so builds must set `DEVELOPER_DIR`; the scripts default it to
  Xcode.app.
- Tools: `/bin/bash` is 3.2; Homebrew bash 5.3 is at `/opt/homebrew/bin/bash`
  but not on a non-interactive SSH `PATH`, so the scripts stay bash 3.2
  compatible. `python3` is `/usr/bin/python3`, 3.9 (depot_tools has no
  `python3` on `PATH`, only `vpython3`), so `scripts/branding.py` stays 3.9
  compatible. `iconutil`, `sips`, `actool` exist; `rsvg-convert` and
  ImageMagick don't, and aren't needed: the PNGs the Mac uses are rendered
  on Linux and committed (`branding/mac/rendered/`, refreshed with
  `apply_branding.sh --render-mac` after an SVG changes).
- SSH: `ssh -i ~/.ssh/<ssh-key> <user>@<mac-host>`.

## One folder

Everything lives in `~/work/koan-mac`:

```
~/work/koan-mac/
├── env.sh        source before any Koan command
├── fetch.sh      first fetch: clone, depot_tools, pinned Chromium, hooks
├── home/         $HOME: .vpython-root, .cipd-cache, .cache (siso), ...
├── tmp/          $TMPDIR
├── logs/         fetch.log, build logs
└── koan/         the Koan clone
    ├── depot_tools/
    └── chromium/src   Chromium 158.0.8127.2, target_os ["mac"]
```

`env.sh` points `HOME`, `TMPDIR`, `XDG_CACHE_HOME`, `XDG_CONFIG_HOME`,
`VPYTHON_VIRTUALENV_ROOT` and `CIPD_CACHE_DIR` into the folder, sets
`DEPOT_TOOLS_METRICS=0` and `DEVELOPER_DIR`, and puts depot_tools first on
`PATH`. The checkout is ~31 GB; with `out/Koan` budget ~60 GB.

**The rule is that everything stays in the folder.** Every script (and
`branding.py`) refuses to run on macOS unless `KOAN_MAC` is set and `HOME`
and `TMPDIR` are inside it (`koan_require_mac_env` in
`scripts/koan_platform.sh`). With `KOAN_MAC` unset (a friend's clone,
docs/BUILDING-MAC.md) the folder is `<clone>/.cache/mac`
(`koan_use_clone_env`), so the rule holds without an env.sh. The scripts also export
`CFFIXED_USER_HOME="$HOME"`: Apple's tools (`xcodebuild` from `gn gen`,
`actool`, `ibtoold`) find the home directory through CoreFoundation, which
ignores `HOME` but honours that.

Still outside the folder (by design or unavoidable):

- Xcode in `/Applications`.
- The daily-driver app (opened from Finder/Dock, so with the real home):
  `~/Library/Application Support/Koan` (profile), `~/Library/Caches/Koan`,
  `~/Library/Preferences/dev.koan.Koan.plist`,
  `~/Library/Saved Application State/dev.koan.Koan.savedState`, a
  code-signing clone in `/var/folders/.../dev.koan.Koan.code_sign_clone`,
  the "Koan Safe Storage" login-keychain item, and its LaunchServices
  registration.
- Possibly Apple tool caches in `/var/folders` (`xcrun`'s lookup cache,
  `DARWIN_USER_CACHE_DIR` users): not verified, since checking needs writes.

`scripts/run_mac.sh` runs a test instance inside the folder: profile and
cache in `$KOAN_MAC/profile`, `--use-mock-keychain` (no keychain item;
`KOAN_REAL_KEYCHAIN=1` to use the real one), the code-sign clone off, and
started directly so it keeps the folder's `HOME`/`CFFIXED_USER_HOME`.
The test instance does not touch the real-home preferences plist
(`~/Library/Preferences/dev.koan.Koan.plist`): that file belongs to the
daily-driver app opened from Finder. Because `CFFIXED_USER_HOME` points into
the folder, a test instance's defaults go there, not into the real home. The
plist in the real home is harmless and is left alone.

## Build

```sh
source ~/work/koan-mac/env.sh && cd ~/work/koan-mac/koan
scripts/apply_patches.sh && scripts/apply_branding.sh
scripts/build.sh               # build/koan-mac.gn -> chromium/src/out/Koan/Koan.app
scripts/run_mac.sh             # test run, profile in $KOAN_MAC/profile
```

- `build/koan-mac.gn`: arm64, optimised, not a component build, no DCHECKs,
  `symbol_level = 0`. No `is_official_build`: the official build (ThinLTO +
  PGO) is separate, in `out/KoanOfficial` (below, "Official build"). Every incremental change relinks the whole
  framework; a component `koan-mac-dev.gn` could come later for iterating
  on Mac-only code. `out/Koan` is estimated at 10-15 GB.
- `JOBS` defaults to 2, 4 or 6 by the memory free at the start (free,
  inactive, speculative and purgeable pages from `vm_stat`; same thresholds
  as the shared Linux agent build). macOS swaps rather than killing the
  build, so too many jobs only slows it down.
- `scripts/bootstrap.sh` does what `fetch.sh` did: a `.gclient` with
  `target_os = ["mac"]`, `gclient sync --nohooks --no-history --revision
  src@<CHROMIUM_VERSION rev>`, `gclient runhooks`.
- Branding on macOS copies the committed PNGs (sizes checked), builds
  `app.icns` (iconutil; 16, 32, 128, 256, 512 as Chromium does) and
  `Assets.car` (actool, from the icon bitmaps plus
  `branding/mac/AppIcon.icon` for the macOS 26+ icon) and copies them over
  `chrome/app/theme/chromium/mac/`. Any actool error, warning or notice
  fails it, except the two warnings Chromium's `compile_car.py` accepts.
  The bundle is `Koan.app`, id `dev.koan.Koan`, from `BRANDING`; Finder's
  kinds become "Koan Extension"/"Koan Shortcut" (`app-Info.plist`).
- Profile: `~/Library/Application Support/Koan` (hook in
  `chrome_paths_mac.mm`).
- Keychain: "Koan Safe Storage", account "Koan" (hook in
  `components/os_crypt/common/keychain_password_mac.mm`). Never rename it
  once a profile exists: its cookies and passwords would stop decrypting.
- Each build gets a new ad-hoc signature, so macOS asks again for the
  keychain item after every rebuild. `scripts/mac_signing_identity.sh create`
  once, and `scripts/build.sh` signs every build with one local identity
  (below, "Signing builds with one identity (#141)").

## Official build (ThinLTO + PGO)

`scripts/mac/build_official.sh` builds `chromium/src/out/KoanOfficial/Koan.app`
from `build/koan-mac-official.gn`. It is a separate out dir: `out/Koan` (the
everyday and test build) and `~/Applications/Koan.app` (the daily app) are
never touched. Why: the everyday build is `is_official_build = false`, so it
has no ThinLTO and no PGO, and Speedometer 3.1 gave 41.8 against Arc's 57
(bench2, 2026-11-19).

```sh
source ~/work/koan-mac/env.sh && cd ~/work/koan-mac/koan
scripts/mac/build_official.sh --dry-run   # the plan, changes nothing
scripts/mac/build_official.sh --check     # the Mac's checks only
scripts/mac/build_official.sh             # user at the Mac: -j 4, background QoS
scripts/mac/build_official.sh --away      # user said they're away: -j 8
```

Rerun the same command after any interruption: every step is skipped when it
is done, siso resumes, and the ThinLTO cache keeps a link's finished modules.

### What Chromium 158 does in an official build (pristine source)

- `is_official_build = true` is the switch. From it, on mac-arm64:
  - `use_thin_lto` is true (`build/config/compiler/compiler.gni`:
    `is_apple && use_lld`) and so is `thin_lto_enable_optimizations`
    (`--lto-O2` on the main binaries);
  - `chrome_pgo_phase` is 2 (`build/config/compiler/pgo/pgo.gni`, only with
    `dcheck_always_on = false`, since the profiles come from DCHECK-off
    builds);
  - `enable_stripping` and `enable_dsyms` are true
    (`build/config/apple/symbols.gni`);
  - `default_symbol_level` is 2 (full DWARF). Koan sets 0;
  - CHECK messages are compiled out (`base/check.h`, `CHECK_WILL_STREAM()`
    false), LOG(FATAL) prints no stack (`base/logging.cc`), no PCH
    (`build/config/pch.gni`), `exclude_unwind_tables`, the raw_ptr plugin is
    off, and `use_dummy_lastchange` is false (the real LASTCHANGE, so a
    Chromium version bump rebuilds a few thousand targets).
- PGO: `pgo_optimization_flags` picks the target `mac-arm` for arm64 and
  reads its profile through `tools/update_pgo_profiles.py get_profile_path`:
  `chrome/build/mac-arm.pgo.txt` names it (at 158.0.8127.2:
  `chrome-mac-arm-8127-1793742285-3b43597c…-21743ca0….profdata`) and the
  file lives in `chrome/build/pgo_profiles/`. The DEPS hook "Fetch PGO
  profiles for mac arm" downloads it from
  `gs://chromium-optimization-profiles/pgo_profiles` only when the gclient
  custom var `checkout_pgo_profiles` is true (default false). Without the
  file, gn gen fails with a message that says exactly that. The script sets
  the var in `chromium/.gclient` (a copy of the old file is kept as
  `.gclient.before-pgo`), so update_chromium.sh's syncs fetch each new
  milestone's profile, and runs only that one hook. A separate renderer
  profile (`_renderer.profdata`) is only used with
  `enable_separate_renderer_binary`, false in 158.
- Every PGO target gets `-Wno-profile-instr-unprofiled`,
  `-Wno-profile-instr-out-of-date` and `-Wno-backend-plugin`
  (crbug.com/978401): stale or missing profile data is normal, never an error.
- Chromium's own mac-arm64 official configs (`tools/mb/mb_config.pyl`:
  `official_remoteexec_mac_arm_perf_pgo`, `official_mac_arm_pgo_with_symbols`)
  are `official` (which adds `is_chrome_branded = true`), `remoteexec`,
  `no_widevine_cdm_host_verification` (`ignore_missing_widevine_signing_cert
  = true`), `minimal_symbols`, `arm64`. Koan takes the same minus branding,
  remote execution and symbols.
- `is_chrome_branded` stays false. Most "official Chrome" behaviour that
  would break Koan is gated on branding, not on official: `--load-extension`
  is ignored only under `GOOGLE_CHROME_BRANDING`
  (`extension_service.cc`), and so is the default-profile check of
  `--remote-debugging-port` (`remote_debugging_server.cc`).
- `ignore_missing_widevine_signing_cert` defaults to `!is_official_build`, so
  an official build would fail without Google's cert. Koan's placeholder
  signer ignores the cert anyway (`//koan/build/widevine_signature_placeholder.py`);
  the arg is set as Chromium's bots do. The VMP signature still comes from
  `scripts/mac/widevine_vmp_sign.sh` after the build, before code signing.
- `enable_nacl` no longer exists; `use_remoteexec` stays false (siso runs
  locally); every `v8_enable_*` stays at its default (official builds get
  `v8_logging_level = 0` and no fast mksnapshot by themselves).

### What other forks ship

- **Brave** (`brave-core` `build/commands/lib/buildArgs.ts`, `config.ts`):
  `is_official_build` for release builds; `checkout_pgo_profiles` only for
  real releases (`isBraveReleaseBuild`), otherwise `chrome_pgo_phase = 0`
  and, on Mac, `enable_dsyms = false`. It uses Chromium's profiles on its
  patched code, as Koan will.
- **ungoogled-chromium-macos** (`flags.macos.gn`): `is_official_build`,
  `use_thin_lto`, `chrome_pgo_phase = 2`, `symbol_level = 1`,
  `fatal_linker_warnings = false`. Their build moved from GitHub's runners
  (3 days, didn't finish) to bigger Depot runners (~9 h).
- **Thorium** (`other/Mac/mac_ARM_args.gn`): `is_official_build`,
  `use_thin_lto`, `thin_lto_enable_optimizations`, `chrome_pgo_phase = 2`,
  `symbol_level = 0`, `thin_lto_enable_cache = false`,
  `ignore_missing_widevine_signing_cert = true`.

### Memory: one link, four ThinLTO threads

- **Links at a time.** With ThinLTO, `build/toolchain/concurrent_links.gni`
  asserts that `concurrent_links` isn't set, and computes it as
  `(RAM - 10 GB) / 20 GB`, at least 1: **1** on 16 GB. GN writes it as the
  depth of `build_toolchain_link_pool`, which siso honours. The script reads
  the depth back after gn gen and stops if it isn't 1 on a machine with up to
  24 GB.
- **Threads in that link.** Upstream adds `-Wl,--thinlto-jobs=all` (one
  backend thread per CPU: 10 on the Air) in `//build/config/compiler`, with
  no GN arg. The ThinLTO link of the framework holds the combined index
  (a few GB) plus one module's optimisation per thread (around 1 GB each for
  Blink/V8-sized modules; estimate), so 10 threads would want ~13-19 GB and
  swap the Air. Koan caps it without a patch: `//koan/build:thin_lto_jobs`
  adds `-Wl,--thinlto-jobs=$koan_thin_lto_jobs`, applied to every link that
  contains Koan through `all_dependent_configs` on
  `//koan/browser:feature_overrides` (which `//chrome/browser` depends on).
  GN puts flags from dependencies' `all_dependent_configs` after the
  target's own configs, and lld takes the last `--thinlto-jobs`. The script
  sets it from the RAM: 4 up to 16 GB (about 6-9 GB at peak; estimate), 6 up
  to 24, 8 up to 32, Chromium's `all` above. It depends on the machine, not
  on `--away`, so switching modes never relinks. After gn gen the script
  checks that the framework's link line ends with the cap and stops if not
  (upstream moved the flag). `KOAN_THIN_LTO_JOBS` overrides it.
- **Compile jobs.** At most `-j 4` under `nice -n 19` and `taskpolicy -b`
  (background QoS: mostly the efficiency cores, throttled I/O) while the user
  may be at the Mac, fewer when little memory is free; `-j 8` without
  `taskpolicy -b` only with `--away`. A watchdog samples the 1-minute load
  every 30 s and, after four samples in a row above 8 (16 with `--away`),
  sends SIGSTOP to the build's whole process group, then SIGCONT once it has
  stayed below 60% of that for a minute. It logs load, free memory and swap
  every 10 minutes.
- **The lock.** The whole run holds `$KOAN_MAC/tmp/e2e.lock` (flock(2), the
  same lock as the Mac loop's stand-in `flock.py`, via
  `scripts/mac/tools/flock_run.py`): builds and e2e runs on the shared
  `chromium/src` never overlap. It waits for the lock (`KOAN_LOCK_WAIT=0`:
  fail at once).

### Expected results (estimates unless a source is given)

| | Estimate | Basis |
|---|---|---|
| First build, `--away` (`-j 8`) | 10-16 h | Measured: the non-official build took ~16 h on CI's 3-core M1 runners (`.agent/notes/macos-ci.md`). The Air's 4 P + 6 E cores are faster, but it is fanless and throttles, and the official build has no PCH, reads the profile and emits bitcode (+20-40% compile, estimate) |
| First build, default (`-j 4`, `taskpolicy -b`) | 24-40 h | Same work on mostly the efficiency cores. Start it with `--away` overnight instead |
| The framework's ThinLTO link (4 threads) | 1-2 h (`--away`), 2-4 h (default) | Estimate; the ThinLTO cache makes a relink after a small change far shorter |
| Incremental rebuild after a Koan change | 30-90 min | Mostly the framework relink: every change relinks it, and ThinLTO re-optimises the changed modules and what imports them |
| Disk at peak | 45-65 GB; the check asks for 70 GB free | Objects (bitcode, no symbols) 25-35 GB, ThinLTO cache ≤ 10% of the free disk (Chromium's policy), link temporaries in `$TMPDIR` 2-5 GB, app + `.unstripped` copies ~2 GB, the profile < 1 GB, swap growth 2-8 GB. With ~167 GB free that leaves ~100 GB |
| Speedometer 3.1 | 41.8 → about 48-55 (+15-30%) | Chromium's numbers: PGO gave "up to 10% faster page loads" (median) when Chrome 85 shipped it on Mac and Windows ([Chromium blog, M85 post under the "speed" label](https://blog.chromium.org/search/label/speed)) and "up to 15%" on Windows in 2016 ([Chromium blog](https://blog.chromium.org/2016/10/making-chrome-on-windows-faster-with-pgo.html)); Chrome 99 turned on ThinLTO on Mac, one of the changes behind its Speedometer jump on Apple Silicon ([Chromium blog, 2022](https://blog.chromium.org/2022/03/how-chrome-became-highest-scoring.html)); `compiler/BUILD.gn` tunes `import_instr_limit` by Speedometer. Arc ships Google's official build with both; the rest of the gap may be Koan's own work (bench2: 11% idle CPU against Arc's 1.9%) |

### Risks

1. **Koan's code under LTO.** Koan's `BUILD.gn` files change no compiler
   configs (no `configs -=`, no cflags; the only ldflags are `-Wl,-ObjC` in
   `//koan/test` and the cap above), so its objects are built exactly like
   upstream's. ThinLTO can surface ODR violations (two different
   definitions of the same inline function or class: the linker keeps one,
   silently), which non-LTO builds also have but rarely show; Koan's code is
   in `namespace koan` and `.mm` files compile with LTO like any other.
   `fatal_linker_warnings` stays true: if the link stops on a linker
   warning, rerun with `KOAN_GN_EXTRA='fatal_linker_warnings = false'`
   (ungoogled-chromium-macos's setting; relinks, no recompile) and file
   the warning.
2. **Stale PGO data.** The profile is Chromium's for 158.0.8127; Koan's new
   code has no profile data (treated as unprofiled, not cold, on Mac) and the
   few upstream functions Koan's hook patches change lose theirs (CFG hash
   mismatch, `-Wno-backend-plugin`). That costs only those functions. After a
   rebase the new `.pgo.txt` names a new profile, which the next gclient
   sync fetches (the custom var is set). Never use `dcheck_always_on = true`
   with the profile (pgo.gni: very long compiles).
3. **Tests on an official build.**
   - Work unchanged: `--remote-debugging-port` and `--load-extension`
     (gated on branding only), UI DevTools (`--enable-ui-devtools` and the
     `ui-debug-tools-enable-synthetic-events` feature), `--enable-features`,
     Koan's test switches (`koan_switches.h`: runtime switches, not
     build-gated). DCHECK-only paths (`--koan-icon-trace`) are already off in
     the everyday Mac build.
   - Changes: CHECK failures print no message and LOG(FATAL) no stack, so
     `suite.sh`'s log grep for "Check failed" finds nothing; crashes show up
     only as `.ips` reports, with addresses (stripped). Symbolise them with
     `atos -o "out/KoanOfficial/Koan Framework.unstripped" -l <load address>`.
   - Don't build `koan_unittests`/`koan_browsertests` in `out/KoanOfficial`:
     each test binary would be another ThinLTO link of most of Chromium.
     They stay in `out/Koan`. Run the e2e suite against the official app
     with `scripts/mac/suite.sh --skip-cxx out/KoanOfficial/Koan.app`.
4. **Symbols for crash reports.** No DWARF (`symbol_level = 0`) and no dSYMs
   keeps the out dir and the link's memory small. With stripping on and
   dSYMs off, `build/config/mac` saves `<binary>.unstripped` next to each
   stripped binary: function names, no line numbers. Keep those files with
   every build that is installed (match them by `dwarfdump --uuid`). Line
   numbers later need `symbol_level = 1` and `enable_dsyms = true`: a full
   rebuild, more disk and more link memory; only once this build is proven.
5. **The Mac itself.** Closing the Air's lid sleeps it even under
   `caffeinate -i`; keep it open and on power. siso doesn't mind being
   stopped by the watchdog, but a run of many pauses makes the build longer
   than the estimates.

### Order of signing

The script VMP-signs (Widevine) first and code-signs after, as
`scripts/mac/widevine_vmp_sign.sh` asks and `scripts/ci/mac_package.sh`
does: the `.sig` file sits in the framework's Resources, which the code
signature seals. (`scripts/build.sh` does it the other way round; its VMP
step only runs with an EVS login on the Mac.)

## What differs from Linux in the code

- Linux-only patches (`shell_integration_linux.cc`, `chrome_paths_linux.cc`,
  `channel_info_posix.cc`, the overlay-scrollbar `IS_LINUX` branch) apply
  but aren't compiled on macOS. `opaque_browser_frame_view.cc` compiles but
  the Mac browser frame is `BrowserFrameViewMac`.
- No Aura on macOS: Glance doesn't scale the page in. The page is an
  NSView composited under the NativeViewHost's layer; animating
  `holder()->layer()` may work there, untested.
- No caption button views: Koan's caption-button hiding is a no-op; the
  traffic lights are AppKit's.
- Immersive fullscreen moves the top container into
  `BrowserView::overlay_widget()`. While Koan hides the toolbar (single
  toolbar, compact), Koan's controller takes it back into its own overlay on
  entering and exiting (an `ImmersiveModeController::Observer`) and the
  fullscreen toolbar style is `none` (no reveal at the top edge); when the
  mode deactivates in fullscreen the top container goes back to Chromium's
  overlay widget, with its hidden-toolbar transform and pointer blocking
  reset.
- Traffic lights (#72): AppKit's. Koan sets the title bar height they are
  centred in (hook in `browser_native_widget_mac.mm`), hides and shows them
  with `standardWindowButton:` like the other platforms' caption buttons
  (`koan_traffic_lights*`), and the compact mode controller treats them as
  caption buttons on the page side when the sidebar is hidden or on the
  right. Trackpad swipes between Spaces: `KoanSpaceSwipe`. Neither could be
  run on a Mac by the agent that wrote them: see the PR for what the macOS CI
  compiled.
- Shortcuts follow Zen's macOS table (#71): `kAccel` is ⌘, the table is in
  `src/browser/koan_shortcut_table.cc`, and the menu bar and Dock menu in
  `src/browser/ui/cocoa/koan_main_menu_mac.mm` (hooks in
  `main_menu_builder.mm` and `app_controller_mac.mm`). Key path on a Mac:
  `ChromeCommandDispatcherDelegate` runs high-priority views accelerators
  and Chrome's reserved commands first (`prePerformKeyEquivalent`), then the
  page, then normal-priority accelerators (`postPerformKeyEquivalent`,
  before the main menu). Koan registers shortcuts on reserved keys
  (`shortcuts::IsReservedOnMac()`) at high priority, and keeps the menu bar's
  keys in line with Koan's (items give up or take a key at "menu open").
  Tried on a Mac by the Mac loop, not by the agent.

## Sidebar animations (#94)

- Upstream animates panels differently per platform
  (`browser_animations::GetPanelAnimationType()`, `common_animation_values.h`):
  Linux and Windows lay the page out once at its final size and clip it
  (`kClipAndSingleReflow`); **macOS on Apple Silicon resizes the page on
  every animation frame** (`kContinuousFrameResizeWithSurfaceSync`). The
  Linux e2e never sees that, and on a Mac it is the flicker when the sidebar
  expands or collapses (the page is cleared and redrawn at each size, with
  Koan's card around it). `koan::UseSingleReflowSidebarAnimation()`
  (feature `KoanSidebarSingleReflow`, chrome://flags "Koan sidebar single
  reflow") makes the tabbed layout take the clip branch everywhere;
  `--disable-features=KoanSidebarSingleReflow` gives upstream's macOS
  behaviour back to compare.
- Compact mode zeroes the sidebar's width from the start of its collapse
  (not at the end, when the width used to jump from 56 to 0), and the hidden
  sidebar's reveal is a layer transform on a strip laid out once
  (`koan_sidebar_animation.*`).
- Compact mode and single toolbar apply in native fullscreen on macOS (the
  card stays, see `UseContentCard()`); see the entry above for the top
  container.

## Signing builds with one identity (#141)

**Symptom.** Chromium's "Relaunch the browser to load your profile data and
keep it encrypted" infobar: `MaybeShowOSCryptAsyncAvailabilityInfoBar`
(`infobar_utils.cc`, feature `kOSCryptAsyncAvailabilityInfoBar`, on by
default on macOS) shows it when the async `Encryptor` has no key, i.e. when
`KeychainPassword::GetPassword()` returned an empty string for the login
keychain item "Koan Safe Storage".

**Under `--use-mock-keychain`** (`scripts/run_mac.sh`, the Mac harness, tests)
the async key provider asks a `FakeKeychainV2`, which refuses an item without
its access group (`errSecMissingEntitlement`), so encryption is "unavailable"
on every launch and the bar says nothing true. Koan switches it off in that
case only (`koan_feature_overrides.cc`). The harness keeps
`--use-mock-keychain` everywhere and never touches the real keychain.

**Mechanism for a real launch (from how the Keychain works; not yet observed
on a Mac).** A keychain item's access control lists the apps allowed to read
it by their code requirement. A build without a certificate is signed ad hoc
by the linker, and an ad-hoc signature is the CDHash of the code, which
changes with every build. After a rebuild the saved item's ACL no longer
matches: macOS prompts ("Koan wants to use your confidential information
stored in 'Koan Safe Storage'") and, if the prompt is dismissed or denied,
the read returns nothing: the bar shows, and cookies and passwords can't be
decrypted. Nothing should hide the bar then: it is true.

**The fix: one signing identity.** `scripts/mac_signing_identity.sh create`
makes a self-signed code-signing certificate "Koan Local Signing" in a
keychain file of its own, `$KOAN_MAC/keychains/koan-signing.keychain-db`
(not the login keychain, not trusted system-wide). `security create-keychain`
adds the new keychain to the user's keychain search list; `create` records
`security list-keychains -d user` first and restores it exactly (it refuses
to run if the list can't be read), so afterwards the list is what it was.
`scripts/mac_sign.sh Koan.app` signs with it, and `scripts/build.sh` calls
that after every Mac build (`KOAN_SIGN=0` skips). The designated
requirement is then `identifier "dev.koan.Koan" and certificate leaf =
H"<hash>"` on every rebuild, so the saved item keeps matching.

The commands (what the scripts run, with `KC=$KOAN_MAC/keychains/koan-signing.keychain-db`):

```sh
security list-keychains -d user                 # recorded, restored at the end
# openssl.cnf: [req] distinguished_name=dn, prompt=no, x509_extensions=ext;
# [dn] CN=Koan Local Signing; [ext] basicConstraints=critical,CA:false,
# keyUsage=critical,digitalSignature, extendedKeyUsage=critical,codeSigning
# (a config file: macOS's LibreSSL may not have -addext; umask 077 for the key)
/usr/bin/openssl req -x509 -new -newkey rsa:2048 -nodes -sha256 -days 3650 \
  -config openssl.cnf -keyout key.pem -out cert.pem
/usr/bin/openssl pkcs12 -export -inkey key.pem -in cert.pem \
  -name "Koan Local Signing" -out identity.p12 -passout pass:koan-local-signing \
  -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1
security create-keychain -p koan-local-signing "$KC"
security set-keychain-settings "$KC"            # never auto-lock
security unlock-keychain -p koan-local-signing "$KC"
security import identity.p12 -k "$KC" -P koan-local-signing -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple: -s -k koan-local-signing "$KC"
security list-keychains -d user -s <the recorded list>
codesign --force --deep --sign <SHA-1 of the certificate> --keychain "$KC" \
  --timestamp=none Koan.app
codesign --verify --deep --strict Koan.app
codesign -d -r- Koan.app                        # the designated requirement
```

If signing or the checks after it fail, `mac_sign.sh` signs the whole app ad
hoc again (never a half-signed bundle: `--deep` signs the nested code first)
and warns loudly; the build still succeeds. If `create` fails after making
the keychain file it removes that file and restores the search list, so it
can be run again; a keychain file left with no certificate (an older run) is
also fine to rerun on. The key and certificate are written with umask 077
into a temp dir that is removed on exit.

**What a friend sees.** Run `scripts/mac_signing_identity.sh create` once.
It makes the keychain file and the certificate; there is normally no
dialog (the keychain is the script's own and unlocked with a fixed password,
`koan-local-signing`, public on purpose: it protects only a key which signs
local builds on this Mac). If macOS asks whether `codesign` may use the key,
choose "Always Allow". The first launch of a build signed this way can still
prompt once for "Koan Safe Storage" if the item was created by an ad-hoc
build before. That prompt asks for **your login keychain password** (not
only a button); type it and choose "Always Allow", which is meant to stick
for every later rebuild (see "Not proven" below). A rebuilt, re-signed Koan
also asks once more for camera, microphone and screen-recording access it had
been granted, because its requirement changed from the CDHash to the
certificate. Each build re-signs about 1 GB with `--deep`: a few seconds;
`KOAN_SIGN=0` skips it (use it for runs that only use `--use-mock-keychain`).

**Run real-keychain checks from Finder or `open`**, not through
`scripts/run_mac.sh`: `env.sh` redirects `HOME` and `CFFIXED_USER_HOME` into
`$KOAN_MAC`, so a Koan started that way may not use your login keychain.

**Undo it.** Delete `$KOAN_MAC/keychains/koan-signing.keychain-db` by hand
(and, if you ever added it to the search list yourself, remove it in Keychain
Access or `security list-keychains -d user -s ...`). Nothing else changed.
Two Koan folders (two `KOAN_MAC`s) make two certificates with the same name
and different hashes; switching folders makes the saved item stop matching
once. One folder, one identity.

**Apple Development identity (an option, not the default).** With a free
Apple ID in Xcode you get an "Apple Development: ..." certificate with a Team
ID. List it read-only with
`security find-identity -v -p codesigning | grep "Apple Development"` and sign
with `codesign --force --deep --sign <its hash> Koan.app`. Its requirement is
certificate- and team-based (`anchor apple generic and ... subject.OU =
<team>`), which also gives the keychain item a `teamid:` partition. Prefer it
if the self-signed identity does not stop the prompt (see below).

**If they decline** (never run `create`): builds stay ad-hoc signed;
`scripts/mac_sign.sh` says so on each build. Saved logins and cookies may
need re-entering after a rebuild, and the bar can show; nothing else breaks.

**What it never does.** It never deletes or edits an existing keychain,
keychain item or certificate (no `delete-*`, no `set-*-partition-list` on
another keychain), never touches the login keychain, and never runs under
`--use-mock-keychain` tests. Changing the identity (a different certificate)
changes the designated requirement, so the existing item would stop matching
once: don't regenerate it without a reason.

**Static proof (touches no existing keychain item; the Mac loop can run it).**
`scripts/mac_signing_check.sh selftest` makes a throw-away keychain in
`$TMPDIR` (search list restored, directory removed), signs two dummy apps
(`dev.koan.Koan`, different executables, like two rebuilds) the way
`mac_sign.sh` does, prints `codesign -d -r-` for both (must be identical and
`certificate leaf = H"..."`, not `cdhash`), checks the second against the
first's requirement with `codesign --verify -R "=<requirement>"`, and as a
negative control shows that two ad-hoc rebuilds do not satisfy each other.
`scripts/mac_signing_check.sh X.app Y.app` does the same on two real signed
builds. This proves the ACL's trusted-application requirement stays valid
across rebuilds.

**Not proven: the login keychain's partition list.** Since macOS 10.12 an
item also has a partition list; an item created by an app without a Team ID
gets a code-hash partition, and a self-signed certificate has no Team ID. If
that makes each rebuild a new partition, the prompt comes back despite the
stable requirement. Only a real item can show it (E1 was cancelled to keep
the maintainer's real item safe). If the validation below shows the prompt
again, use an Apple Development identity (above) and update this section.

**Validation (by hand, with consent, on the maintainer's Mac).** Build and
sign X, log in somewhere, quit; rebuild (Y), sign again; launch Y on the same
profile: no keychain prompt, no infobar, the login is still there. Compare
`codesign -d -r- X.app` and `Y.app`: the same requirement. The Mac harness
does not run this.

## Bench lessons (2026-11-03, the Mac test-bench loop)

What the first day of running other threads' branches on the Mac taught us.
The bench scripts live in `~/work/koan-mac` on the Mac, not in the repo. This
section is the Mac build loop's own procedure: other agents never ssh to the
Mac, build there or run Koan there.

### Building

- **Load.** `autoninja -j N` turns into siso `-local_jobs=N`, but the load
  average still reached 27 at `-j 8` and froze the laptop. While the user is at
  the Mac use `-j 3` (or 4) under `nice -n 10`, with `caffeinate -i`; `-k 0` keeps
  going after a failed object, but anything that depends on it is blocked.
  A killed siso resumes incrementally.
- **Detached builds.** Run `nohup ./build-full.sh > log &` and poll for a line
  starting `AGENT-BUILD-EXIT <rc>`; do not keep an ssh session open for a build
  (the harness limit is 2 hours). An incremental rebuild (a few objects and the
  link) takes 40-50 s at `-j 3`.
- **Branches: one PR at a time.** The Mac loop builds `develop` alone first,
  then adds one PR head at a time in dependency order, rebuilds incrementally and
  runs the regression script after each. The first PR after which a crash appears
  is the culprit. No stacked preview is given to the user. Mechanics: the head is
  merged onto the current branch in a scratch worktree (Linux is only for
  editing), shipped as a `git bundle`, and checked out on the Mac by
  `switch-branch.sh OLD NEW bundle` (reverse the old branch's changed patch
  files, `git checkout NEW`, `scripts/apply_patches.sh apply`, `gn gen`). Nothing
  is pushed. Conflicts are mostly additive (BUILD.gn lists, feature flag
  entries, patch hunks whose counts need recounting); check
  `git grep '^<<<<<<<'` before every build, because a blanket `git commit -a`
  mid-merge commits conflict markers.
- **Env.** `CFFIXED_USER_HOME="$HOME"` must be exported (see above) or
  `gn gen`/`actool` look in the real home; the Metal toolchain link change of
  #70 is a local-only change in `scripts/build.sh` until it lands.

### Running and capturing

- **Flags for a test instance:** `--user-data-dir=<fresh> --use-mock-keychain
  --disable-features=MacAppCodeSignClone --no-default-browser-check
  --enable-ui-devtools=9340 --remote-debugging-port=9341
  --enable-features=ui-debug-tools-enable-synthetic-events`; add
  `--disable-features=KoanWelcome --no-first-run` to skip the welcome, or
  `--force-first-run` to show it (`--enable-features=KoanWelcomeWebUI` for the
  WebUI version). Do not seed `Default/Preferences` by hand: Koan then says
  "Your preferences file is corrupt" and hangs.
- **Driving it (Mac loop only):** an `ssh -L` tunnel to 9340 (UI DevTools: the Views
  tree, synthetic mouse and key events) and 9341 (CDP). UI DevTools can stop
  answering after the Library is opened and closed; CDP keeps working.
- **Input.** Posted key chords (`CGEventPostToPid`, ⌘T, ⌘L, ⌘S, ⌃⌘F, Esc)
  reach the Koan process; posted mouse clicks do not, and a global click moves
  the pointer and can hit another app, so there are none. Activate Koan and check
  that `lsappinfo front` is Koan before each key.
- **Capture.** The user's own Zen is usually in a fullscreen Space of its own
  and re-raises itself, so Koan's windows are normally on another Space and
  region captures see nothing. List windows with `CGWindowListCopyWindowInfo`
  (`kCGWindowListOptionAll`) and capture one by id with
  `screencapture -x -o -l <id>`; it works on any Space without switching.
  Limits: a captured window shows glass as flat grey, a fullscreen window has a
  blank band at the top, and the overlay windows of the welcome (title bar and
  scrim) are separate windows, so capture every Koan window wider than 200 pt.
  Never record the screen: it captures the user's other windows.
- **Processes.** Check with `ps -Ao command | grep "[M]acOS/Koan -"`;
  `pgrep -f "Koan.app/..."` misses `Koan-preview.app`. Kill only by exact pid,
  and only a process whose command line is under `~/work/koan-mac`
  (`tmp/killkoan.sh`). The user's own Zen or Koan must never be signalled.
- **No debugger.** Developer mode is off, so there is no lldb; read crashes from
  `~/Library/Logs/DiagnosticReports/Koan-*.ips` (read-only) and, for an upstream
  `CHECK`, add the view's class name to the message locally (revert after).

### Crashes seen

- **Colour cycle at launch** (#97 6de98f7e): a cyclic `ColorRecipe` overflowed
  the stack in `ThemeSource::GenerateColorsCss`. #97 15565a45 computes the
  colours from tokens instead of ids.
- **⌘S and fullscreen on the glass stack:** `CustomCornersBackground`'s
  `CHECK(!layer->fills_bounds_opaquely())` fired for `VerticalTabStripRegionView`
  (`custom_corners_background.cc:160` and `:331`). Fixed on the glass side
  (#105 8c5a986c guard, root cause in #41's `InvalidateBackground`).

## Black notch at the top in compact mode (2026-11-20)

Upstream's `vertical_tab_strip_top_corner` / `_bottom_corner`
(`CustomFloatingCorner`, the frame colour through `ThemedBackground`) are
BrowserView children placed at the strip's layout bounds and animated by
`TabStripAnimations` (`kTopCorner`: collapse from 1, hover flyout from its
resting value to -1). Compact mode lays the strip out at full width at once and
slides only its layer, so the corners stood alone where the strip was going or
had been. In native fullscreen the resting top corner is 1 (`kFullscreen`
window state: Koan defaults `kShowFullscreenToolbar` off), so every peek and
hide flashed it at the top edge; in a window the collapse on ⌘S started it at
1. In compact glass and in fullscreen their alpha is 1. Koan hides both in
compact mode (`koan_sidebar_corners.h`). Check: `scripts/mac/video/flows/compact_edge.mjs`
then `edge_check.mjs`.
