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
  compatible. `iconutil`, `sips`, `actool` exist; `rsvg-convert` and
  ImageMagick don't, and aren't needed: the PNGs the Mac uses are rendered
  on Linux and committed (`branding/mac/rendered/`, refreshed with
  `apply_branding.sh --render-mac` after an SVG changes).
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
  keychain item after every rebuild. `scripts/mac_signing_identity.sh create`
  once, and `scripts/build.sh` signs every build with one local identity
  (below, "Signing builds with one identity (#141)").

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
