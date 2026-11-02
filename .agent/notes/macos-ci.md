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
carries on from the last good job. Intermediate artifacts live 5 days, the
.dmg 30 days, logs (build log, memory log) 14 days.

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
  If `CHROMIUM_VERSION` ever pins a release tag instead, the lite tarball
  (+ CIPD/GCS deps and hooks, which the tarball doesn't have) becomes an
  option.
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
