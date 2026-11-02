# Following Chromium releases (#86)

Koan was pinned to `158.0.8111.0`, a trunk snapshot (main @ 67e29b8,
2026-10-18) that gets no security fixes. Policy, and the routine for point
releases and new milestones: [docs/REBASE.md](../../docs/REBASE.md#following-chromium-releases).
This note is the record of the M158 move and the plan for switching the
shared Linux checkout.

## The move to M158 (2026-11-02)

Mac channels that day (chromiumdash): Stable 156.0.8078.80, **Beta
158.0.8127.2**, Dev 159.0.8130.0. Our pin was just before M158's branch
point, so the smallest move is onto the M158 release branch
(`refs/branch-heads/8127`), pinned to the tag **158.0.8127.2**
(`681cd621e82e606d7536eae8ea2576afb3c7d65c`). Back to 156 would mean
back-porting patches that rely on newer upstream code, such as the vertical
tab strip.

`8111.0..8127.2` is 5998 upstream commits (2026-10-18 to 2026-10-27), and
the compiler rolled: `CLANG_REVISION` `llvmorg-24-init-7747-g62397f8b` ->
`llvmorg-24-init-14453-ge063be79`, `RUST_REVISION` `1edd55dc...` ->
`8a9dd7ab...`. See the rebuild cost below.

### Patch check against 158.0.8127.2

`scripts/ci/check_patches.sh 158.0.8127.2` on the old patches: 66 of 76 ok,
**9 conflicts and 1 file gone**. After the ports in this PR, all 76 are ok
(also applied with `git apply` to a real `158.0.8127.2` checkout; see
"Rehearsal").

| Patch | What upstream did | Port |
|---|---|---|
| `chrome/browser/ui/views/tabs/common/tab_group_view_layout.cc` | **renamed**: the tab layouts were split into `tab_group_view_{horizontal,vertical}_layout.cc`; the layout is a `LayoutManager` of `TabGroupView` (`TabGroupView()` is the host accessor, not `tab_group_view->`) | now `tab_group_view_vertical_layout.cc.patch`; same hook |
| `.../tabs/common/pinned_tab_container_view.cc` | **moved** into `pinned_tab_container_view_vertical_layout.cc` (`children_on_row` is there) | now `pinned_tab_container_view_vertical_layout.cc.patch`; same hook; the include moved with it |
| `.../tabs/vertical/vertical_tab_strip_top_container.cc` | `CalculateProposedLayout` rewritten (controls are always in the layout, `show_segmented_control`, `keep_horizontal_layout`); `GetPreferredWidth(bool)` | the 3 `koan::` calls in the same places (collapsed branch, end of the horizontal branch, `GetPreferredWidth`) |
| `chrome/browser/ui/tabs/tab_strip_model.cc` | `HasIndicesClosedByCommand` is gone (`HasTabsClosedByCommand` calls `GetTabsClosedByCommand`); the "Unfocused tab group" toast moved from `SetSelection` into `ExitFocusMode` (reason `kTabOutsideGroupActivated`) | dropped the first (the `GetTabsClosedByCommand` hook covers it); the toast condition is now in `ExitFocusMode` |
| `.../frame/vertical_tab_strip_region_view.cc` | one new line (`resize_area_->SetVisible`) after the resize bounds | context only |
| `.../tabs/common/tab_strip_collection_controller.cc` | include block | context only |
| `chrome/browser/ui/browser_live_tab_context.cc` | `GetExtraDataForTab` has a new `contextual_tasks` block | context only; Koan's merge is still last |
| `chrome/browser/ui/tabs/BUILD.gn`, `tab_menu_model.h`, `chrome/browser/about_flags.cc` | neighbouring lines | context only |

Compile-level changes `git apply` can't see (found by reading the diff
around each hook, and by comparing `WindowOpenDisposition`):

- **`WindowOpenDisposition` values were renamed** `NEW_FOREGROUND_TAB` ->
  `kNewForegroundTab` (all of them: `kCurrentTab`, `kNewPopup`,
  `kNewWindow`, `kNewBackgroundTab`, `kSwitchToTab`, `kNewSplitView`, ...).
  The Glance patch (`browser_web_contents_delegate.cc`) is fixed here.
  `src/` still has the old names (below).

### Open PRs: which of their patches conflict

Each is a hook patch the PR adds or changes, checked against 158.0.8127.2
(`gh pr diff N --name-only | grep ^patches/`, the PR branch's version of the
file against upstream at the tag). The PRs rebase onto develop after this
lands, so they only need the same treatment for what they add.

| PR | Patches it adds or changes | Against 158.0.8127.2 |
|---|---|---|
| #60 folders | `tabs/common/tab_group_view_layout.cc`, `tabs/common/unpinned_tab_container_view_layout.cc` | **both files are gone.** Port to `tab_group_view_vertical_layout.cc` and `unpinned_tab_container_view_vertical_layout.cc`: in the group layout `tab_group_view->` is `TabGroupView().` (so `koan::FolderRows folder_rows(TabGroupView())`); the hook's logic is otherwise unchanged. Build on develop's renamed `tab_group_view_vertical_layout.cc.patch` (Spaces' `koan_space`) rather than the pre-Spaces text |
| #62 theme picker | `chrome/browser/BUILD.gn`, `frame/BUILD.gn`, `frame/themed_background.cc` ok; `chrome_content_browser_client.cc` **conflicts** in the `GetPreferredColorScheme` hunk | `GetPreferredColorScheme` now takes `const Profile& profile` and no longer defines `Profile* profile`; the hunk's `koan::PrefersDarkWebColorScheme(web_contents, ...)` needs only `web_contents`, so re-place it |
| #64 Library | no patches; merged into develop before the switch: its `src/` (`koan_library_downloads.cc`, `koan_library_history.cc`) used the old disposition names, ported in #89 |
| #66 UI pieces | merged into develop before the switch: its 2 patches (`download/BUILD.gn`, `download_toolbar_ui_controller.cc`) are ok against 158.0.8127.2 (78 patches ok in all); no old disposition names in its `src/` |
| #71 Mac shortcuts/menu | `chrome/browser/BUILD.gn`, `app_controller_mac.mm`, `ui/cocoa/BUILD.gn`, `ui/cocoa/main_menu_builder.mm` (branch `feat/mac-shortcuts-menu`, no PR yet) | all ok |
| #72 Mac traffic lights | `frame/BUILD.gn`, `browser_native_widget_mac.mm`, `frame/layout/browser_view_tabbed_layout_impl.cc` (branch `feat/mac-traffic-lights-swipe`, no PR yet) | all ok |

Other branches with patch changes and no PR yet: `feat/zen-compact-layouts`
changes `vertical_tab_strip_region_view.cc` and
`vertical_tab_strip_top_container.cc` (conflict: port as in this PR's
versions), `feat/zen-glance-split` touches `tab_strip_model.cc` and
`ui/tabs/BUILD.gn` (conflict, same).

### `src/` and the compile

`src/` is not compile-checked (see Rehearsal): that needs the new toolchain and
most of the generated code. Known: the `WindowOpenDisposition` renames in `koan_url_bar_controller.cc`
and `koan_glance_controller.cc` (this PR changes them). The rest shows up in
the first build after the switch (step 4), where the build is kept going
(`autoninja -k 0`) so one pass lists all the errors.

## Switching the shared checkout (step 4): NOT DONE; waits for "go"

Eight agents build from `~/work/koan/chromium/src` under
`agent-build.sh`'s lock. The switch rebuilds everything, so it is done once,
at a time the maintainer picks, and announced to the agent threads.

### Rebuild cost

- The compiler changed (`CLANG_REVISION` and `RUST_REVISION` above), so
  **every object is stale**: a full rebuild of the dev build, like the first
  one (~70.5k steps, ~15 h at 4 jobs; `agent-build.sh` uses 6 jobs when
  9 GB are free, so **roughly 10-15 hours**). No agent can build while it
  runs: whoever builds first holds the lock.
- `gclient runhooks` downloads the new clang, Rust and the other hook
  outputs first (the rehearsal below measured it).
- Disk: the old `out/Koan` objects are replaced in place; ~780 GB is free.
- Then the full e2e set (23 scripts in `scripts/e2e/`), still under the lock.
- Point releases within a milestone usually rebuild far less (they change
  tens of files, not the compiler), unless the toolchain rolls.

### Plan: swap in the prepared checkout (recommended), 17:30 UTC

Re-syncing the shared checkout in place means fetching the tag and ~30 GB of
dependencies again, with the old tree destroyed meanwhile. Instead
`~/work/koan/.cache/bootstrap-test` is a complete checkout of the tag made by
`scripts/bootstrap.sh` (deps synced, hooks run; 28 min, 30 GB; its
`gn gen` and the 76 patches checked). Under the build lock it is renamed
into place and the old one is kept, so rollback is two renames:

```sh
~/work/koan/.cache/agent-switch-chromium.sh <this worktree> --preflight  # no lock, any time
# at "go": preflight, then the switch detached (setsid + nohup; the harness
# kills background commands after 2 h, and this runs 10-15 h):
~/work/koan/.cache/agent-switch-chromium-detached.sh <this worktree> ~/work/koan/.cache/switch-chromium.log
until grep -q '^AGENT-BUILD-EXIT' ~/work/koan/.cache/switch-chromium.log; do sleep 60; done   # poll; ends "AGENT-BUILD-EXIT <code>"
```

The log has the steps; the compile output is in
`.cache/agent-switch-chromium.log`. Builds after it use
`agent-build-detached.sh`.

The script does, in order (one filesystem: the moves are instant):

```sh
# before the lock (--preflight): new tree at the pin, pristine, not in use,
# out/ and the //koan link removed, depot_tools works with it
# under the lock:
cd ~/work/koan
mv chromium chromium-8111                      # keeps out/Koan, out/Release, branding, patches
mv .branding-pristine chromium-8111/branding-pristine   # the backups belong to that tree
mv depot_tools depot_tools-8111
mv .cache/bootstrap-test/chromium chromium
mv .cache/bootstrap-test/depot_tools depot_tools
scripts/apply_branding.sh                      # new tree; strings/icons
# this worktree's patches (git apply), ln -sfn <worktree>/src chromium/src/koan
# gn gen out/Koan (build/koan.gn); autoninja -C out/Koan -j 2-6 -k 0 chrome   # 10-15 h
# every scripts/e2e/*.mjs (23), 10 min each at most
```

Decisions behind it:

- **`.gclient`: use the scratch one** (what `bootstrap.sh` writes,
  `"managed": False`), not the shared tree's managed one: a managed `src`
  with no `--revision` is expected to move to the tip of main on the next
  `gclient sync`.
- **`depot_tools`: swap too.** The shared one is from 2026-10-17, the
  scratch one (cloned by `bootstrap.sh`) from 2026-11-01, which the hooks
  of this revision ran with.
- **`out/`: not carried.** `gen/` files hold absolute paths of the scratch
  location (e.g. `gen/third_party/devtools-frontend/.../com.chrome.devtools.json`),
  and the compiler changed anyway. Starting clean costs the ~7k steps the
  scratch compile had done (about an hour) and removes any doubt. The old
  `out/Koan` stays in `chromium-8111/`, so the old tree builds again after
  a rollback.
- **`-k 0`** (keep going) for this first build, so one pass lists all
  compile errors in `src/` and the patches. The script exits non-zero with
  the first 150 lines of errors; fix them on the PR branch and carry on with
  `agent-build.sh` as usual (it builds incrementally from where this stopped).
- **Rollback:** `~/work/koan/.cache/rollback-chromium.sh` (written before the
  first move, so an interrupted swap has it; hold the lock, nothing
  building). It puts `chromium-8111/` and `depot_tools-8111/` back, moves
  the 8127 `.branding-pristine` aside (`chromium-new/branding-pristine-8127`;
  a plain move would nest the old backups inside it and the next
  `apply_branding.sh --revert` would write 8127 files into the 8111 tree),
  and restores `last-built-src`, `last-built-dirty` and `applied-patches`
  as they were (saved in `chromium-8111/cache-state/`), so the old
  `out/Koan` stays consistent with the incremental-build bookkeeping.
  **Rollback stops being possible when #89 is merged and the worktrees have
  rebased**: develop's patches then target 8127 and don't apply to 8111. The
  old trees are then dead weight: delete `chromium-8111/` and
  `depot_tools-8111/` (~100 GB) once the first M158 builds are good.
- **Links:** every worktree needs `chromium` and `depot_tools` links that
  resolve (`e2e/run.sh` runs `<worktree>/chromium/src/out/Koan/chrome`).
  `--preflight` lists the ones that are missing or dangling (the PR's own
  worktree had none); `--go` repairs them wherever git ignores them, before
  the build and again before the e2e stage, and stops if one still doesn't
  resolve.
- **Guards:** `--go` is required (no accidental lock for 15 hours); the
  preflight is repeated right after the lock is taken (the wait may be
  hours); it also checks that clang and rustc from the hooks exist.
- The in-place alternative (kept for a point release, which changes few
  files): `scripts/update_chromium.sh <tag>` fetches the tag by name and
  syncs; `--with_branch_heads` is not needed (it would fetch all 8,329
  `refs/branch-heads/*`).
- After the switch the maintainer merges #89 and every agent worktree
  rebases on develop (new patch names, `CHROMIUM_VERSION`, `src/` renames);
  until then their `agent-build.sh` stops at "patch does not apply". #89
  must not merge before the switch.

## Rehearsal (scratch checkout, not the shared one)

Done in `~/work/koan/.cache/bootstrap-test` (a second checkout, never the
shared one), on 2026-11-02:

- `scripts/bootstrap.sh` at the pin: 28 min wall under load, ~30 GB; `src`
  ended at 681cd621 (gclient fetches a release-branch commit by hash).
- `scripts/apply_patches.sh check`: 76 applicable; `apply`: all applied.
- `gn gen out/Koan` with `build/koan.gn`: 37,238 targets, no warnings (so
  the patched `BUILD.gn` files and `//koan` deps resolve).
- A compile of the 88 objects of `src/` and the patched files (`autoninja
  -j 2 -k 0` on their object paths, from `compile_commands.json`): it needs
  ~24.6k generated-code steps first. It got 7.1k done in 47 min and then
  stopped on an infrastructure error (`spawn helper: EOF` in
  `web_idl_database`, when the step was interrupted), with no compile error
  seen. **So there is no compile verdict yet**; tonight's `-k 0` build is it.
- Not run: `update_chromium.sh` end to end (it would reverse the patches
  under that compile), the e2e suite, anything on the Mac.

## The routine

In docs/REBASE.md: "Following Chromium releases". In short:

- A point release of our milestone (Beta or Stable): bump the pin within
  days.
- A newer milestone at Stable: rebase within about 2 weeks.
- The workflow `.github/workflows/chromium-releases.yml` opens and keeps up
  to date one `chromium-update` issue with the version, the due date and the
  patch check; it closes it when the pin has caught up.
- The Mac CI (#69; PR #70 is still open, so `macos.yml` isn't on develop
  yet) will build whatever `CHROMIUM_VERSION`
  pins through `scripts/bootstrap.sh`, so it follows the pin by itself.
