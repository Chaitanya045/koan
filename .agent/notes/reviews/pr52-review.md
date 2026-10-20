# Review: PR #52, macOS build support (part of #51)

I reviewed branch `mac-build` at `a514840` (5 commits), diffed against `origin/develop`. I only read the code: nothing was built, and nothing was run on the Mac.

What I checked against:
- **Mac checkout:** `~/work/koan-mac/koan/chromium/src`, Chromium 158.0.8111.0 @ `67e29b8`. It is clean, with no patches or branding applied. I only ran `cat`/`grep`/`ls`/`sed -n` over SSH.
- **Mac host:** a few read-only probes: `sw_vers`, `xcode-select -p`, `xcodebuild -version`, `sips --formats`, and `/bin/bash -n < script` on each changed script.
- **Linux checkout:** `~/work/koan/chromium/src`, the same revision, with develop's patches applied.

Paths are relative to the worktree. Upstream line numbers are from the Mac checkout.

**What checks out (summary):**
- **Mac compile:** every Mac-specific `#if`, include, GN condition and upstream API the PR uses exists on the Mac tree:
  - `BrowserView::overlay_widget()` is at `browser_view.h:244-246`, under `IS_MAC`.
  - `USE_AURA=1` is defined from `use_aura` (`build/config/BUILD.gn:59`), and `feature_flags` is a default config.
  - `ProductDirNameForBundle` matches the patch context (blob `6829c9cb`).
  - The `chrome/browser/BUILD.gn` hunk sits inside `if (toolkit_views)`.
  - Every frame target in `:impl`'s deps is defined unconditionally.
  - No other Koan source includes Aura, Ozone or GTK headers. I grepped every upstream header Koan includes for blocks that exclude Mac, and Koan uses none of their symbols.
- **Patches on the Mac tree:** all 30 should apply to the clean Mac tree (same revision; the new mac patch's context matches). I couldn't run `git apply --check` there because it isn't on the read-only list.
- **GN args:** every arg in `build/koan-mac.gn` is declared: `dcheck_always_on.gni`, `blink/renderer/config.gni`, `v8/gni/v8.gni`, `features.gni`, `ffmpeg_options.gni`, `components/variations/service/BUILD.gn`, `compiler.gni`. The values do what the comments say: arm64, not a component build, `symbol_level` 0, DCHECKs off (the default is on for non-official builds, `dcheck_always_on.gni:25-26`).
- **SDK and warnings:** Xcode 27.1's SDK is `MacOSX27.0.sdk`, build `26A425`. That is exactly `mac_sdk_official_version`/`_build_version` (`mac_sdk.gni:51,56`). The compiler is Chromium's own clang, and the deployment target is 13.0, so the "Xcode 27.1 warnings as errors" risk is low.
- **Xcode setup:** the Xcode license is accepted (`xcodebuild -version` works), and `xcode-select -p` honours `DEVELOPER_DIR` (it printed Xcode.app with the variable set).
- **Shell and Python:** all changed scripts parse with `/bin/bash` 3.2.57. No bash-4 features are used in the scripts that run on the Mac. `branding.py` and its tests parse with Python 3.9 grammar, and the 29 tests pass on Linux.
- **`-j`:** `autoninja -j 4` becomes Siso `-local_jobs=4` (`depot_tools/autoninja.py:198-224`).
- **Branding:**
  - The bundle name, framework and helper names, and `CFBundleIdentifier` all come from `BRANDING` (`build/util/branding.gni`, `chrome/BUILD.gn:548-550`), so the app becomes `Koan.app`, `dev.koan.Koan`.
  - The `InfoPlist.strings` keys (`infoplist_strings_util.cc:147-196`) come from messages that `branding.py` already rebrands: `IDS_PRODUCT_NAME`, `IDS_HELPER_NAME`, the OS permission reason, the shortcut description.
  - The icon pipeline matches Chromium 158. The build only copies the checked-in `mac/app.icns` and `mac/Assets.car` (`chrome/BUILD.gn:675-683`). The actool flags in `actool_command` are the same as `tools/mac/icons/compile_car.py:186-236`. The `icon.json` keys used (`linear-gradient`, `supported-platforms.squares`) also appear in upstream's `AppIcon.icon`.
- **Linux:** I found no regressions in `src/` or in the patches. The Aura guard and the Mac branches leave Linux unchanged.

---

## Blockers

None verified. I found nothing that would make `gn gen` or the compile and link of `chrome` fail on the Mac. The biggest risk to the first run comes before the compile: `apply_branding.sh` on `sips` (S3).

---

## Should fix

### S1. Compact mode leaves the toolbar shifted off-screen and ignoring clicks when macOS fullscreen takes it over

- **Where:** `src/browser/ui/views/koan_compact_mode_controller.cc:599-612` (the new `IS_MAC` branch of `Deactivate`), together with `:485`, `:650` and `:673`.
- **What's wrong:** while compact mode is on with the toolbar hidden, Koan leaves two things on the top container:
  - `SetCanProcessEventsWithinSubtree(false)` (`:650`, through `SetToolbarRevealed(false)` at `:485`);
  - a layer transform of `Translate(0, -height)` (`:673`).

  Normally the restore branch (`:613-627`) undoes both: it calls `SetCanProcessEventsWithinSubtree(true)` and `DestroyLayer()`. The new Mac branch sets `restore_top_container = false` and only hides Koan's overlay, so neither is undone. Upstream doesn't reset them either:
  - `BrowserView::ReparentTopContainerForStartOfImmersive` (`browser_view.cc:3798-3825`) calls `SetPaintToLayer()`, which keeps the existing layer and its transform;
  - `immersive_mode_controller_mac.mm` never calls `SetCanProcessEventsWithinSubtree` or `SetTransform` on the top container.
- **Failure scenario:**
  1. Turn on compact mode (Ctrl+S) with the toolbar hidden.
  2. Enter fullscreen (green button or Ctrl+Cmd+F). Immersive fullscreen moves the top container into `overlay_widget()` before Koan's posted `UpdateActive` runs, so the Mac branch is taken.
  3. Move the pointer to the top of the screen. The revealed toolbar is drawn one toolbar-height above the screen (blank), and clicks fall through.
  4. Turn compact mode off in fullscreen, then leave fullscreen. Upstream's `DestroyLayer()` clears the transform, but the event flag stays false, so the toolbar ignores the mouse in the normal window until compact mode is toggled again.
- **Confidence:** medium, from reading the code; not reproduced. It depends on immersive starting before the posted deactivation, which is the case this branch was written for.
- **Fix:** in the Mac branch, still undo Koan's own state without reparenting:
  - abort the layer's animations;
  - `SetTransform(gfx::Transform())`;
  - `SetCanProcessEventsWithinSubtree(true)`.

  Leave the layer, which the overlay widget needs, and leave `kViewDoesNotLayOutChildren`, which upstream sets on exit.

### S2. Nothing enforces the one-folder rule: running a script without `env.sh` silently writes into the real home directory

- **Where:**
  - `scripts/koan_platform.sh:18-27` (`koan_require_xcode` defaults `DEVELOPER_DIR` itself);
  - `scripts/build.sh:34`, `scripts/bootstrap.sh:19`, `scripts/apply_branding.sh:31`.
- **What's wrong:** the scripts never check that `~/work/koan-mac/env.sh` was sourced. Because `DEVELOPER_DIR` gets a default, a fresh Terminal "just works", and everything the tools cache lands outside the folder:
  - depot_tools' `vpython3` creates `~/.vpython-root` (the copy inside the folder is about 2.7 GB with the CIPD cache);
  - plus `~/.cipd-cache`, `~/.gsutil`, Siso's `~/Library/Caches/...` state, and `tempfile`/`mktemp` under `/var/folders`;
  - `git` also stops seeing `home/.gitconfig`, and the other way round.
- **Failure scenario:** a day later, the maintainer opens a new Terminal, `cd ~/work/koan-mac/koan`, and runs `scripts/build.sh`. Gigabytes of virtualenvs and CIPD packages are created under `/Users/chaitanya`, outside `~/work/koan-mac`, and nothing reports it.
- **Fix:** on `mac`, have `koan_platform.sh` refuse to run (or warn loudly) unless `KOAN_MAC` is set and `HOME` is under it, for example `[[ -n ${KOAN_MAC:-} && $HOME == "$KOAN_MAC"/* ]]`. Point the error at `source ~/work/koan-mac/env.sh`.
- **Places outside the folder even with `env.sh` (by design, for the record):**
  - The running app (launched with `open`, so with the real `HOME`) writes:
    - the profile: `~/Library/Application Support/Koan`;
    - the cache: `~/Library/Caches/Koan` (`chrome_paths_mac.mm`, `GetUserCacheDirectory`);
    - `~/Library/Preferences/dev.koan.Koan.plist` and `~/Library/Saved Application State/dev.koan.Koan.savedState`;
    - a hard-link clone under `/private/var/folders/.../dev.koan.Koan.code_sign_clone` (`chrome/browser/mac/code_sign_clone_manager.mm`);
    - the login-keychain item (S4) and its LaunchServices registration.
  - **Uncertain:** Apple's tools (`xcodebuild` from `gn gen`'s `sdk_info.py`, `xcrun`, `actool`/`ibtoold` from `apply_branding.sh`) find the home folder through Foundation, which reads `CFFIXED_USER_HOME`/`getpwuid`, not `HOME`. So they may still write `~/Library/Caches/com.apple.dt.*` or `~/Library/Developer/...`. I didn't verify this, because it would need writes on the Mac.

### S3. The `sips` fallback (the path the first run will take) doesn't check what it renders, and `wordmark.svg` depends on `<text>`

- **Where:**
  - `scripts/apply_branding.sh:40-46` (the `sips` `render`);
  - `branding/wordmark.svg:11-12` (a `<text>` element, rendered with system fonts);
  - `scripts/branding.py:277-288` (`actool_problems` only collects `*errors`/`*warnings`) and `:309-330` (only the return code and the presence of `Assets.car` count as success).
- **What's wrong:**
  - **`rsvg-convert` is missing on the Mac** (`which` shows none), so the first run will use `sips`. `sips --formats` does list `public.svg-image` as readable, but this pipeline has never run.
  - **Sizes are never checked.** If `sips` doesn't rasterise at the injected `width`/`height` exactly, `iconutil` rejects the `.iconset` ("Invalid Iconset"), and `apply_branding.sh` exits under `set -e` before `branding.py apply`. `BRANDING` and the strings stay Chromium's.
  - **Unverified: CoreSVG's `<text>` support.** If CoreSVG doesn't render SVG `<text>`, `wordmark_*.png` silently lose the word "Koan". If it errors instead, the script aborts at the wordmark loop.
  - **actool notices are dropped.** Upstream's `compile_car.py:290-304` notes that actool reports missing inputs as *notices*, and it fails on notices and on most warnings. `branding.py` drops notices and never fails on warnings, so a wrongly sized `appicon_*.png` gives a "successful" but wrong `Assets.car`.
- **Failure scenario:** the maintainer runs the documented chain from `.agent/notes/mac.md`, `apply_patches.sh && apply_branding.sh && build.sh`. If `iconutil` rejects a bitmap, the chain stops at branding and no `Koan.app` is built. If the icons are slightly off-size instead, the build completes with a blurry or mis-sized Dock icon and no error.
- **Fix:**
  - After each `sips` render, force and check the size: `sips -z "$3" "$2" "$4"`, then compare `sips -g pixelWidth -g pixelHeight`.
  - Convert the wordmark text to outlines in the SVG (also removes the font dependency on Linux).
  - Make `actool_problems` include `*notices`, and treat anything except upstream's two expected warning types as fatal.
  - Or install librsvg for the first run. That goes to `/opt/homebrew`, outside the folder: the maintainer's call.

### S4. Keychain item "Chromium Safe Storage": change it now, before the first launch

- **Where:** upstream `components/os_crypt/common/keychain_password_mac.mm:37-39` (`kDefaultServiceName = "Chromium Safe Storage"`, account `"Chromium"`, for every non-Google build). This is the only place it is set; `components/os_crypt/async/browser/keychain_key_provider.mm` reuses `KeychainPassword`.
- **What's wrong:**
  - Koan would share Chromium's Safe Storage key. Any Chromium build on this Mac would read, or have created, the same key, and macOS would ask "Koan wants to use ... 'Chromium Safe Storage'".
  - Once Koan has encrypted cookies, passwords and autofill (os_crypt v10) with that key, renaming it later makes all of it undecryptable: the user is logged out everywhere and saved passwords are lost, unless someone writes a migration.
- **Is it easy and safe now?**
  - **Easy:** yes. It's a two-line hook patch (`"Koan Safe Storage"`/`"Koan"`), or a `koan::kMacKeychainServiceName` constant in `koan_branding.h` plus a `//koan/common:branding` dep on `components/os_crypt/common:keychain_password_mac`.
  - **Safe:** yes, *now*. `~/Library/Application Support/Koan` doesn't exist yet, so nothing has been encrypted.
  - Add it to this PR, or land it before anyone launches `Koan.app`.
- **Related, for daily use:**
  - Every new build gets a new ad-hoc signature, so macOS asks for keychain access again after each rebuild. Upstream documents this in `docs/mac_build_instructions.md:225-235`.
  - Upstream's workaround, `--use-mock-keychain`, isn't fit for a daily driver.
  - Signing `Koan.app` with a stable local identity after each build would make "Always Allow" stick. Worth a line in `mac.md`.

### S5. Changing an existing patch makes `apply_patches.sh` report CONFLICT on checkouts that already have develop's patches

- **Where:** `patches/chrome/common/BUILD.gn.patch` (gains the `is_mac` hunk), and `scripts/apply_patches.sh:58-67`.
- **What I ran:** `git apply --check` and `git apply --reverse --check` (both check-only) against the Linux tree. The new `chrome/common/BUILD.gn.patch` is neither applied nor applicable there; it is reported as CONFLICT.
- **Failure scenario:** the queued Linux build, or any agent, checks out this branch in the shared `~/work/koan/chromium/src` and runs `scripts/apply_patches.sh`. It prints `CONFLICT chrome/common/BUILD.gn`, exits 1, and the build is skipped or runs without the new hunk.
- **Fix:**
  - Run `apply_patches.sh reverse` on develop's patches before switching. Say so in the PR description and `docs/REBASE.md`.
  - Or make `apply` handle it: each patch owns a whole file. So when a file is modified but its patch neither applies nor reverses, `git checkout HEAD -- <file>` and apply again.

---

## Nits

### N1. The Glance comment says the page on macOS is outside the compositor's layers; it isn't

- **Where:** `src/browser/ui/views/koan_glance_controller.cc:128-129`.
- **What's wrong:** on Mac, `NativeViewHostMac` gives the host a `LAYER_NOT_DRAWN` layer (`native_view_host_mac.mm:63-67`). The `ViewsHostable` web contents are composited under `host_->layer()` (`GetUiLayer`, `:85-87`). That is also how `SetNativeViewCornerRadii` works on Mac (`:234-241`).
- **Effect:** Glance opens with no scale-in on Mac.
- **Suggestion:** fix the comment. Animating `web_view_->holder()->layer()` would probably give Mac the same scale and fade; unverified, so try it when Glance is tested on Mac.

### N2. Some Info.plist strings stay "Chromium" and are shared with Chromium

- **Where:** upstream `chrome/app/app-Info.plist:237, 265`.
- **What's wrong:**
  - `UTTypeDescription` is hard-coded as "Chromium Extension"/"Chromium Shortcut", and isn't localized or rebranded.
  - The exported UTIs `org.chromium.extension`/`org.chromium.shortcut` are the same ones Chromium declares.
  - `NSHumanReadableCopyright` is "The Chromium Authors" (`IDS_ABOUT_VERSION_COPYRIGHT`). That's deliberate credit, but it differs from `BRANDING`'s `COPYRIGHT`.
- **Effect:** Finder's "Kind" for `.crx` and `.crwebloc` files says Chromium.
- **Suggestion:** cosmetic; a one-line `branding.py` rewrite if wanted.

### N3. `JOBS=4` is conservative for an M4 with 4 performance and 6 efficiency cores (uncertain estimate)

- **Where:** `scripts/build.sh:24`.
- **Estimate:** with `symbol_level = 0`, most clang jobs need well under 1 GB, and a non-LTO lld link of the framework a few GB. So `JOBS=6` should still leave room for daily apps, and the first build (likely 8 h or more at `-j4` on a fanless Air) would be noticeably faster.
- **Why it can't fail hard:** macOS swaps under memory pressure instead of killing the build, so too high a `JOBS` only slows things down.
- **Suggestion:** keep 4 as the default, and suggest trying 6 while watching memory pressure in Activity Monitor.

### N4. A non-component build relinks the whole Chromium Framework on every incremental change

- **Where:** `build/koan-mac.gn:20`.
- **Effect:** right for a daily driver and for disk use. But when iterating on Mac-only Koan code, each change pays a full framework link (minutes, a few GB of RAM).
- **Suggestion:** a `koan-mac-dev.gn` with a component build could come later.
- **Disk:** fine. I estimate `out/Koan` at about 10-15 GB (uncertain; no Mac out dir exists yet), well under 25 GB. Nothing in the args (no symbols, no dSYMs because `enable_dsyms` follows `is_official_build`, no LTO or PGO) inflates it.

### N5. `bootstrap.sh` re-runs `gclient sync` when `chromium/src` is already clean at the pinned revision

- **Where:** `scripts/bootstrap.sh:51-72`.
- **What's wrong:** only a dirty tree skips the sync. Re-running bootstrap on a clean tree at `$REV` (for example, after `apply_branding.sh --revert` and `apply_patches.sh reverse`) makes several minutes of network calls for nothing.
- **Suggestion:** skip the sync when `head == REV` whether or not the tree is dirty. `gclient runhooks` still runs.

### N6. CONTRIBUTING's `env.sh` snippet doesn't match the real one

- **Where:** `CONTRIBUTING.md:95-105`, compared with `~/work/koan-mac/env.sh`.
- **Differences:**
  - The snippet leaves out `DEPOT_TOOLS_METRICS=0`, which the real file and `.agent/notes/mac.md` both have.
  - The snippet adds `mkdir -p "$HOME" "$TMPDIR"`, which the real file doesn't have.
- **Suggestion:** make them the same, so a fresh setup behaves like the maintainer's.

### N7. `app.icns` is built with every @2x variant, which isn't Chromium's convention (cosmetic)

- **Where:** `scripts/branding.py` (`MAC_ICON_SET`, `mac_assets`).
- **What's wrong:** `docs/mac/icons.md` ("`.icns` files") recommends 16/32/128 without @2x, plus 256 and 512 as PNG, because @2x variants of small sizes looked blocky on Retina.
- **Effect:** on macOS 26 and later, the Dock uses `Assets.car` (`CFBundleIconName`), so `app.icns` mostly matters before macOS 26 and for DMGs. Low priority.
