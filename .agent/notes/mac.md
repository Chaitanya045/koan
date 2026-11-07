# macOS

Facts and rules for Koan's Mac build (issue #51). The Mac is the
maintainer's daily laptop; Linux stays where agents develop and run the e2e
tests.

## Consent first

**Every build, test run, gclient/gn command or script on the Mac needs the
maintainer's explicit OK first.** Without it, only quick read-only commands
are allowed over SSH: `ls`, `cat`, `grep`, `sw_vers`, `which`,
`bash --version`, `git ls-tree`/`git status`, and parse-only checks such as
`/bin/bash -n < script`. Nothing that writes, nothing long-running.

## Machine

- MacBook Air, Apple M4 (arm64), 10 cores, 16 GB RAM, 256 GB SSD
  (~220 GB free when this was written).
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
    └── chromium/src   Chromium 158.0.8111.0 @ 67e29b8, target_os ["mac"]
```

`env.sh` points `HOME`, `TMPDIR`, `XDG_CACHE_HOME`, `XDG_CONFIG_HOME`,
`VPYTHON_VIRTUALENV_ROOT` and `CIPD_CACHE_DIR` into the folder, sets
`DEPOT_TOOLS_METRICS=0` and `DEVELOPER_DIR`, and puts depot_tools first on
`PATH`. The checkout is ~31 GB; with `out/Koan` budget ~60 GB.

**The rule is that everything stays in the folder.** Every script (and
`branding.py`) refuses to run on macOS unless `KOAN_MAC` is set and `HOME`
and `TMPDIR` are inside it (`koan_require_mac_env` in
`scripts/koan_platform.sh`). The scripts also export
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

## Build

```sh
source ~/work/koan-mac/env.sh && cd ~/work/koan-mac/koan
scripts/apply_patches.sh && scripts/apply_branding.sh
scripts/build.sh               # build/koan-mac.gn -> chromium/src/out/Koan/Koan.app
scripts/run_mac.sh             # test run, profile in $KOAN_MAC/profile
```

- `build/koan-mac.gn`: arm64, optimised, not a component build, no DCHECKs,
  `symbol_level = 0`. No `is_official_build` yet (ThinLTO + PGO later, if
  build time and memory allow). Every incremental change relinks the whole
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
  keychain item after every rebuild. Signing `Koan.app` with a stable local
  identity after each build (`codesign --force --deep -s <identity>`) would
  make "Always Allow" stick; not set up yet.

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

## "Relaunch ... to keep it encrypted" infobar (#141)

`MaybeShowOSCryptAsyncAvailabilityInfoBar` (`infobar_utils.cc`, feature
`kOSCryptAsyncAvailabilityInfoBar`, on by default on macOS) shows it when the
async `Encryptor` has no key. With `--use-mock-keychain` (what
`scripts/run_mac.sh` and test launches pass) the async key provider
(`KeychainKeyProvider`) asks a `FakeKeychainV2`, which refuses an item without
its access group (`errSecMissingEntitlement`), so `KeychainPassword::GetPassword()`
is empty and encryption is "unavailable" on every launch. Koan switches the
infobar off in that case (`koan_feature_overrides.cc`). Without the flag the
login keychain item "Koan Safe Storage" is created by the first launch with no
prompt; an ad-hoc signed build whose signature changed since the item was
created gets a keychain access prompt ("Always Allow" doesn't stick across
builds): if that is denied the bar is the correct warning. A stable local
signing identity would stop the prompts (not set up).

