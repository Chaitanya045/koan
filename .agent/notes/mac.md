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
  `actool` 27.1). The system-wide `xcode-select -p` is the Command Line
  Tools, so builds must set `DEVELOPER_DIR`; the scripts default it to
  Xcode.app.
- Tools: `/bin/bash` is 3.2; Homebrew bash 5.3 is at `/opt/homebrew/bin/bash`
  but not on a non-interactive SSH `PATH`, so the scripts stay bash 3.2
  compatible. `python3` is `/usr/bin/python3`, 3.9 (depot_tools has no
  `python3` on `PATH`, only `vpython3`), so `scripts/branding.py` stays 3.9
  compatible. `iconutil`, `sips`, `actool` exist;
  `rsvg-convert` and ImageMagick don't (`brew install librsvg` is the
  maintainer's call; `apply_branding.sh` falls back to `sips`).
- SSH: `ssh -i ~/.ssh/koan_mac_ed25519 chaitanya@chaitanyas-macbook-air`.

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

## Build

```sh
source ~/work/koan-mac/env.sh && cd ~/work/koan-mac/koan
scripts/apply_patches.sh && scripts/apply_branding.sh
scripts/build.sh               # build/koan-mac.gn -> chromium/src/out/Koan/Koan.app
```

- `build/koan-mac.gn`: arm64, optimised, not a component build, no DCHECKs,
  `symbol_level = 0`. No `is_official_build` yet (ThinLTO + PGO later, if
  build time and memory allow). `JOBS` defaults to 4.
- `scripts/bootstrap.sh` does what `fetch.sh` did: a `.gclient` with
  `target_os = ["mac"]`, `gclient sync --nohooks --no-history --revision
  src@<CHROMIUM_VERSION rev>`, `gclient runhooks`.
- Branding on macOS also builds `app.icns` (iconutil) and `Assets.car`
  (actool, from the icon bitmaps plus `branding/mac/AppIcon.icon` for the
  macOS 26+ icon) and copies them over `chrome/app/theme/chromium/mac/`.
  The bundle is `Koan.app`, id `dev.koan.Koan`, from `BRANDING`.
- Profile: `~/Library/Application Support/Koan` (hook in
  `chrome_paths_mac.mm`).

## What differs from Linux in the code

- Linux-only patches (`shell_integration_linux.cc`, `chrome_paths_linux.cc`,
  `channel_info_posix.cc`, the overlay-scrollbar `IS_LINUX` branch) apply
  but aren't compiled on macOS. `opaque_browser_frame_view.cc` compiles but
  the Mac browser frame is `BrowserFrameViewMac`.
- No Aura on macOS: Glance doesn't scale the page in (it's an NSView).
- No caption button views: Koan's caption-button hiding is a no-op; the
  traffic lights are AppKit's.
- Immersive fullscreen keeps the toolbar in `BrowserView::overlay_widget()`;
  compact mode leaves it there when it deactivates.
- Shortcuts are still Ctrl-based. ⌘ shortcuts, traffic lights in the
  sidebar and trackpad swipes are later parts of #51.
