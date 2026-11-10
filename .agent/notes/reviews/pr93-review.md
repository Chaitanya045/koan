# Review of PR #93: Unit tests and browser tests for Koan's own logic (draft, closes #87)

Head: 4f5df76 (2 commits). Read only: `gh pr view/diff`, the worktree sources, and
`grep` of Chromium BUILD.gn files for label checks. Nothing was built or run.

Counts: **4 Blockers, 7 Should-fix, 6 Nits.**

## Results: were the tests reported as run?

- **Browser tests (`koan_browsertests`): NOT RUN.** The PR body says: "Not yet run, this PR is a draft until
  they are". Only "the first 2154 steps compiled with no errors". They are not even fully built. That is
  3 files, 19 `IN_PROC_BROWSER_TEST_F`s.
- **Unit tests: the reported run does not cover the current tree.** "62/62 unit tests pass, before the final
  rebase". The tree now has 53 `TEST` + 9 `TEST_P` x 2 params = **71** cases (the body's table says 70).
  62 = 71 - 9, which is exactly the 9 tests in `koan_library_logic_unittest.cc`. That file arrived in the second commit
  (4f5df76), together with the `koan_library_history.cc` / `koan_library_spaces.cc` refactor. So the Library
  logic, its tests, and the two view edits were probably never built or run (inference from the counts, not
  something the PR states).
- GitHub CI proves nothing about the C++: it does not build Chromium on Linux. The "Unit tests" job is the
  Python branding tests. **"Lint" is red** (see B1).

## Blockers

**B1. CI Lint fails: shellcheck SC2054 at `scripts/test_browser.sh:19`.**
`flags=(--ozone-platform=headless ... --window-size=1280,800 --ozone-override-screen-size=1280,800)`:
shellcheck wants the commas quoted. Run: `gh run view 41553423960 --job 125646356185 --log`. The run's head SHA is
the current head. Fix: quote the two `--window-size=1280,800` words.

**B2. Browser tests have never run (see Results).** The PRE_ restore tests are the riskiest part of the PR
(see S1, S2) and could not have been checked. A draft is fine, but it must not merge or "close" #87 until a run
is pasted into the PR (Linux headless run, plus a macOS compile).

**B3. The last commit's code and tests were never built or run (see Results).** 9 tests plus edits to
`koan_library_history.cc` and `koan_library_spaces.cc` (shipping UI code). Needs a rerun of
`scripts/test_unit.sh` on the final head, and ideally an e2e of the Library (`library` / drag) because
`koan_library_spaces.cc:~490-510` now allocates and calls through the new helpers.

**B4. Licence: MPL-derived code moved into new MIT-headed files.**
`src/browser/koan_library_logic.{h,cc}` (header: "Copyright 2026 The Koan Authors ... MIT").
`FormatUrl` and `SiteOf` were moved verbatim out of `koan_library_history.cc`, which is MPL (it ports
`ZenLibraryHistorySection.mjs`; the comments say "Zen's #formatUrl", "as Zen groups By Site").
`DropSlot` and `CardShift` came out of `koan_library_spaces.cc`, also MPL (`ZenLibrarySpacesSection.mjs`,
`zen-library.css` card layout). CONTRIBUTING.md:200-214 says files that port Zen logic get the MPL header
and an entry in THIRD_PARTY_NOTICES.md. Fix: give `koan_library_logic.{h,cc}` the MPL header
(`ZenLibraryHistorySection.mjs`, `ZenLibrarySpacesSection.mjs`) and list them. The `_unittest.cc` file only
tests them, so MIT is probably fine for it.
The fuzzy split is done right on the header: `koan_fuzzy_score.{h,cc}` keep the MPL header.

## Should-fix

**S1. `WaitForRestore(1, 1)` is already true in a fresh browser** (`src/test/browser/koan_session_restore_browsertest.cc:60-65`,
used at :160 and :180). `tab count == 1` and `Space count == 1` is the state of a new window, so the wait
returns at once. `PinnedTabRestoresToPinnedUrl` and `SessionDataComesBack` can then read the pre-restore
tab (race), or pass/fail for the wrong reason. If `browser()` in the main test is not the restored window at
all, every `WaitForRestore` waits on the wrong window. Wait on the restore itself instead: Chromium's
`SessionRestoreTestHelper` / `ui_test_utils::WaitForBrowserToOpen`, then wait on the data (e.g.
`RunUntil` on `GetTabSessionData(...) == "tab data"`, or the pinned state).

**S2. PRE_ tests do not make sure the session is written before the browser closes.** The session service
writes asynchronously. Chromium's own restore tests flush or rely on a clean shutdown path. Here the PRE_
bodies just return after `NavigateToURL` / `CreateSpace` / `SetTabSessionData`. Worth confirming on the first
real run, and add a flush (e.g. close via `CloseBrowserSynchronously` pattern or wait for the session
commit) if it is flaky. Related: `SetStartupPref(LAST)` is applied in `SetUpOnMainThread`
(:54), which runs after the profile has started in the main run, so whether startup restore actually reads
it needs the first run to show.

**S3. `Closes #87` overclaims.** The issue asks for the folder tree and its (de)serialisation, Space ordering
(in unit tests), the tab-menu feature table, Mac shortcut variants, `agent-build.sh` wiring and a macOS CI
run (#69). The PR itself lists the folder tree, folder ordering and tab-menu table as not covered. Use
"Part of #87" (or split the rest into a follow-up) so the issue stays open.

**S4. `agent-build.sh` proposal (PR body, "Proposed change"): not sane as written.**
- The file is outside the repo (`~/work/koan/.cache/agent-build.sh`), so the PR does not deliver the "wired into
  agent-build.sh" requirement; nothing in the diff changes the agent brief
  (`.agent/notes/agent-brief.md:13-15`) to tell agents the tests exist.
- Every other open worktree does not have `scripts/test_unit.sh` yet, so `"$WT/scripts/test_unit.sh"`
  fails with "No such file" and sets `status=1` for all of them until they rebase. Guard with
  `[[ -x $WT/scripts/test_unit.sh ]]`. Likewise their `src/` has no `//koan/test`.
- No `timeout` (the e2e loop uses `timeout 600`, AGENT_CMD `timeout -k 30 1800`), and it runs `$WT/scripts/build.sh`
  while the main build used `$MAIN/scripts/build.sh`. A hung test would hold the shared lock. Use
  `timeout 600 ... 8>&- 9>&-` (the diff does close 8/9, good).
- The test build's output goes to stdout, not `$LOG`, and failures bypass the dirty-file bookkeeping
  (`rm -f "$touched" "$DIRTY"` has already run), so an unit-test compile failure is not re-touched next run.
- The first run compiles the unit-test support stack inside the lock (plus all of `//components/performance_manager`,
  see N5). Run it once outside the lock to prime, and say so.

**S5. Possible regressions to `gn gen` for the product build.** `group("tests")` in `src/browser/BUILD.gn` (end of file) makes every
`gn gen` load `//koan/test/BUILD.gn` and resolve `koan_browsertests`'s whole dependency graph, including
`//chrome/test:test_support_ui`, which is inside `if (!is_android)`. The release and Mac arg files
(`build/koan-release.gn`, `build/koan-mac.gn`) were not shown to work; a resolve error there would break the
macOS CI (#69) and release builds even though nothing builds the tests. Please paste `gn gen` output for
`build/koan-release.gn` and `build/koan-mac.gn`. Good: the group is `testonly`, nothing in `chrome` depends on
it, and the default `chrome` target does not get slower to compile.

**S6. Invisible characters in source.** `src/browser/koan_space_title.cc:31-32` replaced `u'️'` / `u'‍'` with the literal
variation selector and ZWJ (bytes EF B8 8F and E2 80 8D; verified). Behaviour is the same, but the lines are
unreadable in review and editors/formatters can strip them. Same in `koan_space_title_unittest.cc` (the `©️`
and the family string). Restore the `\u` escapes, as the original code had.

**S7. Mac-only paths of the tests never run on Linux** (`koan_shortcut_table_unittest.cc` `SwitchWorkspaceKeysAreMacOnly`
`#if BUILDFLAG(IS_MAC)` branch; the browser tests' `-ObjC` ldflags). The PR claims both targets "can be compiled by
the macOS CI" but #69 does not build tests yet, so these are untested code. At least do a one-off compile on the Mac
(ask first, per the Mac consent rule), or say plainly they are unverified.

## GN check (item 1)

Verified in the Chromium checkout (BUILD.gn grep): `//chrome/browser/ui/color:color_headers`,
`//components/sync_preferences:test_support`, `//components/prefs:test_support`,
`//chrome/browser/ui:browser_tabstrip`, `//chrome/browser/ui/tabs:tab_strip`,
`//chrome/browser/ui/views/frame:browser_widget`, `//chrome/browser/profiles:profile`,
`//chrome/browser/prefs`, `//chrome/test/base:browser_tests_runner`, `//chrome/test:test_support_ui`,
`//components/tabs:public`, `//components/tab_groups`, `//chrome:packed_resources|resources|strings`,
`//testing:test_scripts_shared`, `//base/test:run_all_unittests`, `//third_party/icu:icudata` all exist.
`use_xvfb_in_this_config` is defined in `build/config/ui.gni`, which the file imports.
- `testonly`: `test()` targets are implicitly testonly; `browsertests_impl` and `group("tests")` set it. OK.
- Unit tests do not link `//chrome/browser`; the one browser dep is the header-only `color_headers`. Good.
- `is_mac`: only the `-ObjC` ldflag, copied from `chrome/test/BUILD.gn` (:2926, :4372) pattern. Not verified on a Mac.
- Real `browser_tests` (`chrome/test/BUILD.gn:1207`) has many more deps (`:test_interfaces`, `//chrome/app:browser_tests`,
  `//chrome/browser:buildflags`, ...). The hand-picked set here might lack runtime data (Linux sandbox,
  Mac app helpers); only a run will tell (B2).

## Refactor equivalence (item 2): verified line by line, identical

- `FuzzyScore`: body identical to the removed `KoanCommandPalette::FuzzyScore` (same branches, constants, comment).
  Constants moved from the anonymous namespace to `koan::` in the header; the only remaining user
  (`koan_command_palette.cc:340`) still compiles against it. The two call sites in `Score()` are unchanged.
  `KoanCommandPalette::FuzzyScore` is removed from the header, and I found no other users in `src/` or `scripts/`.
- `SplitTitle` / `JoinTitle` / `IsEmoji`: moved verbatim; `KoanWorkspaceController::SplitTitle/JoinTitle` forward.
  Includes removed from `koan_workspaces.cc` (break_iterator, char_iterator, uchar.h) have no remaining users there.
  Only S6 (escape to literal).
- `FormatUrl` / `SiteOf`: verbatim. `DropSlot` / `CardShift`: same logic as the inlined loops, including the
  `i != drag_index` skip and strict `>` comparison. Behaviour change: a `std::vector<int>` is allocated on each
  drag-move event (N4).

## Test quality (item 3)

Good: real assertions with hand-derived numbers (`KnownScores`: I re-derived `tcm` = 73 including the
index-0 "run" quirk), edge cases (empty, too-long query, boundary `center == centre`), malformed
overrides, duplicate accelerators, flag/ZWJ clusters, light/dark parametrisation, feature-off mixer.
No sleeps, `PostDelayedTask` or `RunLoop` spins anywhere in the diff; waits use `base::test::RunUntil`.
Weaker spots:
- `koan_session_restore_browsertest.cc:99` etc.: see S1.
- `koan_spaces_browsertest.cc:56-57, 94-99`: `SwitchTo`, `Cycle`, `SwitchToNumber` and the "new Space is active" check
  are asserted immediately, assuming they are synchronous. They probably are; confirm on the first run.
- `koan_shortcut_table_unittest.cc` `EveryActionHasOneInfo`: relies on `kOpenSettings` being the last enum value; a new
  action appended after it fails with a confusing message (Nit N1).
- `TwoActionsCannotShareACustomKey` does not say which action keeps the key (only that both are present).
- `koan_prefs_unittest.cc` `ProfilePrefsAreInKoanNamespace` only registers Koan's prefs, so it cannot show that no
  upstream pref is shadowed, as its comment says; it checks Koan's own names. Fine, but fix the comment.
- Expected values in the colour test are the Zen constants, not recomputed from the mixer's code. Not tautologies.

## Licence headers (item 4)

- `koan_fuzzy_score.{h,cc}`: MPL header, correct. But **THIRD_PARTY_NOTICES.md is not updated**: line 28 lists
  `koan_command_palette.cc` for the fuzzy scoring; add `koan_fuzzy_score.{h,cc}` (CONTRIBUTING.md:200-203 requires it).
  (The command palette file keeps other Zen material, so its line stays.)
- `koan_library_logic.*`: wrong, see B4. Also add to THIRD_PARTY_NOTICES.md.
- `koan_space_title.*`: MIT; the source (`koan_workspaces.cc`) is MIT. OK.
- `koan_shortcut_table_unittest.cc` hard-codes Zen's keys and has an MIT header: acceptable (matches behaviour, not a port).

## Docs (item 5)

- CONTRIBUTING.md "Which test to write" is clear and short, and the three-way split matches the issue. Nits: "three kinds
  of C++ and browser tests" but one is the JS e2e (N2); `src/test/BUILD.gn:10,50` say `see "Tests" in CONTRIBUTING.md`, but the heading is
  "Which test to write" (N3). It does not mention the shellcheck/CI rule for new `scripts/*.sh`, which is what broke Lint
  (B1); and the "Run the same checks locally" block was not asked to include them.
- `test_browser.sh` hard-codes `out/Koan`, while `build.sh` honours `KOAN_CONFIG=release` (-> `out/Release`); `test_unit.sh` too. A
  release-config run builds one tree and runs the other's binary. Use the same `KOAN_OUT` convention as `scripts/e2e/run.sh:44`.
- The `agent-build.sh` proposal: see S4.

## Nits

- N1. `EveryActionHasOneInfo`: enum-last assumption (above).
- N2. CONTRIBUTING wording "three kinds of C++ and browser tests".
- N3. BUILD.gn comments reference a non-existent "Tests" section.
- N4. `koan_library_spaces.cc` builds a `std::vector<int>` per drag-move; a reused member or an iterator-based API avoids it.
- N5. `//koan/browser:features` has `public_deps` on `//components/performance_manager` and `//components/webui/flags`
  (`src/browser/BUILD.gn:20-23`), so "links only //base and //ui/base" is not literally true: the unit tests link
  performance_manager, so the first build is not as short as the docs claim ("a few seconds after a short build").
- N6. PR body says 70 tests and 62 passed; the tree has 71 cases. Update the numbers after a rerun.

## Recommendation

**Do not merge yet; keep as draft.** Fix B1 (one-line quoting), B4 (headers + notices), rerun the unit tests on
the final head (B3), run the browser tests on Linux and paste the result (B2), address S1/S2 (restore waits),
change "Closes #87" to "Part of #87", and make the `agent-build.sh` change safe for worktrees without the script.
The refactors themselves are behaviour-identical and the unit tests are good quality.
