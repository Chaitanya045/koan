# Review of PR #89 (Closes #86): Chromium base to M158 release tag 158.0.8127.2, plus release tooling

Reviewed head `e6268ba` (branch `chromium/m158-release-branch`), read only. The switch script was read as it was at 2026-11-02 15:31 (the swap-in-a-prepared-checkout version), not the earlier in-place gclient version. Upstream files were fetched from googlesource at 67e29b82 (old) and 681cd621 (new).

Totals: **Blocker 0, Should-fix 6, Nit 9.**

Things I checked and found fine are in "Verified OK" at the end, so they are not re-checked.

---

## Blockers

None. Nothing here stops the PR from merging or makes running the swap dangerous. The Should-fix items about the switch script (S4, S5) are worth fixing before the run, because they only hurt on the rollback path and in the final e2e stage.

---

## Should-fix

### S1. `update_chromium.sh` cannot be run in the flow it documents, for any patch that changed

`scripts/update_chromium.sh:68-79`, `docs/REBASE.md:87-97,109-116`

The script first checks the **new** `patches/` against the target tag (68-72). It then un-patches the checkout with `apply_patches.sh reverse` (75). `reverse` uses the `patches/` of the **current branch**, which are the new, ported patches. The checkout still has the **old** patches applied.

`apply_patches.sh:is_applied` is `git apply --reverse --check`, which is all-or-nothing per file. It fails for every patch whose text changed in this PR: the 9 ported ones, or any patch with different context. Those files stay modified, so line 76 aborts with "chromium/src still has local changes after reversing Koan's".

The docs say "port the hook on a branch first, then run `update_chromium.sh`". That is exactly the case that breaks. A point release with unchanged patches works, so the bug only shows on a milestone rebase. The PR itself says the whole flow was not run ("update_chromium.sh itself ... was not run").

Fix: do what the switch script does. Check that the modified files are only a subset of the previous patches plus branding, then `git checkout -- .`. Or reverse with the patches of the commit that was applied (`.cache/applied-patches`-style), not the new ones.

### S2. The watcher records a failed or skipped patch check as final and never retries it

`scripts/ci/chromium_releases.py:213-221, 270-274`

`state_of` (184-190) holds only pin, point version, milestone version and the `patches/` tree hash. It does not hold the check results. Line 272 returns early when the old marker equals the new state. Run 1 may therefore produce one of these results:
- `not checked: tag not found (yet?)`. `run_check` returns `None` for any exit code 2 (219-220), and `check_patches.sh` exits 2 whenever `koan_resolve_commit` fails, not only when the tag is missing. A gitiles 429/5xx that outlasts curl's retries gives the same result. I saw a 429 from googlesource while testing, and the resolve step then printed `curl: (22) ... 429`.
- `FETCH FAILED` rows.
- `not checked: no result`.

All three are written into the issue together with the marker. From then on every run prints "Same as the last run" until the pin, the versions or `patches/` change. The issue then shows a stale "not checked" for days, exactly when a human is deciding whether the update is safe. chromiumdash can list a version before the tag is on googlesource, which the code anticipates, but it never retries.

Fix: skip the early return (or do not store the marker's check outcome as final) while any check is `None`, has `FETCH FAILED`, or has `total == 0`. Better, make `check_patches.sh` use distinct exit codes for "no such tag" (404) and "network error".

### S3. An empty chromiumdash answer closes the tracking issue

`scripts/ci/chromium_releases.py:203-210, 263-268`

If both Beta and Stable come back as `[]` (a chromiumdash hiccup returning an empty list), `pick_targets` returns `{}`. The `if not targets:` branch then closes the open issue with "Koan is on X, the newest of its milestone ...". A later good run opens a new issue instead of reopening, so there is a duplicate and extra notifications.

There is no sanity check that the data contains the pin's own milestone. When chromiumdash is down or answers with an error, `urlopen` raises (HTTPError/URLError/timeout/JSON error). The job then fails red, changes nothing and tries again in 6 hours. That is acceptable, but there is no retry or message.

Fix: treat "no release of the pin's milestone in the list" as an error (exit non-zero without touching the issue). Add 2-3 retries with a short sleep around `urlopen`.

### S4. The switch script's final e2e stage cannot pass: the PR worktree has no `chromium` link

`~/work/koan/.cache/agent-switch-chromium.sh:127-133`, `scripts/e2e/run.sh:42`, `scripts/koan_test_env.sh:15`, `~/work/koan/.cache/agent-build.sh:31-33`

`run.sh` runs `$KOAN_ROOT/chromium/src/out/Koan/chrome`, with `KOAN_ROOT` being the worktree. `agent-build.sh` first creates `<wt>/chromium` and `<wt>/depot_tools` symlinks. The switch script does not.

`ls` in the PR worktree shows no `chromium` or `depot_tools`. After a 10-15 hour build, all 23 e2e tests will fail with a missing binary, and the script exits 1. The build result is still good, so nothing is lost, but the stage is wasted and its failure message is misleading.

Fix: copy the `for link in chromium depot_tools` loop from `agent-build.sh` into the switch script, before the e2e stage.

### S5. The rollback script restores the wrong branding backup and does not restore the incremental-build bookkeeping

`~/work/koan/.cache/agent-switch-chromium.sh:79, 84-95, 103, 109-110`

1. **Branding backup.** The swap moves `.branding-pristine` into `chromium-8111/branding-pristine` (79). Then `apply_branding.sh` (103) creates a **new** `$MAIN/.branding-pristine` that holds the 8127 versions of the branded upstream files. The rollback does `mv chromium/branding-pristine .branding-pristine` (92). That target now exists as a directory, so `mv` puts the old backup *inside* it as `.branding-pristine/branding-pristine`.

   The result: `.branding-pristine/` contains the 8127 copies at the paths `branding.py` reads, and the real 8111 backups are nested one level down. The restored 8111 tree is still branded, but the next `apply_branding.sh --revert` (or `update_chromium.sh`, which un-brands first) writes the 8127 `chromium_strings.grd`, `BRANDING`, logos and other files into the 8111 tree. `branding.py:save_pristine` also never overwrites an existing backup. Silent corruption of upstream files.

   Fix: in the rollback, first `rm -rf .branding-pristine` (or move it to `chromium-new/`), then move the old one back.

2. **Build bookkeeping.** The switch deletes `.cache/last-built-src` and `.cache/last-built-dirty` and replaces `last-built-src` with a copy of the PR's `src/` (109-110). The old `out/Koan` in `chromium-8111` was built from another worktree's `src/` (`applied-patches` points at feat-zen-folders). After a rollback, `agent-build.sh`'s mtime logic compares a worktree against the PR snapshot. A file that equals the PR's version but differs from what the old objects were compiled from is not touched, so it can be left stale.

   Fix: save the two files into `chromium-8111/` before overwriting, and put them back in the rollback. Or have the rollback force a full touch.

3. **Rollback is only valid before #89 merges and the worktrees rebase.** Develop's patches do not apply to 8111. Say so in the notes. The PR text says "two renames" and nothing more.

4. **Written after the swap.** The rollback script is created after the four `mv`s (84-95). Write it before the first `mv`, so an interrupted swap still has one.

### S6. Docs that will be wrong or point at something that does not exist yet

- `scripts/update_chromium.sh:88-89` says the shared Linux checkout's `.gclient` is not `"managed": False`. After the swap it is (the prepared checkout's `.gclient` is unmanaged), so the comment is wrong from then on.
- `docs/REBASE.md:81` and `.agent/notes/chromium-releases.md` (last section) say "the nightly CI (`macos.yml`) follows the pin". `.github/workflows/` on this branch and on develop has no `macos.yml`; it is PR #70 (OPEN, branch `ci/macos-nightly`). Reword until #70 merges.

---

## Nits

- **N1** `scripts/ci/check_patches.sh:55-58`: `fetch` runs under `|| rc=$?`, so `set -e` is off inside it. If the cache write (`mkdir`/`cp`) fails, the function returns that command's status (1), and the caller prints `MISSING` for a file that exists. Return 0 explicitly after the cache write, and write the cache file with a temp name + `mv` so a killed CI run cannot leave a truncated cached file.
- **N2** `scripts/koan_chromium.sh:14`, `scripts/update_chromium.sh:49-57`: "release tags only" is only a format check. A Dev-channel tag such as 158.0.8117.2 (confirmed to exist, listed as a Dev release) is accepted. It sits on a release branch, so it is not a trunk snapshot, but it is not Beta/Stable. Optionally require that the version appears in chromiumdash Beta/Stable for Mac.
- **N3** `CHROMIUM_VERSION`: nothing checks that line 1 resolves to line 2 (`refs/tags/<line1>` -> commit). The watcher reads line 1, bootstrap and CI read line 2. A one-line CI check (`koan_resolve_commit "$(koan_pin_version)"` equals `koan_pin_commit`) would catch a typo. Today they match (681cd621).
- **N4** `scripts/update_chromium.sh:84-91`: it checks `HEAD == $commit` after the checkout, but not after `gclient sync`/`runhooks`, which `bootstrap.sh:76-79` now does. It also writes `CHROMIUM_VERSION` (95) before the patches apply, so a failed apply leaves a moved pin with unapplied patches. That is described in the script's own message, but a rerun then hits S1.
- **N5** `.github/workflows/chromium-releases.yml:56-59`: the `# shellcheck disable=SC2086` is a YAML comment outside the `run:` block, so shellcheck/actionlint never see it. I could not run either tool here (neither is installed), so I cannot say whether actionlint flags the unquoted `$DRY_RUN`. A bash array or `[[ $DRY_RUN ]] && args+=(--dry-run)` avoids the question. Also, a manual dispatch from a feature branch uses that branch's `patches/` and `CHROMIUM_VERSION` and edits the real tracking issue; consider `if: github.ref == 'refs/heads/develop'` for non-dry runs.
- **N6** `.github/workflows/ci.yml`: still uses mutable tags (`actions/checkout@v4`, `setup-python@v5`, ...). The new workflow pins by SHA (`3d3c42e5...` is v7.0.1, the current release, checked with the GitHub API). Pre-existing; consider matching.
- **N7** `agent-switch-chromium.sh`: (a) the preflight is run once, then the script may wait hours in the queue (7 tickets are queued now); `HEAD == COMMIT` and "pristine" are not re-checked right before the `mv`s (line 100 checks after the swap). (b) `$USER` is used under `set -u` (line 34). (c) No explicit go-gate: the "NOT RUN until the maintainer says go" rule is only a comment; a `--go` flag or an env var would stop an accidental run that takes the lock for 10-15 hours. (d) The preflight only checks `gclient --version` and `gn --version`, not that the toolchains from the hooks exist. They do exist now (clang `llvmorg-24-init-14453-ge063be79`, rustc present, all DEPS commits match chromiumdash), but a cheap `test -x third_party/llvm-build/Release+Asserts/bin/clang` would keep that true.
- **N8** `.cache/bootstrap-test/chromium/src/buildtools/reclient_cfgs/reproxy.cfg:11,22` holds absolute `.cache/bootstrap-test/...` paths (credentials helper, depsscanner). Koan builds with siso and no remote execution, so it does not matter, but it is the only absolute-path leftover I found outside `out/` (I grepped depot_tools, `build`, `buildtools`, the toolchain dirs and the `.gclient*` files).
- **N9** The old `chromium-8111/` and `depot_tools-8111/` are ~100+ GB; the notes do not say when to delete them (after #89 is merged and the worktrees have rebased).

---

## 1. Patch ports: result

For each patch I compared the hook site in the old and new upstream file. All 76 patches are `ok` against 681cd621 (I ran `scripts/ci/check_patches.sh` on the pin from the worktree: 76 ok, 0 other).

| Patch | Same behaviour? | Notes |
|---|---|---|
| `tabs/common/tab_group_view_vertical_layout.cc` (renamed from `tab_group_view_layout.cc`) | Yes | The old `CalculateVerticalLayout` body is moved verbatim into `TabGroupViewVerticalLayout::CalculateProposedLayout`, `tab_group_view->` becomes `TabGroupView()`. The `koan_space` flag, the hidden header (`visible=!koan_space`, empty rect) and the skipped height increment are the same. Old `CalculateVerticalMinimumSize` called the vertical layout; new `GetMinimumSize` calls `CalculateProposedLayout(0,0)`, so the min size still includes Koan's change. The horizontal layout is a separate class (`tab_group_view_horizontal_layout.cc`); the old hook was only in the vertical branch, so no hook is lost. |
| `tabs/common/pinned_tab_container_view_vertical_layout.cc` (from `pinned_tab_container_view.cc`) | Yes | `children_on_row = std::min(...)` block is the same; the `ClampEssentialsColumns` call is after the min, as before. Include moved with it. |
| `tabs/vertical/vertical_tab_strip_top_container.cc` | Yes | Hook 1: inside the collapsed branch, after the collapse button's `SetToMax` (was `if (collapse_button_ && GetVisible())`, now `if (collapse_button_)` with `visible=true`, which is the new upstream rule). Hook 2: at the end of the horizontal branch, with `right_alignment` and `y_baseline`; `LayoutSidebarHeaderTrailingViews` only counts `child.visible` entries, so the new always-present invisible entries with empty rects are ignored. Hook 3: `GetPreferredWidth(bool)`; it is still used by `WillWrapDueToOverflow` and `wrap_during_animation`, so Koan's width is still part of the wrap decision. One difference to know about (not a regression): with `keep_horizontal_layout` (organizer-panel feature, expand-on-hover) upstream can now hide both trailing controls and `right_alignment` is `layout_width`; Koan's buttons would then sit at the uncollapsed width. I did not find Koan enabling the organizer panel, so it does not apply today. |
| `ui/tabs/tab_strip_model.cc` | Yes | (a) The dropped `HasIndicesClosedByCommand` hunk: upstream now has `HasTabsClosedByCommand(...) = !GetTabsClosedByCommand(...).empty()`, so the one remaining `koan::IsPinnedForUser(tab)` hook in `GetTabsClosedByCommand` covers both the enabled check and the execution. Same result. (b) The toast: upstream's `SetSelection` now calls `ExitFocusMode(kTabOutsideGroupActivated)`, which shows the toast; that is the only caller with that reason. `koan::ShowsFocusExitedToast(profile())` is `!IsWorkspacesEnabled() || OTR`, independent of timing, so evaluating it before `OnChange` (it used to run after) makes no difference. The other hunks (`EnterFocusMode`, `RotateFocusedGroup`, menu enabled/execute, `DetermineNewSelectedIndex`) have the same surrounding code; the new `tabs::TabHandle context_tab` line sits before the hook but the hook does not use it. |
| `ui/browser_live_tab_context.cc` | Yes | Koan's `merge` still comes after the new `contextual_tasks` merge; `std::map::merge` does not overwrite existing keys, so upstream keys win as before. |
| `ui/views/frame/vertical_tab_strip_region_view.cc` | Yes | New upstream line `resize_area_->SetVisible(organizer_panel_show_percent_ == 0.0)` is after Koan's bounds change; `OnResize` lost `SetStateOverride/ClearStateOverride`, which Koan's hunks do not touch. |
| `about_flags.cc`, `ui/tabs/BUILD.gn`, `tab_menu_model.h`, `tab_strip_collection_controller.cc` | Yes | Context only. The `about_flags.cc` hook is still just above "Add new entries above this line" at the end of `kFeatureEntries`. |

I found no hook that compiles but silently changes behaviour.

## 2. `WindowOpenDisposition` renames

Complete. `grep -rnE '\b(CURRENT_TAB|SINGLETON_TAB|NEW_FOREGROUND_TAB|NEW_BACKGROUND_TAB|NEW_POPUP|NEW_WINDOW|SAVE_TO_DISK|OFF_THE_RECORD|IGNORE_ACTION|SWITCH_TO_TAB|NEW_PICTURE_IN_PICTURE|NEW_SPLIT_VIEW|UNKNOWN)\b'` over `src/`, `patches/` and `scripts/` at the PR head finds nothing. Remaining hits are in docs/notes that talk about the old names on purpose (`docs/REBASE.md:93,210`, `.agent/notes/...`). All `WindowOpenDisposition::` uses in `src/` and `patches/` are now `kCurrentTab`, `kNewBackgroundTab`, `kNewForegroundTab`, `kNewPopup`, `kNewWindow` (5, 2, 1, 1, 1 uses). The PR's comment edits in `koan_glance.h` and `koan_url_bar.h` are cosmetic. (Open PRs #60, #62, #64 and the other branches still need the same work, as the PR lists.)

## 3. Tooling

- `check_patches.sh`: the version, commit, `refs/branch-heads/8127` and no-argument forms work (tested: pin run prints 76 ok; `refs/branch-heads/8127` resolves to 9169b9f2; `158.0.99999.9` gives `cannot resolve ...` and exit 2). Issues: S2 (exit 2 conflates "no tag" and "network"), N1.
- `update_chromium.sh`: S1, N2, N4. It does check the patches before touching the checkout, and it passes `--revision src@<commit>`.
- `bootstrap.sh`: the pin validation (both lines) and the post-sync `HEAD == REV` check are right and fail loudly. One note: `[[ -d src/.git ]]` with a dirty tree still points at `apply_patches.sh reverse`, which has the S1 problem only if the patches changed between pin and branch.
- `koan_chromium.sh`: no shellcheck issues that I can see by reading; works with bash 3.2 (`[[ =~ ]]`, no associative arrays). I could not run shellcheck or actionlint (not installed).

## 4. `.github/workflows/chromium-releases.yml`

- Least privilege: `permissions: {}` at the top; job gets `contents: read` + `issues: write` only; `persist-credentials: false`; the script gets `GH_TOKEN` only in the step's env. Good.
- Pinned: checkout by SHA (v7.0.1, verified). Python is stdlib, run with `-I`.
- No user-controlled input reaches the shell: `DRY_RUN` is a fixed flag. Issue text built from chromiumdash data only uses values validated by `VERSION_RE`; patch names come from the repo. No secrets are printed.
- chromiumdash parsing: `platform=Mac` is the right value (`Mac_arm64` is rejected with a validation error). Beta and Stable are fetched with `num=40`. "Our milestone" = same milestone and version > pin, Beta or Stable, Stable wins ties; "newer milestone" = Stable only. With today's data (Stable M156 156.0.8078.80, M157 157.0.8103.26; Beta M158 158.0.8127.2) it gives no targets, as the PR says. Correct.
- Idempotence: one open issue looked up by label, edited in place, a comment only when a new version appears, a quiet edit when only the patch result changed, `concurrency` group prevents overlap. Problems: S2 (stuck failed checks), S3 (false close). Also a closed issue is not reopened; a new one is created next time (acceptable).
- chromiumdash down: run fails red, no change (see S3 for the empty-list case, and the missing retry).
- Time budget: the check runs `check_patches.sh` with an empty cache for up to two targets (76 sequential fetches each, `--retry 6 --retry-delay 10`), inside a 15-minute job. Normal runs take a minute or two; a throttled googlesource can exceed 15 minutes and the job is cancelled with no output (state is not saved, so the next run retries; no harm).

## 5. The switch plan (read only; `agent-switch-chromium.sh`, md5 `b9db8378...`, 15:31)

**Is it safe to run?** Yes. The swap itself is reversible, takes the same queue and lock as `agent-build.sh`, and refuses to run if `chromium-8111/` already exists. I recommend fixing S4 and S5 first; neither makes the swap dangerous, but S5 makes the rollback unreliable and S4 wastes the e2e stage.

Why it is safe:
- **Lock:** a copy of `agent-build.sh`'s FIFO queue plus `flock` on `build.lock`; all steps that touch shared state (the four `mv`s, branding, patches, link, build) are after the lock (line 71-73).
- **Shared checkout is not modified in place.** The old tree is only renamed (`chromium` -> `chromium-8111`, `depot_tools` -> `depot_tools-8111`). Same device (`stat` dev 41 for all of them), so the `mv`s are instant renames. Nothing is deleted from the old tree.
- **Untracked files:** the preflight checks that the prepared tree has no modified tracked files other than the PR's patches (and puts them back), removes `out/` and the `koan` link, and requires a clean `git status --untracked-files=no`. `git ls-files --others --exclude-standard` in the prepared tree is empty. The prepared tree's `.git/info/exclude` already has `/koan`, and `.git/config` is identical to the shared one.
- **gclient DEPS:** the prepared checkout was made by `bootstrap.sh` at the pinned commit. All eight DEPS I compared (skia, v8, angle, webrtc, pdfium, dawn, devtools, and src itself) match the hashes chromiumdash lists for 158.0.8127.2, clang is `llvmorg-24-init-14453-ge063be79` as `update.py` says, and rustc is present. The prepared `.gclient` is unmanaged with `target_os = ["linux"]`, which avoids the "managed src moves to main" risk of the in-place plan. I did not find absolute paths of the scratch location outside `out/` except the unused `reproxy.cfg` (N8).
- **Branding:** the old pristine backups are moved aside (line 79), so `apply_branding.sh` makes fresh backups from the new tree. I also checked that all 31 branded files exist in the new tree and that the rebrand regexes still change `chromium_strings.grd`, `settings_chromium_strings.grdp`, `components_chromium_strings.grd` and `icons.html.ts` there. Forward path is correct. The rollback path is not (S5.1).
- **Patches:** `git apply` of the PR's patches, with `STATE` set even on failure, like `agent-build.sh`; `patched-files` is covered because `applied-patches` points at the PR's patches and `agent-build.sh` also resets `prev`.

What is not covered: S4 (e2e), S5 (rollback), N7. Also, after the swap every other worktree fails at "patch does not apply" until it rebases (documented), and 7 agent builds are already queued; they will fail fast against the new tree.

**Irreversible?** No, as long as the rollback is used before #89 merges and the worktrees rebase. The real one-way steps are: the prepared checkout's `out/` is discarded (deliberate, absolute paths in `gen/`), the incremental-build bookkeeping is overwritten (S5.2), and 10-15 hours of the shared lock.

**Compile status:** no compile verdict exists yet for `src/` and the patched files against 8127.2. The PR notes now say so honestly (the scratch compile stopped on `spawn helper: EOF` in `web_idl_database` after 7.1k steps, "no compile error seen"). The `-k 0` build in the switch script is the first real test. That is a known risk, not a defect.

## 6. Docs

The routine is clear and consistent across `docs/REBASE.md:21-98`, the watcher text, `SECURITY.md`, `CONTRIBUTING.md`, `README.md` and `.agent/notes/decisions.md`: point releases (Beta or Stable of our milestone) within days (watcher due date 3 days), a new milestone within about 2 weeks of reaching Stable (14 days), nothing for a newer milestone that is only Beta/Dev, stay on our milestone's latest Stable point release until then. The watcher's due dates match the policy.

Gaps: S1 (the documented new-milestone step 3 fails for ported patches), S6 (the `.gclient` comment, the `macos.yml` reference), nothing in the docs says what to do when a due date passes (the issue only shows the date), and the hook-audit table in `docs/REBASE.md` still correctly lists the moved files. The "Following Chromium releases" section and the audit rows for `tab_group_view_vertical_layout.cc`, `ExitFocusMode` and `GetTabsClosedByCommand` were updated.

---

## Verified OK (no action)

- 76/76 patches apply to 158.0.8127.2 (ran `check_patches.sh`); `refs/branch-heads/8127` resolves; a bad version exits 2.
- CHROMIUM_VERSION line 2 `681cd621...` is the commit of tag 158.0.8127.2 and the Beta commit chromiumdash lists.
- No old ALL_CAPS `WindowOpenDisposition` names left in `src/`, `patches/`, `scripts/`.
- Branding files and regexes still match the new tree; prepared checkout's DEPS and toolchains match the pin.
- `actions/checkout` SHA pin matches v7.0.1; permissions are minimal.
- CI jobs on the PR are green (patches, unit tests, lint).
