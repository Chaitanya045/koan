# Review of PR #70 (ci: nightly macOS build, issue #69)

Reviewed at head e112a59 (worktree `~/.t3/worktrees/koan/ci-macos-nightly`), read only. File:line refer to that head.
Evidence: the PR diff, run 41420273033 (jobs, artifacts, step timings), repo and Actions settings via `gh api`.
Not available here: shellcheck and actionlint (not installed). I read the scripts by hand instead. The repo's own ci.yml runs shellcheck on `scripts/ci/*.sh`, so that will catch the rest.

Counts: 0 Blocker, 5 Should-fix, 6 Nit.

## Security: no Blocker

Verified fine:
- Top-level `permissions: {}` (macos.yml:33). `contents: read` on `fetch` and the build callers. `contents: write` only on `release` (macos.yml, `release` job). `check` has `permissions: {}`. macos-build.yml uses `contents: read`.
- Actions are pinned by full SHA with a tag comment. I checked that the SHAs are the tags v7.0.1 (checkout), v8.0.2 (download-artifact) and v7.0.2 (upload-artifact) with `git ls-remote`.
- `persist-credentials: false` on both checkouts.
- No `pull_request_target`. No secrets are used. `GH_TOKEN` is the job's `github.token`, in the `release` job only.
- No `${{ github.event.* }}` appears in any `run:` block. `inputs.ref` and `inputs.sha` are used only in `with:`, `concurrency` and `if:`. Every other value reaches a script through `env:`.
- `release` is skipped for `pull_request`. It also requires `github.ref == refs/heads/develop` and `inputs.ref` of `''`, `'develop'` or `null` (macos.yml `release.if`).
- A fork PR gets a read-only token and no secrets. The `paths:` filter (macos.yml:26-32) limits PR runs to changes in the build itself. Fork approval policy is `first_time_contributors` (verified via `gh api .../fork-pr-contributor-approval`). Default workflow permissions are `read`.

### S1. Should-fix: a fork PR can run arbitrary code on 16 x 6 h of free macOS runners
- macos.yml:26-32. A PR that touches `scripts/ci/mac_*` runs the PR's own version of those scripts on the runners. Approval is only required for first-time contributors; anyone who already has a merged PR runs without it. Nothing leaks, because there are no secrets and the token is read-only. The cost is runner time and the five concurrent macOS jobs allowed on the free plan.
- Fix: add `if: github.event.pull_request.head.repo.full_name == github.repository` to `fetch`, so fork PRs do not start the chain. Or set the approval policy to "all outside collaborators". This is a personal repo, so the first option is the simple one.

## Correctness and robustness

### S2. Should-fix: the release job has never run, and the swap is not atomic
- macos.yml:321-322. In run 41420273033 `release` was skipped, because it is a PR run. So the `gh release delete nightly --cleanup-tag --yes || true` then `gh release create` path is untested.
- `|| true` hides every delete failure. If `create` fails after the delete, the previous nightly is gone and there is none until the next day.
- Fix: create the new release under a temporary tag first (`nightly-new`), then delete the old one and rename. Or accept the gap and drop `|| true` in favour of `gh release view nightly >/dev/null && gh release delete ...`. Also run the job once for real (a manual dispatch on develop after merge) and watch it before relying on it.

### S3. Should-fix: concurrency groups differ for the same ref, and PR groups collide
- macos.yml:39: `macos-${{ pull_request && head_ref || inputs.ref || github.ref }}`.
- A nightly has group `macos-refs/heads/develop`. A manual dispatch with `ref=develop` has group `macos-develop`. The two can run side by side for a day each, and both reach `release` and race on delete and create.
- `head_ref` is a bare branch name. A fork PR from a branch named `develop` shares `macos-develop` with a manual run, and with `cancel-in-progress: true` for PRs it cancels that day-long run. Two forks' PRs with the same branch name (for example `patch-1`) also cancel each other.
- Fix: use `github.event.pull_request.number` for PRs, for example `pr-<number>`. Normalise the dispatch ref with `github.ref_name`. Alternatively give the release job its own `concurrency: nightly-release` with no cancel.

### S4. Should-fix: the nightly rebuilds for 16 hours even when nothing changed
- macos.yml:18 (cron). No step compares the last nightly's Koan SHA and `CHROMIUM_VERSION` with today's. A quiet week burns 7 x 16 h of runner time and ~8 GB of artifacts per day, for a byte-identical result.
- Fix: a first `ubuntu-latest` job that reads the `nightly` release's `SHA` and the Chromium pin, and skips the chain if both are unchanged (keep `workflow_dispatch` forcing a build). The notes already say this runs for personal use only.
- Also note that GitHub disables scheduled workflows after 60 days without repo activity.

### S5. Should-fix: the 5 h of work in a slice is lost if the job fails after the deadline
- macos-build.yml (Build / Pack steps, `if: steps.build.outputs.status == 'running'`). `mac_build_slice.sh` exits non-zero on any build error, and then nothing is packed or uploaded. A re-run starts from the previous slice's `out`, so a flaky compile or a runner loss at hour 4 costs the whole slice. With the 65-minute reserve (macos-build.yml, Plan step) this is mostly an issue for real build failures, which should fail anyway.
- Re-runs work as designed: each job reads only fixed-name artifacts, and uploads use `overwrite: true`. The resume window is the 5-day retention. After that the whole run has to restart.
- Suggested low-cost change: also pack and upload `out` when the build fails with a non-zero exit but a `FAILED:` step, so the fix can resume. Optional.

### Other verified points (fine)
- Resume chain, status passing and the `check` job work as intended (run 41420273033: fetch 28 min, builds 4h56m, 4h56m, 4h56m, then 46 min, which finished).
- `CHROMIUM_VERSION` handling: `bootstrap.sh` reads line 1 as the version and line 2 as a 40-hex commit. `mac_package.sh:` uses line 1 and the summary step in macos.yml uses `paste -sd' '`. Nothing hard-codes a version. A move to the 158.0.8127.2 tag commit (PR #89) is a one-line edit of line 1 and a one-hash edit of line 2. Gap: the pruning script and the Xcode default (S/N below) are only proven on 158.0.8111.0, so the first build after the bump should be watched.
- The prune script: paths that do not exist are skipped, build files (`.gn .gni .grd .grdp .pydeps ...`) are kept, and none of Koan's `patches/` touch a removed path (grep over `patches/` for the removed prefixes found nothing). The green build with `chrome` as the target is the proof for 158.0.8111.0. I could not verify that the pruned set is also safe for 158.0.8127.2.

### N1. Nit: `mac_build_slice.sh` watcher cleanup is order dependent
- scripts/ci/mac_build_slice.sh:74-76. `pkill -P "$watcher"` kills the watcher's `sleep "$budget"`. The subshell then continues to `touch "$timer"` (line 63) unless `kill "$watcher"` lands first. If `touch` wins, a finished or failed build is reported as `status=running`. In 30 local trials with a stand-in script I never saw this, so it is theoretical, but the order can be made safe: `kill "$watcher" "$monitor"` first, `pkill -P` after.

### N2. Nit: the resume window and `out` size
- `koan-mac-out-N` is only needed until slice N+1 has downloaded it, but is kept for retention. See Cost.

## Cost and limits

Measured from run 41420273033 (`gh api .../artifacts`):

| Artifact | Size |
|---|---|
| `koan-mac-source.tar.zst` | 5.80 GB (single artifact) |
| `koan-mac-out-1/2/3.tar.zst` | 0.56 / 0.73 / 1.05 GB |
| `Koan-158.0.8111.0-d5c85c0-arm64.dmg` | 0.19 GB |
| Logs (all slices) | ~1.8 MB |
| Total per run | ~8.3 GB |

- The repo is public, `xcode-27` is documented as free for public repos (.agent/notes/macos-ci.md:117), and there are no self-hosted runners. I did not confirm from GitHub's docs how artifact storage is counted for public repos, so treat the figure below as a worst case if it ever were counted. Each run is ~8.3 GB for up to 5 days, and each PR push that is not cancelled early adds another.
- No per-artifact size limit is near: the largest, at 5.8 GB, uploaded and downloaded fine (the download took 8 min in build-01).
- Recommended retention (macos.yml:95, macos-build.yml:112, :127, :155):
  - source: 3 days (was 5). It is only needed for the run and a re-run the next day or two.
  - out slices: 2 days (was 5).
  - logs: 7 days (was 14).
  - .dmg: 7 days (was 30). The `nightly` release already keeps the latest build, and a dev wants only recent run artifacts.
  This cuts the steady-state footprint of ~8.3 GB per run from 5 days to 2-3 days.
- Time: 16 h 05 min for the green run. The chain of 16 jobs is ample for 8111.0; the `check` job fails clearly if it is not enough.
- Preview label: `xcode-27` is described as a public-preview label (macos-ci.md:117). If GitHub renames it the jobs queue forever. Note it in the runbook.

## Scripts

### N3. Nit: `mac_runner_setup.sh` only checks `GITHUB_ACTIONS`
- scripts/ci/mac_runner_setup.sh:19. It deletes Xcodes, Chrome, the Android SDK and `$HOME/.cargo`, `.rustup` with `sudo rm -rf`. A self-hosted Mac runner (for example the maintainer's, if ever registered) has `GITHUB_ACTIONS=true` too. Add `|| ${RUNNER_ENVIRONMENT:-} != github-hosted` to the guard.

### N4. Nit: the default Xcode path is written twice
- scripts/ci/mac_env.sh:21 and scripts/ci/mac_runner_setup.sh:24 both hard-code `/Applications/Xcode_27.1.app`. A Chromium bump that needs another SDK must change both. Have the setup script source `mac_env.sh` or export it.

### N5. Nit: `codesign --deep`
- scripts/ci/mac_package.sh:36. Apple deprecates `--deep` for signing; nested helper apps should be signed inside-out. It passes `--verify --deep --strict` in the green run, so this only matters if a future macOS enforces it.

### Shared scripts: no breakage found for Linux or agent-build
- scripts/bootstrap.sh:69-76: the retry loop wraps `gclient sync` with the same arguments as before, only on the sync path. On Linux a real failure (for example a bad revision) now takes two extra minutes (2 x 60 s sleeps) before it exits 1. No change to the already-at-revision skip.
- scripts/build.sh: `--gen` is handled after the existing `gn gen` and before `autoninja`, so default behaviour (`chrome`) is unchanged. The Metal block is inside `if [[ $OS == mac ]]`, so Linux never reaches it. It does change the Mac build: it now fails with "no Metal toolchain" instead of failing later in ANGLE, and it appends `angle_metal_toolchain_dir` to `$OUT/args.gn`. The `cp "$ARGS" "$OUT/args.gn"` at line 52 resets the file on every run, so the append does not accumulate.
- I found no reference to `scripts/ci/mac_*` or `--gen` outside the new files; `scripts/agent-build.sh` is not in this worktree (it is not on this branch), so I could not diff it directly. `update_chromium.sh` writes `CHROMIUM_VERSION` as `version` then commit, consistent with the readers.

## Docs

### N6. Nit: docs/USAGE.md:526 and :560
- It does explain how to download (release page, run artifact, and a `gh api` recipe because `gh run download` cannot fetch an unzipped artifact) and how to open the unsigned app (right-click Open, System Settings -> Privacy & Security -> Open Anyway, or the quarantine flag). That meets the requirement.
- `xattr -d com.apple.quarantine /Applications/Koan.app` (line 560) only clears the flag on the bundle directory; `xattr -dr` is the usual form and is safer if any nested file carries it.
- "macOS 13 or later" (line 526) is not backed by anything in the repo (`build/koan-mac.gn` sets no deployment target and the notes do not mention it). Confirm against Chromium 158's minimum macOS before keeping it. I did not check the Chromium tree.

## Merge recommendation

Merge. There is no Blocker: permissions are least-privilege, actions are SHA-pinned, nothing user-controlled reaches a shell, and the build chain has a green end-to-end run. Before or right after merge, fix S1 (fork guard) and S3 (concurrency group). S2 and S4 can follow in a small PR, but watch the first real nightly on develop closely (release job untested, 16 h build). Shorten retention as listed.
