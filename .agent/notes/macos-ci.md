# macOS CI (issue #69)

The nightly macOS build in `.github/workflows/macos.yml`: how it works, why
it is built this way, and what was measured. User-facing docs are in
CONTRIBUTING ("macOS nightly build") and docs/USAGE.md ("Installing the
macOS build").

## Shape

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
3 days, out slices 2 days, logs 7 days, .dmg 7 days (the `nightly` release
keeps the latest). So a failed job can be re-run for 2 days.

Concurrency: one group per ref for nightly and manual runs
(`macos-<ref name>`; the nightly and a manual run of develop share
`macos-develop`, so they never race on the release), never cancelled, at
most one waiting. Pull requests: `macos-pr-<number>`, cancelled by the next
push. The release job also has its own `macos-nightly-release` group.

Review of PR #70: [reviews/pr70-review.md](reviews/pr70-review.md).

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
