# macOS CI (issue #69)

The nightly macOS build in `.github/workflows/macos.yml`: how it works, why
it is built this way, and what was measured. User-facing docs are in
CONTRIBUTING ("macOS nightly build") and docs/USAGE.md ("Installing the
macOS build").

## Shape

Since the release pipeline (docs/RELEASING.md), the chain from fetch to
cleanup is the reusable `macos-chain.yml`: `macos.yml` (plan, then the chain,
then the nightly release) and `release.yml` (checks, the chain with
`release: true`, then the tagged release) both call it. Job names in the UI
gain a `Build /` prefix; the artifact names and re-running failed jobs are
unchanged (it is still one run).

```
fetch ──► build-01 ──► build-02 ──► … ──► build-16 ──► check ──► release
 (src)     (out-1)      (out-2)            (.dmg)                (develop)
```

- **Triggers:** the nightly (`cron 23 1 * * *`), `workflow_dispatch`
  (optional `ref`), and the `macos-build` label on a pull request (its
  head, once per labelling). Never on pushes: in the first two hours after
  review, every push to #70 started a 16 h chain that the next push
  cancelled. Pull requests only get ci.yml's actionlint + shellcheck. The
  label exists because `workflow_dispatch` only works for workflows on
  the default branch, so before merge a branch can't be dispatched.
- **plan** (ubuntu): decides whether to build. Not for other labels, not
  for PRs from forks
  (their scripts would run on 16 × 6 h of macOS runners), and not for a
  nightly when no build input (`src/ patches/ branding/ build/ scripts/
  CHROMIUM_VERSION VERSION .github/workflows/macos*`) changed between the
  `nightly` tag's commit and develop (GitHub's compare API; ≥ 300 changed
  files, a rewritten history or no tag mean build). The Chromium pin is in
  `CHROMIUM_VERSION`, so a new pin always builds. Manual runs always build.
  `check` and `release` are skipped with the chain, so a skipped nightly
  isn't a failure.
- **fetch** (`scripts/ci/mac_fetch.sh`): bootstrap, prune, patches,
  branding, `build.sh --gen`; uploads `koan-mac-source.tar.zst`.
- **build-NN** (`macos-build.yml`, a reusable workflow called 16 times):
  unpack source + `koan-mac-out-<NN-1>.tar.zst`, `mac_build_slice.sh`
  (build.sh until the deadline), then either upload
  `koan-mac-out-<NN>.tar.zst` (status `running`) or sign + package the
  .dmg (status `finished`). Job N+1 runs only if job N says `running`.
- **check**: fails the run when a build job failed or 16 weren't enough.
- **release**: develop only; deletes the `nightly` release and tag and
  makes them again at the built commit with the .dmg and its SHA-256
  (deleting and recreating avoids moving a tag with `GITHUB_TOKEN`, which
  can be refused when the new commit changes workflow files).

Re-running: every job reads only artifacts earlier jobs left under fixed
names and uploads with `overwrite: true`, so "Re-run failed jobs" on a run
carries on from the last good job. Retention (after the #70 review): source
3 days (14 for a run that keeps a base, see "Incremental builds", which
also keeps out-final 14 days), out slices 2 days, logs 7 days, .dmg 7 days
(the `nightly` release keeps the latest). So a failed job can be re-run for 2 days.

## Incremental builds

A full build is ~16 h, but most nightlies change a few files. Runs of
`macos.yml` and `release.yml` pass `incremental: true` to `macos-chain.yml`.
The rule (user, 2026-11-28): a full build happens only once per full-build
input change; after that every trusted build, nightly or release, is
incremental, and every finished trusted build is the base for the next.

**Only trusted runs become bases.** Once Nightly auto-updates users, objects
built from unmerged PR code must never be linked into what they install. So
`keep_base` is true only for a run that built develop itself (the nightly,
or a manual run on develop without a `ref` input: the `develop` output of
macos.yml's plan job, the same condition as the release job's) and for
release.yml's runs (a `v*` tag, which only org admins can create, on
develop or a release/* branch). Only such a run uploads
`koan-mac-out-final.tar.zst`, keeps its source 14 days and deletes older
runs' bases. Runs without it (a labelled PR, a manual run of another ref)
may still build on a base, but leave none: their source lives 3 days, they
upload no out-final, and their cleanup deletes only their own source and
out slices, as before incremental builds. `find_base.sh` (and
`prune_bases.sh`) only accept what `scripts/ci/trusted_base.sh` calls
trusted: a successful run of the nightly workflow on `$BRANCH` (develop)
whose event is `schedule` or `workflow_dispatch`, or a successful
release.yml run from a tag push or a dispatch, from develop or from a `v*`
tag that still points at the run's `head_sha` (a dispatch runs the workflow
files of its ref, and a branch may be named `v...` too), with both archives
unexpired; the missing out-final of any other run is the second lock.

**A release as a base.** A release's tree and out/Koan match a nightly's of
the same commit: the same gn args (`build/koan-mac.gn`), the update test
hooks off in both (`KOAN_CI_UPDATE_TEST_HOOKS=0` for `release` or
`keep_base`, `check_update_test_hooks.sh` on both), the version from
`VERSION` (`//koan/version`), no tag or event read by the build. The only
release-only steps are `KOAN_RELEASE=1` in `mac_package.sh` (the .dmg's
name, after out-final is packed) and the publish job's re-signing on its
own runner. So a nightly built on a release base carries nothing
release-only. A release run is a base only once it succeeded, i.e. after
the `release` environment's approval and the publish; one waiting for
approval isn't picked yet (and keeps its archives through other runs'
cleanups), and a failed one keeps its archives for a re-run until they
expire. A release on a release/* branch is a base too when it is the
newest: `incremental.sh` turns any trusted tree into the commit being
built, so that only costs rebuild time.

- The fetch job finds that run (`scripts/ci/find_base.sh`).
  **How the base is found:** it asks the API only for runs that can be a
  base, `actions/workflows/<file>/runs?branch=develop&event=schedule` and
  the same with `event=workflow_dispatch`, plus
  `actions/workflows/release.yml/runs?event=push` and `event=workflow_dispatch`
  (up to 3 pages of 100 each),
  merges them newest first (`created_at`), keeps the successful trusted ones
  (filtered by the script too: `?status=success` has left out a
  finished run for hours), and takes the first, other than this run, with
  both archives of its platform's prefix unexpired. It used to take the workflow's newest 50 runs of
  any kind and filter them; every label put on any pull request makes a
  run of `linux.yml` and `macos.yml` (`pull_request: labeled`), so a dozen
  labelled pull requests hid the base and every build was a full one
  (#537; #536 added `branch=` alone first). Shared by both chains; tests in `scripts/tests/test_find_base.py`
  (a fake `gh` over recorded runs). A pull-request build runs its own
  head's scripts, so a pull request branched before a find_base.sh fix
  needs develop merged in to get it.
  **When GitHub's API fails** (#589; both chains): every `gh api` call of
  `find_base.sh`, `trusted_base.sh` and `prune_bases.sh` goes through
  `gh_api` (`scripts/ci/gh_api.sh`): up to 4 tries, 5, 10 and 20 s apart;
  a 404 is an answer (no such tag) and isn't retried. It used to be
  `2>/dev/null || true`, so a transient 5xx or rate limit on the run list
  or a run's artifact list read as "no base", and PR #588's run
  46073436923 silently built all of Chromium although 46055294396 was
  there. **Decision: a lookup that still fails fails the step** (an
  `::error::` naming the call, GitHub's error above it, a line in the
  summary); re-run the job. A failed job costs minutes, a needless full
  build hours (~16 h here, 5 h+ on Linux) and delays every other build
  behind it. It fails on the first unanswered call, newest run first, so
  an older base is never taken while a newer run is unknown. Every run it
  passes over is logged with why (this run, not finished, not successful,
  not trusted, archives missing or expired), so a full build can be
  explained from the log. `prune_bases.sh` does the opposite on purpose:
  an unanswered read deletes nothing more and the step *succeeds* with a
  `::warning::`, because failing the cleanup would fail this keep_base run
  and `find_base.sh` would then refuse it as a base; what's left expires
  in 14 days. (A failed out-final list used to make every base look like
  it had only its source, i.e. "no base, delete it".) Tests: the fake
  `gh` of `test_find_base.py` and `test_prune_bases.py` fails a call once
  (`FAKE_GH_FAIL=<path part>:1`, retried, base found) or always.
  `scripts/ci/incremental.sh` then:
  - deletes the checkout's tracked files and unpacks that run's source
    archive in their place, mtimes and all;
  - lets `git status` against this commit's index say what differs, and
    restores just those files, so only they get new mtimes;
  - reverses the base's version of each changed patch and applies this
    commit's, with `git apply` as `apply_patches.sh` does.
    `GIT_CEILING_DIRECTORIES` keeps git from using Koan's repo for
    `chromium/src`, which has no `.git` by then. Without it, `git apply`
    run from a subdirectory of Koan's repo silently skips every path.
- It falls back to the full fetch (workspace wiped) when `CHROMIUM_VERSION`
  or any path in `KOAN_FULL_BUILD_INPUTS` (set by `mac_fetch.sh`: branding,
  the scripts that branch, patch, bootstrap, prune and check the tree, and
  `mac_fetch.sh` itself, which holds the `.gclient`) differs between the
  base's tree and this commit, or the toolchain does. The fetch writes
  `.cache/ci-toolchain` (`xcodebuild -version` plus the SDK version), so a
  runner image with a new Xcode or SDK gets a clean build. It also falls
  back when a patch won't reverse or apply, or when the archive is gone.
- Build 1 downloads the base's `out/Koan` (`koan-mac-out-final`) before
  building. If that download fails, the build is full but still correct,
  and build 1 says so: a `Lost base` warning and "base run N lost its
  out/Koan (...); building from scratch" in its summary.
- The job that finishes packs `out/Koan` as `koan-mac-out-final` **before**
  `mac_package.sh`, which signs `Koan.app` in place and adds the VMP
  signature. The next build then starts from the build's own output.
- With `keep_base`, source and out-final are kept 14 days. A successful
  run deletes its own out-N slices, and older bases as below.

**When bases are deleted** (both chains, `scripts/ci/prune_bases.sh` in the
`cleanup` job of a successful keep_base run; tests in
`scripts/tests/test_prune_bases.py`, a fake `gh`). A run picks its base when
its fetch starts, but build 1 downloads the base's out-final only when the
fetch is done, up to hours later. The cleanup used to delete every older
develop run's archives at once, so a develop build finishing in that window
turned the other build into a full one (#569: pull request #514's run
46028618466 picked 46019353569, develop run 46026497864's cleanup deleted
it at ~16:00 UTC, and build 1 built from scratch). Now:
- an older develop run of the same workflow whose run is still in progress
  keeps its archives (a running chain needs its source);
- one that is no base (not successful, or missing its source or out-final)
  loses both at once;
- a base stays until a newer base has existed for 12 h (`GRACE_HOURS`):
  the newer one counts from its run's `updated_at`, this run's from now.
  12 h covers the fetch's 5 h timeout plus waiting for a runner; after
  that every run's `find_base.sh` has picked the newer base. With one
  develop build a day, the previous base goes with the next day's cleanup;
  a busy day keeps two or three (~7 GB each).
- It looks the archives up by name (`actions/artifacts?name=`) and checks
  each run's own status and workflow (`gh api .../runs/<id>`), not a page
  of the workflow's runs (macos-chain.yml used the newest 50, which #537
  showed can miss develop's). Releases and nightlies supersede each other;
  a release run that is no base keeps its archives (a re-run of a failed
  16 h release needs them). Pull requests' and other refs' archives are
  never touched.

If a base is lost anyway (a build 1 re-run days later), build 1 reports it
as above. Retrying with the next base belongs in the fetch, but
`linux_fetch.sh` and `mac_fetch.sh` are in `KOAN_FULL_BUILD_INPUTS`: any
change to them makes the next build of every branch a full one, so #569's
fix left them alone.

- Why `git status` and `git apply` rather than rsync and GNU patch (the
  first Linux version): macOS ships openrsync and BSD patch, which lack the
  needed options.
- Tested by `scripts/tests/test_incremental.py` on a throwaway repository
  (patches added, changed and removed; files added, changed and deleted;
  unchanged files keep their mtimes; a changed fetch input, Chromium or
  toolchain falls back).
- 2026-11-24, tnfssc/koan run 45281812693 (a1141b7, no base: full): fetch
  35 min, then 3 build jobs (4 h 57 min, 4 h 57 min, 4 h 47 min), ~15 h
  15 min end to end. Kept: source 5.5 GB, out-final 1.5 GB; .dmg 186 MB.
- 2026-11-25, tnfssc/koan run 45513425253 (2a7f638: develop merged in, 17
  files changed incl. 12 under src/, 0 patches): incremental on
  45281812693. Fetch 20 min (download 7 min, unpack 3 min, gn gen 30 s);
  build 1 ran 38 steps and made the .dmg in 23 min; ~44 min end to end.
  Cleanup deleted 45281812693's source and out-final.
- `xcode-27` capacity: from 14:21 to 20:00 UTC on 2026-11-24, five jobs
  on the fork waited 15 min for a runner and were cancelled ("The job was
  not acquired by Runner of type hosted even after multiple attempts"),
  while this repo's nightly got runners. The next try, at 23:51, got one.

Concurrency: one group per ref for nightly and manual runs
(`macos-<ref name>`; the nightly and a manual run of develop share
`macos-develop`, so they never race on the release), never cancelled, at
most one waiting. Pull requests: `macos-pr-<number>`, cancelled by the next
push. The release job also has its own `macos-nightly-release` group.

Review of PR #70: [reviews/pr70-review.md](https://github.com/Chaitanya045/koan/blob/ac644383c4884c21aaa7a2a7fc23694e5a5859e0/.agent/notes/reviews/pr70-review.md).

## Resuming

A job resumes the previous one's siso state (out/Koan with `.siso_fs_state`
and `.siso_deps`, mtimes kept by the archives). Anything outside the
archives that siso sees differently in the next job can make it redo
finished work, and that stalls the chain: each job spends its 5 h on the
same steps and 16 jobs never finish.

**Runner image dates (issue #534).** `develop` run 45816128161 (full
build): build-01, 02 and 04 ran on `xcode-27-arm64` 20261104.0337.1,
build-03 on 20261123.0387.2. Build-03 recompiled 21,653 of build-01's
22,539 objects, and build-04 the same again. siso's own reasons, from the
`siso_explain` files in `koan-mac-out-4` (`siso_explain.0` is build-03's)
and the two `.siso_fs_state` files:

1. `//build/modules:mac_sysroot` and `//build/config/mac:sdk_inputs` are
   `noop.py` actions whose declared outputs are the SDK's `usr/include`
   (through `out/Koan/sdk/xcode_links`), so siso's state records 3,451 SDK
   headers as generated files, with their mtime and when the action last
   ran (GN actions are `restat`, so that time is what siso compares).
2. The headers' mtime is when the runner image was made: `mach/exc.defs`
   is 2026-11-04 04:52:34 UTC on 20261104 and 2026-11-23 16:08:34 on
   20261123. When the disk doesn't match the state, siso drops the entry
   (hashfs `handleBeforeLocal` / `handleAfterLocal`), with it the action's
   run time, and then finds the header "older than most recent input
   ../../build/noop.py", so it runs the action again. Build-02, on the
   same image as build-01, skipped it.
3. The rerun makes `gen/build/modules/module.modulemap` dirty ("input
   out/Koan/phony/build/modules/mac_sysroot: input
   .../usr/include/arpa/nameser.h: dirty"), which rebuilds
   `obj/build/modules/system/module.pcm` (Clang's system module), and every
   object that imports it is dirty: 23,064 steps in build-03, 23,253 in
   build-04 ("deps for … dirty: input
   out/Koan/obj/build/modules/system/module.pcm: dirty").

The out archive, `.siso_deps` and the `xcode_links` symlinks (same target
on both images, made on 11-26 by build-01's gn gen) were fine.

Fix: `mac_build_slice.sh` gives every file of the SDK one mtime
(2001-01-01 UTC, `sudo find … -exec touch -h`) before siso starts, so the
state matches the disk on any image. Only when Xcode and the SDK are the
ones the fetch job recorded in `.cache/ci-toolchain` (same Xcode build, same
headers); with another, the mtimes stay, siso rebuilds what the new SDK
changes (correct), and the resume check below is off. The first incremental
build on a base from before the fix rebuilds once.

**The resume check.** A job that stops at the deadline writes the steps
siso had left to `out/Koan/koan-ci-resume` with the run id
(`scripts/ci/build_progress.py record`). Ten minutes into the next job, if
siso's total is above 1.25 × that + 1,000, the job stops siso and fails
with an `::error::` and the head of `siso_explain`. A healthy resume
settles within 2 min at or below what the previous job left (build-02:
49,796 vs build-01's 52,255 left); build-03 and 04 settled at 68,900 vs
36,348 and 66,634 vs 39,437. The first `[0/N]` total doesn't tell: build-02
started at 90,434. Nothing to compare in slice 1 (no state, or a base
run's).

**Logs.** Each job prints the image (`$ImageOS $ImageVersion`), Xcode, the
SDK path, the mtimes of `SDKSettings.json` and `mach/exc.defs`, and the
`xcode_links` targets, before the build and after it (gn gen can replace a
link), into the job log and `toolchain.txt` in `koan-mac-logs-N`, with
`siso_explain` (why siso ran each step it ran). The job summary has the
image. `out/Koan/metal-toolchain` points into a cryptex mount whose name
changes every boot, but no step's inputs go through it (no siso_explain
line).

**Linux chain** (`linux-chain.yml`, `linux_build_slice.sh`): not exposed
to this. Its compile steps use Chromium's clang and Debian sysroot from the
source archive (`use_sysroot = true`), and `mac_sysroot`/`sdk_inputs` are
Mac-only; the runner's files only run the build (python3, the shell), and
siso doesn't track those. Inferred from the build files, not from a
resumed Linux job's explain. It has the same resume check and copies
`siso_explain` to its logs.

## Release signing

- **Nightly:** ad hoc (`codesign -s -`, `mac_package.sh` without
  `KOAN_CODESIGN_IDENTITY`), unchanged.
- **Tagged release:** `release.yml`'s `publish` job (macOS, `xcode-27`, the
  only job with `environment: release`, so one approval) takes Koan.app out
  of the build's ad hoc .dmg, imports the certificate into a temporary
  keychain (`scripts/ci/mac_codesign.sh setup`), and runs `mac_package.sh`
  with `KOAN_PACKAGE_APP` (real app: Info.plist check and Widevine VMP step
  run, the product name is the bundle's), `KOAN_CODESIGN_IDENTITY` and
  `KOAN_CODESIGN_KEYCHAIN`. It prints the signing authority and designated
  requirement; the final .dmg has the build's file name. Then the update
  manifest (`koan-update.json`, `scripts/ci/update_manifest.py`) and the
  upload. An `if: always()` step deletes the keychain and the update key.
- **One fixed identity:** self-signed "Koan Release Signing", SHA-1
  `41C53B537255BA68D030856528BDACCD559E0B50` (`scripts/ci/codesign_identity.txt`;
  `setup` fails on any other). The designated requirement is then
  `identifier "<bundle id>" and certificate root = H"41c5..."` (the
  throwaway certificate of the checks gets `certificate leaf`; for a
  self-signed certificate both name the same one), the same in
  every release, so keychain items and in-app updates see one app. No
  Developer ID, no notarization: Gatekeeper still asks the user.
- **Secrets** (environment `release`): `KOAN_CODESIGN_P12` (base64 of the
  .p12), `KOAN_CODESIGN_P12_PASSWORD`, `KOAN_UPDATE_SIGNING_KEY` (ed25519
  private key, PEM; the manifest's signature). The .p12 **must be 3DES/SHA-1**:
  `openssl pkcs12 -export -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES
  -macalg sha1`; macOS `security import` can't read OpenSSL 3's default AES
  files.
- **Check before a 16 h build:** `mac-signing-check.yml` (on pull requests
  that touch the signing files, and by hand): job `throwaway` signs a dummy
  app with a made-up certificate and verifies the .dmg and a manifest made
  with a throwaway key (no secrets); job `real` (`workflow_dispatch` with
  `real=true`, from develop or a v* tag, owner approves) does it with the
  real secrets and checks the committed public key. `macos-chain.yml`'s
  "Check the packaging" also signs its dummy app with a throwaway
  certificate.
- **Untrusted certificate:** `mac_codesign.sh` probes that `codesign -s <sha1>
  --keychain <kc>` signs with it; if not, it trusts the certificate for code
  signing (`security add-trusted-cert -p codeSign`, admin domain through
  `sudo -n`) and `teardown` removes that. Whether the fallback is ever
  needed is only known from the check's first run.

## How ungoogled-chromium-macos does it (checked 2026-11-02)

- `.github/workflows/building.yml`: `retrieve-resources` job, then 20
  copy-pasted `build_job_NN` jobs, each `needs` the previous and runs only
  if its `status` output is `running`.
- `github_build.sh`: `timeout -k 7m -s SIGTERM <6 h - 1 h - elapsed> ninja
  -C out/Default chrome chromedriver`; exit 124 → `status=running`.
- `github_prepare_artifacts.sh`: `tar -C build -c src | zstd -11 -T0`: the
  whole `src` including `out/`, one artifact (`overwrite: true`) that the
  next job unpacks. The job that finishes signs, notarises and makes the
  .dmg.
- Disk: they delete `$ANDROID_HOME`, all Xcodes but one and the simulators,
  and turn Spotlight off.
- Sources: their own `clone.py` (git), plus prebuilt LLVM/Rust/node from
  their `downloads-*.ini`; they prune binaries and apply domain
  substitution, which Koan doesn't need.
- In January 2026 (PR #305) they **moved from GitHub's runners to Depot**:
  "the build finished in ~9 hours, whereas the GitHub runner took ~3 days
  (and did not finish)". Their build is heavier than Koan's
  (`is_official_build`, ThinLTO, PGO, `symbol_level = 1`, plus
  chromedriver), but it is a warning about the free runners' speed.

What Koan does differently: source and out are separate artifacts (the
source is uploaded once; each job uploads only `out/Koan`), siso instead
of ninja (as `build.sh` and the maintainer's Mac use), SIGINT rather than
SIGTERM (siso saves its state), POSIX tar so mtimes keep nanoseconds, and a
reusable workflow instead of 20 copies of the job.

## Getting the source

- **No tarball of the pinned tree.** `chromium-browser-official` has
  `chromium-158.0.8111.0{,-lite,-testdata}.tar.xz` (4.5 / 2.1 / 1.1 GB,
  published 2026-10-19), but they are made from the tag `158.0.8111.0` =
  `ff682ad`, a branch commit ("Create chrome_branch_deps.json for branch
  8111") whose parent is ~100 main commits after the pinned `67e29b8`.
  Using it would mean building a different Chromium than the Linux bench.
  #86 moves the pin to release tags (the M158 branch, then every Stable
  point release); then `chromium-<version>-lite.tar.xz` matches the pin
  and becomes an option (~2.1 GB download), but it has no CIPD/GCS deps
  and no hook output (clang, Rust, node, Xcode checks), so gclient would
  still be needed for those. Not worth it while the gclient fetch takes
  ~10 min.
- **Nothing in the CI names a Chromium version or commit:** bootstrap.sh
  reads `CHROMIUM_VERSION` (line 1 version, line 2 commit), and the .dmg
  name falls back to line 1. Moving the pin is a one-line change; the
  next run fetches the new tree (a full rebuild, as on any machine).
- **What CI does:** `scripts/bootstrap.sh` unchanged (`gclient sync
  --nohooks --no-history --revision src@<rev>`, then `gclient runhooks`)
  with a `.gclient` for `target_os = ["mac"]` and
  `checkout_configuration = "small"` (drops the GPU Meet test assets and
  press benchmarks). NaCl no longer exists in this Chromium; the Android,
  iOS, Fuchsia and ChromeOS deps are already off for `target_os mac`.
- **Pruning:** `scripts/ci/prune_chromium_source.py` uses the lists
  Chromium's own lite tarball is made with (`publish_tarball.py` and
  `export_tarball.py` in chromium/tools/build, `recipes/`): test data, web
  tests, `v8/test`, benchmarks, and android/iOS/chromecast trees reduced to
  their GN/GRIT/licence files. It keeps the toolchains the recipe drops
  (llvm-build, rust-toolchain, node), since Koan builds with Chromium's
  own. Distributions build `chrome` from the lite tarball, and `gn gen` in
  the fetch job checks that the Mac build files still load.
- Hermetic Xcode isn't used (that needs Google-internal access); the
  runner's Xcode is selected with `DEVELOPER_DIR` (`scripts/ci/mac_env.sh`).

## Build configuration

`build/koan-mac.gn` from #52, unchanged: arm64, `is_debug = false`, no
component build, `symbol_level = 0`, `dcheck_always_on = false`, no
`is_official_build` (so no ThinLTO and no PGO). Official builds were left
out because they don't fit the chain:

- A ThinLTO link of the Chromium framework is one step that needs well
  over the runner's 7 GB of RAM and takes hours on 3 cores. A step can't
  be split across jobs: siso stops at the deadline and the link starts
  over in the next job, so a link longer than one ~5 h slice never
  finishes.
- ungoogled-chromium's official build (ThinLTO + PGO) "took ~3 days and
  did not finish" on these runners before they moved to Depot.

PGO without LTO (`is_official_build` with `use_thin_lto = false`) might
fit; worth measuring once the plain build's time per job is known. It
would change the maintainer's Mac build too, since CI uses the same file.

## Runner

- `xcode-27` (public preview label, standard = free for public repos):
  macOS 27.0.1, Xcode 27.1 default (also 27.0 and 27.2 RC), SDK
  `macosx27.0`. Chromium 158's `mac_sdk_official_version` is 27.0, and the
  maintainer's Mac has Xcode 27.1, so CI compiles against the same SDK.
  `macos-26` only has Xcode ≤ 26.6, `macos-15` ≤ 26.3 (their SDKs satisfy
  `mac_sdk_min = 15` but not the SDK the Mac uses).
- Hardware as documented: 3 vCPU (M1), 7 GB RAM, 14 GB guaranteed SSD.
- Has GNU tar (`gtar`), zstd, python3, gh.

## Measurements

**First green run: 41420273033** (2026-11-02 09:23 → 11-03 01:28 UTC,
**16 h 05 min**), Koan d5c85c0 on Chromium 158.0.8111.0:

| Job | Wall time | Build | Steps done | Rate |
|---|---|---|---|---|
| fetch | 28 min | | | |
| build-01 | 4 h 56 min | 4 h 42 min | 27,940 | 1.65/s |
| build-02 | 4 h 57 min | 4 h 44 min | 9,883 | 0.58/s |
| build-03 | 4 h 57 min | 4 h 48 min | 17,485 | 1.01/s |
| build-04 | 46 min | 38 min | 2,779 (incl. links) | 1.22/s |

4 chained build jobs (of 16 available), ~58k steps run (siso skipped the
rest as no-ops). Artifacts: source 5.8 GB; out-1/2/3 0.56/0.73/1.05 GB;
**`Koan-158.0.8111.0-d5c85c0-arm64.dmg` 183 MB**, `Koan.app` 554 MB
(arm64, `codesign --verify --deep --strict` OK, `hdiutil verify` OK).
Memory never below ~44% free at `JOBS=3`; swap ≤ 130 MB.

**Second green run: 41577942839** (2026-11-03 04:24 → 20:07 UTC,
**15 h 42 min**), Koan 764f834 = develop with Spaces (#57), Library (#64)
and media/toasts (#66), the first Mac compile of those: no Mac-only errors.
Fetch 65 min (googlesource slow: sync 44 min, one HTTP 500 retried, one
lost dependency taken from the tarball); build-01 28,537 steps (4 h 47 min
build), build-02 10,804 (4 h 44 min), build-03 the last 18,760 and the
.dmg (4 h 33 min build): **3 build jobs**. .dmg 183 MB, app 555 MB; the app
in the mounted .dmg verifies. The cleanup job deleted source and out-1/2
at the end, leaving the .dmg and logs.

Run 41560986331's fetch failed on a transient googlesource HTTP 500
("RPC failed; HTTP 500 ... expected 'packfile'") while gclient cloned
`third_party/clang-format/script`. `bootstrap.sh` now retries `gclient
sync` up to 3 times (a re-run of bootstrap.sh wouldn't do: src is already
at the pinned revision then, so it skips the sync). "Re-run failed jobs"
also works for this.

**googlesource incident, 2026-11-03 (two lost commits):** fetching
`third_party/clang-format/script` at `9f796802` (pinned by our DEPS, by
main and by 158.0.8127.2) fails with HTTP 500 (`--depth=1`) / 400, three
gclient attempts in a row, and gitiles answers 404 for the commit; the
mirror's main moved to 47dce84. It worked on 11-02 09:30. The scripts are
only `git cl format`'s (`third_party/clang-format` has no BUILD.gn), so the
CI's `.gclient` drops it and webpagereplay's copy via `custom_deps`. Any
fresh `gclient sync` of this Chromium (the Mac, #89's re-sync) hits the
same until googlesource fixes it; the same two `custom_deps` lines work
around it.

The same happened to `third_party/search_engines_data/resources` at
`5dc9f1af` (also pinned by main and 158.0.8127.2; gitiles 404), which the
build does need (`prepopulated_engines.json`). Worse, gclient's second
sync attempt reported success with the directory missing (the first had
moved it to `_bad_scm/`), so the fetch passed and build 1 failed after 8 s
("missing and no known rule to make it"). `scripts/ci/mac_check_deps.py`
now runs after the sync: every git dependency in `gclient revinfo` must be
a non-empty directory; a missing one is taken from Chromium's official
`chromium-<CHROMIUM_VERSION line 1>-lite.tar.xz` (streamed once, only those
paths extracted) if that tag's DEPS pins the same commit — 158.0.8111.0's
does — else the fetch fails right there.

The .dmg is uploaded with `archive: false`, so the browser download is the
.dmg itself. `gh run download` (2.96) can't fetch such artifacts ("zip:
not a valid zip file"); `gh api .../actions/artifacts/<id>/zip` returns
the raw .dmg (checked: same SHA-256 as on the runner). docs/USAGE.md shows
that.

**Runner** (`xcode-27`, run 41402833326): 3 × "Apple M1 (Virtual)", 7 GB,
a 150 GB disk with 36 GB free; `mac_runner_setup.sh` frees it to
**101 GB** in ~2 min (two extra Xcodes, Android SDK, .NET, tool cache,
browsers). The "14 GB" in GitHub's docs is only the guaranteed minimum.

**Fetch job** (run 41404564286), 22.5 min:

| Step | Time | Result |
|---|---|---|
| Runner setup | 1.8 min | 101 GB free |
| `gclient sync --no-history` (mac, small) | 9.8 min | |
| `gclient runhooks` | 0.6 min | `chromium/src` 29 GB |
| Prune (lite tarball lists) | 3.3 min | 21 GB |
| Patches + branding | 0.2 min | |
| Delete `.git` (src's is 1.4 GB) | 0.2 min | 18 GB |
| `gn gen` | 0.2 min | |
| Pack (gtar posix + zstd -3, 3 threads) | 4.5 min | **8.1 GB** archive |
| Upload | 1.3 min | ~105 MB/s |

Packed: `chromium/src` 18 GB, `depot_tools` 0.85 GB, `.cache/mac` 2.9 GB
(vpython and CIPD caches).

A first run failed at `gn gen`: the pruning had removed
`third_party/blink/tools/*.pydeps`, which `//infra/orchestrator` reads.
The lite tarball keeps every `*.gn`, `*.gni`, `*.grd(p)`, `*.isolate` and
`*.pydeps` in the directories it drops; the script now does the same.

The first build job then failed after 11 s: Dawn's
`tint:generate_sources` runs `third_party/dawn/tools/golang/mac-arm64/bin/go`,
which the lite recipe purges (distributions supply their own Go). Koan
keeps it. `third_party/rust-src` can go: Chromium's Rust `std` builds from
`third_party/rust-toolchain/lib/rustlib/src`.

The next attempt failed after 1m52s (679/90,009 steps) at ANGLE's
`angle_metal_internal_shaders_to_air` (`xcrun metal`, through
`metal_wrapper.py`): "cannot execute tool 'metal' due to missing Metal
Toolchain", after running for 96 s. Since Xcode 26 the Metal toolchain is
a separate component, but the runner image has it installed
(`xcodebuild -showComponent MetalToolchain`: build 27A266a, mounted as a
cryptex under `/private/var/run/com.apple.security.cryptexd/mnt/`).
The next run got past that step but failed at `xcrun metallib` 27 min
in (step 8,418): "xcrun: error: sh -c 'xcodebuild -sdk .../MacOSX.sdk
-find metallib' failed with exit code 17664" (exit 69). Probe jobs on a
scratch branch found the cause: **under `CFFIXED_USER_HOME`, `xcodebuild
-find metallib` fails** ("'metallib' not found", exit 69); with plain env,
`HOME` or `TMPDIR` alone it works. xcrun only asks xcodebuild when its
own cache misses, which is why the failure moved between the `metal` and
`metallib` steps. The one-folder rule (`koan_require_mac_env`, #52) sets
`CFFIXED_USER_HOME`, so the maintainer's Mac would hit this too.

Fix, in `scripts/build.sh` for every macOS build: resolve the toolchain
once with `env -u CFFIXED_USER_HOME xcrun -f metal` (it lives in a cryptex,
`/private/var/run/com.apple.security.cryptexd/mnt/com.apple.MobileAsset.MetalToolchain-v<ver>.<random>/Metal.xctoolchain/usr/bin`,
whose name changes on every boot), link it as `out/Koan/metal-toolchain`,
and set ANGLE's GN arg `angle_metal_toolchain_dir` to the link, so ANGLE
runs `metal`/`metallib` without xcrun. The runner image already has the
toolchain installed (`xcodebuild -showComponent MetalToolchain`: build
27A266a); `mac_runner_setup.sh` compiles a test shader and installs the
component only if that fails.

**Build job 1** (run 41420273033), 4 h 56 min in all:

| Step | Time |
|---|---|
| Runner setup (cleanup, Metal check) | 2.4 min |
| Download source (5.8 GB, without the CIPD cache) | 8.0 min (~12 MB/s) |
| Unpack | 2.6 min |
| Build (`JOBS=3`) | 4 h 42 min: 27,940 steps, 1.65 steps/s |
| Pack out/Koan (0.5 GB) + upload | 1.2 min |

Siso stopped on SIGINT at the deadline with "0 failed, interrupt by
signal". Memory stayed at 62–74% free; at most 127 MB of swap, briefly.
`JOBS=3` is safe; 4 might be worth a try.

**Build job 2** resumed where job 1 stopped: its first progress line was
`[796/48012]` (48k left, not 79k), so unpacking source and out with their
mtimes works. Overhead 11 min (out-1: 25 s download, 23 s unpack). Build
4 h 44 min: 9,883 steps at **0.58 steps/s** (Blink, V8, big C++ files),
0 failed, 34,890 left; memory 60–76% free, ≤ 70 MB swap. out-2 0.7 GB. Rate: ~5 steps/s in the first
half hour (small C++ files), ~1.2 steps/s later. The total siso reported
shrank from 91k to 79.3k as it learned which steps were no-ops
(`skip:17340`).

**Build size:** 91,259 steps for `chrome` with `build/koan-mac.gn` (siso's
count at the start of build 1).
