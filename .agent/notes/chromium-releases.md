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
| #64 Library | no patches | `src/`: `WindowOpenDisposition::NEW_FOREGROUND_TAB` / `NEW_BACKGROUND_TAB` -> `kNewForegroundTab` / `kNewBackgroundTab` |
| #66 UI pieces | `download/BUILD.gn`, `download/bubble/download_toolbar_ui_controller.cc` | both ok |
| #71 Mac shortcuts/menu | `chrome/browser/BUILD.gn`, `app_controller_mac.mm`, `ui/cocoa/BUILD.gn`, `ui/cocoa/main_menu_builder.mm` (branch `feat/mac-shortcuts-menu`, no PR yet) | all ok |
| #72 Mac traffic lights | `frame/BUILD.gn`, `browser_native_widget_mac.mm`, `frame/layout/browser_view_tabbed_layout_impl.cc` (branch `feat/mac-traffic-lights-swipe`, no PR yet) | all ok |

Other branches with patch changes and no PR yet: `feat/zen-compact-layouts`
changes `vertical_tab_strip_region_view.cc` and
`vertical_tab_strip_top_container.cc` (conflict: port as in this PR's
versions), `feat/zen-glance-split` touches `tab_strip_model.cc` and
`ui/tabs/BUILD.gn` (conflict, same).

### `src/` and the compile

`src/` is not checked here: a compile needs the new toolchain and the whole
tree. Known: the `WindowOpenDisposition` renames in `koan_url_bar_controller.cc`
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

### Commands (under the lock)

`agent-build.sh` can't do it: it applies the worktree's patches to the
checkout's current HEAD first. So the switch uses a small script that takes
the same queue and lock (`~/work/koan/.cache/agent-switch-chromium.sh`, not
run yet), which does, in `~/work/koan/chromium/src`:

```sh
# 0. Look first: only Koan's patches and branding may be modified.
git status --porcelain            # everything ' M', no untracked source
# 1. Back to pristine upstream.
~/work/koan/scripts/apply_branding.sh --revert
git checkout -q -- .              # the last build's patches (applied-patches)
# 2. The tag, shallow (the checkout is shallow and only has origin/main).
git fetch --depth=1 origin refs/tags/158.0.8127.2:refs/tags/158.0.8127.2
git checkout --detach 158.0.8127.2
test "$(git rev-parse HEAD)" = 681cd621e82e606d7536eae8ea2576afb3c7d65c
# 3. Dependencies of this revision, and the hooks (new clang, rust, ...).
cd ~/work/koan/chromium
gclient sync -D --nohooks --revision src@158.0.8127.2
gclient runhooks
# 4. Koan: the merged develop's patches, branding, //koan link, then build.
cd ~/work/koan && git pull   # develop with this PR
AGENT_CMD=... agent-build.sh   # build, then the full e2e set
```

Notes on those lines:

- The shared `.gclient` is **managed** (the default), unlike the one
  `bootstrap.sh` writes (`"managed": False`). `gclient sync` with a managed
  `src` and no `--revision` moves `src` to the tip of main: always give
  `--revision src@<tag>` here. The checkout is already at the tag, so it
  fetches nothing for `src`.
- `--with_branch_heads` is not needed for a tag: the tag is fetched by name.
  It makes gclient fetch every `refs/branch-heads/*` (8,329 refs on
  googlesource today), which in a shallow clone is a lot of objects for
  nothing.
- After the switch, every agent worktree has to rebase on develop (the new
  patch names, `CHROMIUM_VERSION`, and `src/` renames); until then their
  `agent-build.sh` stops at "patch does not apply".
- The PR must not be merged before the switch: develop's patches would not
  apply to the old checkout and every build would stop. Merge, switch,
  rebase, in that order, in one quiet window.

## Rehearsal (scratch checkout, not the shared one)

REHEARSAL_PLACEHOLDER

## The routine

In docs/REBASE.md: "Following Chromium releases". In short:

- A point release of our milestone (Beta or Stable): bump the pin within
  days.
- A newer milestone at Stable: rebase within about 2 weeks.
- The workflow `.github/workflows/chromium-releases.yml` opens and keeps up
  to date one `chromium-update` issue with the version, the due date and the
  patch check; it closes it when the pin has caught up.
- The Mac CI (#69, PR #70, `macos.yml`) builds whatever `CHROMIUM_VERSION`
  pins through `scripts/bootstrap.sh`, so it follows the pin by itself.
