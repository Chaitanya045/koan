# Releasing from the maintainer's Mac (interim)

How a Koan release is built, tested and published **from the maintainer's Mac** while
release.yml can't build the official configuration on GitHub's hosted runners
(docs/RELEASING.md, "Official builds"). 0.0.1-alpha.1 and 0.0.2-alpha.1 were made this
way. **This is temporary:** the plan is to move the whole build into GitHub Actions
(a larger or self-hosted Apple Silicon runner for the official config, per RELEASING.md's
"What would make it work"). When that lands, release.yml's tag-push flow replaces steps
2-5 here, and this note shrinks to "install it on the Mac" (step 7).

Nothing about the Mac (host name, address, user, key path) goes into tracked files: CI's
"Private details" check fails on them. Below: `<mac-host>`, `<user>`, `<ssh-key>`.

## 0. Rules
- Only with the user's go-ahead for this release. They say whether they're at the Mac.
  "Full throttle" means `-j 10` dev and `--away` official builds; otherwise `-j 3`/nice
  (dev) and the default `-j 4` (official).
- One build or test run at a time: 16 GB RAM. The official build takes
  `$KOAN_MAC/tmp/e2e.lock` itself. Don't start the official build while the dev build,
  or a suite run, is going.
- Never quit or replace the user's own Koan (`~/Applications/Koan.app`) without asking:
  agents may be driving it.
- Scripted UI tests (real keys and pointer) need the screen: the user away, and
  `tmp/screen-granted` present (`tools/grantok.sh`, `usergate.sh` 600 s idle). Otherwise
  those steps SKIP. Say which ones skipped.

## 1. Reaching the Mac
- From the Linux box: `ssh -i <ssh-key> <user>@<mac-host>` (Tailscale). Use a
  ControlMaster with a short ControlPath (`/tmp/kcm-%C`). Long paths fail.
- Every command on the Mac starts with `source ~/work/koan-mac/env.sh`. It points HOME and
  TMPDIR into `~/work/koan-mac`, so Chromium's caches stay there. The checkout is
  `~/work/koan-mac/koan`, with `chromium/src` inside it.
- **Staying awake:** the LaunchAgent `~/Library/LaunchAgents/com.koan.keepawake.plist`
  runs `caffeinate -d -i` at login (KeepAlive), and `pmset` has AC `sleep 0`. That's
  enough while the lid is open. With the lid closed the Mac sleeps anyway, unless
  `sudo pmset -a disablesleep 1` is set. The user chose not to (2026-11-23: "I'm not going
  to close the lid"). Check with `pmset -g assertions | grep caffeinate`.
- **Keep it on the charger.** `caffeinate` doesn't stop the low-battery sleep: during 0.0.2's official build, on
  battery, the Mac went into "Low Power Sleep" at 1% (06:04) and was offline for 2 h. The build itself carried on
  after it woke. Check `pmset -g batt` before a long job.
- Run long jobs detached: `nohup /bin/bash <script> >/dev/null 2>&1 </dev/null & disown`.
  The scripts log under `~/work/koan-mac/logs/`. Poll the state file instead of holding
  ssh open.

## 2. Dev build + C++ tests (out/Koan)
- `~/work/koan-mac/cycle-build.sh [extra-branches...]`:
  1. `git checkout -B cycle2 origin/develop` and merge any extra branches;
  2. `sync-patches.sh` (rewrites only the patched files that changed, so ninja rebuilds
     little);
  3. `apply_branding.sh`;
  4. regenerates `out/Koan/args.gn` from `build/koan-mac.gn` only if it changed;
  5. `autoninja -j 10` of `chrome` plus every `koan_*unittests` and `koan_browsertests`.
- State in `logs/cycle1.state` ("c2 building", "cb built", "BUILD FAIL ..."), output in
  `logs/cycle-build.log`. 23 commits after a release, or a VERSION change, rebuilt about
  11k steps.
- Then `run-suite.sh --skip-e2e --skip-visual` (scripts/mac/suite.sh step 1) for the
  C++ tests. Results go in `$(cat logs/suite.dir)/summary.txt`.

## 3. E2E and regression (the Mac suite)
- `run-suite.sh --only <names>` for the release's new features first (their own
  `scripts/mac/suite` entries and `scripts/mac/steps/*.md` by hand), then the full
  `run-suite.sh` (every `suite/e2e.list` entry, visual, crash grep).
- Read `summary.txt`: `PASS`/`FAIL`/`SKIP` per test and a `TOTAL` line. Triage each
  FAIL: product bug, harness, or a stale expectation (cycles 14-19 in `.cache/c*-triage`).
- Steps that need a real trackpad (the Space swipe, haptics) are the user's, from
  `scripts/mac/full-flow.md`. List them in the hand-off.

## 4. Official build (out/KoanOfficial)
- `scripts/mac/build_official.sh --away` (or without `--away` while the user works).
  `--no-sync` only when the dev build's sync already left `chromium/src` at the same
  commit. It applies the patches and branding, makes sure the PGO profiles are there,
  runs gn gen, builds `chrome`, VMP-signs it when EVS is set up, and signs it with the
  **local identity** (`scripts/mac_sign.sh`), so the user's saved logins survive an
  install.
- Incremental after a release: the changed objects plus the framework's ThinLTO link
  (1-2 h, 4 threads on 16 GB). A first build takes ~10-16 h. **A patch to a widely included upstream header** (e.g.
  `content/public/browser/web_contents_delegate.h`, #435) makes it nearly a full rebuild: 0.0.2's was 51.8k steps,
  3 h 52 min at `-j 6` (`--away` picks 6 to 8 jobs from the free RAM), then the link. Look at
  `git diff --stat v<prev> -- patches/` first to estimate. Log in
  `koan/logs/official-<stamp>.log`.
- Check `Info.plist`: `KoanVersion` == `VERSION`, `CFBundleShortVersionString` ==
  `python3 -I src/version/koan_version.py show short`.

## 5. The DMG
- `scripts/ci/mac_package.sh` signs **in place** (ad hoc), so never point it at
  `out/KoanOfficial/Koan.app` itself: that would replace the local-identity signature the
  install needs. Copy it first:
  ```sh
  ditto chromium/src/out/KoanOfficial/Koan.app ~/work/koan-mac/release/<v>/stage/Koan.app
  KOAN_CI_XCODE=/Applications/Xcode.app KOAN_RELEASE=1 \
    KOAN_CI_APP=~/work/koan-mac/release/<v>/stage/Koan.app \
    scripts/ci/mac_package.sh ~/work/koan-mac/release/<v>
  ```
  `KOAN_CI_APP` skips the script's Info.plist check, so do step 4's check by hand. `KOAN_CI_XCODE`: the script
  sources `scripts/ci/mac_env.sh`, whose default Xcode path is the CI runner's.
- Before packaging, run a short `suite.sh --skip-cxx --skip-visual --only ...` on the **official** app (0.0.2: 10
  e2e, all passed). The
  result is `Koan-<v>-arm64.dmg` and its `.sha256`.

## 6. Publish on GitHub
- On `develop`, in one PR: `VERSION` is the version; `docs/releases/<v>.md` with every
  `TODO(release)` gone; the "Build" section says *official (ThinLTO + PGO), built on the
  maintainer's Mac from `<sha>`*; and the CHANGELOG line. Merge it.
- Check the version and notes, and get the title:
  `python3 -I scripts/ci/release_info.py check --tag v<v>`. It prints `title=`, `previous_tag=`, and so on. Fill in
  the notes with `release_info.py notes --notes docs/releases/<v>.md --dmg <dmg> --sha256 <hash> --output
  <notes.md>`, which is what release.yml runs.
- Create the release and its tag in one step, on the exact commit the Mac built:
  `gh release create v<v> <dmg> <dmg>.sha256 --target <sha> --title "<title>" --notes-file
  <notes.md> --generate-notes --notes-start-tag <previous_tag> --latest`.
  It must be a full release, not a pre-release (RELEASING.md). The tag push starts
  release.yml, but its `prepare` job refuses a tag that already has a release, so it
  fails within minutes and builds nothing. Check that it did (`gh run list -w
  release.yml`). Then download the asset and compare its SHA-256.
- After the release, bump `VERSION` to the next one in a PR (RELEASING.md step 6).

## 7. Install it as the user's daily Koan (only when asked)
Quit Koan (⌘Q, asked of the user, since agents may be driving it), then:
```sh
ditto chromium/src/out/KoanOfficial/Koan.app ~/Applications/Koan.app.new &&
  rm -rf ~/Applications/Koan.app.prev &&
  mv ~/Applications/Koan.app ~/Applications/Koan.app.prev &&
  mv ~/Applications/Koan.app.new ~/Applications/Koan.app
```
Same local identity, so logins and the keychain key keep working.
`~/Applications/Koan.app.prev` is the way back. Check About Koan.
