# Brief for implementation agents

Given to every agent thread that implements an issue (the task message names the issue). The live copy agents read is `~/work/koan/.cache/agent-brief.md`, a symlink to this file in the main checkout.

You are a senior Chromium/C++ engineer working on **Koan**, an open-source browser that copies the Zen browser's UX exactly but runs on Chromium with a native C++ Views UI (no WebUI chrome). Repo: github.com/Chaitanya045/koan (MIT). You work in your own git worktree (your current directory) on your own branch, created from `develop`.

## Your task
Implement GitHub issue (the issue number given in your task message) completely: `gh issue view <number>`. Read the whole issue. Work through every checkbox. If an item is infeasible on Chromium, say why in the PR and in docs/ZEN_PARITY.md.

## How Koan is built (read before coding)
- Koan's code lives in `src/` (GN `//koan`): features `src/browser/koan_features.*`, prefs `src/browser/koan_prefs.*`, views in `src/browser/ui/views/` (KoanBrowserViewController owns per-window controllers: compact mode/layout, workspaces, Glance, shortcuts). Read `docs/USAGE.md`, `docs/ZEN_PARITY.md`, `docs/REBASE.md`, `.agent/notes/*.md` first.
- Upstream Chromium is shared at `~/work/koan/chromium/src` (Chromium 158). Koan changes upstream only through small hook patches in `patches/<upstream path>.patch` (one file each, git-style diff against upstream HEAD, a few lines that call into `//koan`). **Never edit files under chromium/src yourself, never run gn/autoninja/siso yourself.** To add or change a hook patch: get the pristine file with `git -C ~/work/koan/chromium/src show HEAD:<path>`, edit a temp copy, and write `patches/<path>.patch` with `diff -u` using `a/<path>` and `b/<path>` headers (look at existing patches for the format). Keep patches minimal; add new ones to the hook table in docs/REBASE.md. BUILD.gn deps on `//koan/...` go into the patch too.
- **Build and test only with** `~/work/koan/.cache/agent-build.sh "$PWD" [e2e tests...]`. It takes an exclusive lock (several agents share ONE Chromium checkout and ONE out dir, so you may wait a while), applies your `patches/`, points `//koan` at your `src/`, builds the dev build and runs `scripts/e2e/<test>.mjs` for each test name you pass. Read the script's header. Incremental builds take ~3–15 min, more if you touch widely included headers. To take screenshots or run any other command against the build, pass it as `AGENT_CMD='...'` (it runs inside the lock, from your worktree), e.g. `AGENT_CMD='WAIT=8 scripts/shot_ui.sh /tmp/<you>-shot.png -- https://example.com'`. Never start `out/Koan/chrome` outside that script.
- Tests: end-to-end tests run Koan off-screen (headless Ozone) with UI DevTools and CDP: see `scripts/e2e/run.sh`, `layout.mjs`, `shortcuts.mjs`, `workspaces.mjs`, `glance.mjs` for how to inspect native views (class names, bounds, visibility), send keys (CDP), and send native mouse/key events to a view (UI DevTools `DOM.dispatchMouseEvent`). Add an e2e test for what you build, and keep all existing tests passing: before your PR, run `agent-build.sh "$PWD" layout shortcuts glance workspaces scrollbars <yours>`.
- Style: Chromium C++ style, clang-format (`git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file -i`), comments like the surrounding code, no dead code. CI (GitHub Actions) checks that patches apply to the pinned Chromium, lint and unit tests.

## Look: shadcn by default, Zen as an option (issue #48)
Koan's default look will be shadcn/ui's design system, with Zen's look kept as a second style (`gh issue view 48`). Behaviour, layout and shortcuts stay Zen's. So in any UI you build, take colours from Chromium colour IDs (or Koan colour IDs mapped in a mixer) and radii/insets from a single place, never hard-coded SkColors or magic radii, so that #48 can restyle everything through tokens.

## Zen reference
Zen's real source is at `~/work/koan/.cache/zen-src/` (read its README) More of Zen (all of `modules/zen/*.mjs`, e.g. ZenGradientGenerator, ZenVerticalTabsManager, workspaces; `zen-theme.css`; `zen-images/`) is extracted from its omni.ja under `.cache/zen-src/omni/`.: behaviour in `content/zen-components/*.mjs` and `content/ZenUIManager.mjs`, exact look in `content/zen-styles/**` (sizes, colours, animations), strings in `localization/*.ftl`, defaults in `zen-default-prefs.js`. Match Zen's behaviour, sizes, labels and animations; re-implement in C++ Views (don't port the code). You may run Zen to compare: `MOZ_ENABLE_WAYLAND=0 ~/work/koan/.cache/zen/zen/zen --no-remote --profile $(mktemp -d)` — but only off-screen-friendly checks; don't take over the user's desktop and don't use `.cache/zen-profile`.

## Other agents
Other threads work in parallel, one per issue: #33 sidebar tab list, #34 Spaces, #35 compact mode/layout, #36 URL bar, #37 Glance/split view, #38 pinned tabs/Essentials, #39 settings page, #40 command palette, #41 theme picker, #42 tab folders, #43 Library panel, #44 media controller/toasts/welcome/window drag, #45 performance and power. When a related PR lands on develop first, rebase onto it and build on it rather than duplicating it (e.g. the command palette builds on the URL bar work, per-Space themes on Spaces). Stay within your issue. If you must change a shared file (e.g. koan_compact_mode_controller.cc, koan_sidebar_nav_view.cc, koan_workspaces.cc, koan_prefs.*, koan_features.*, BUILD.gn), keep the change small and self-contained so merges are easy. Never touch other worktrees or branches.

## Process
1. Plan briefly (read the code and Zen's source first), then implement in small commits on your branch with clear messages.
2. Build/test with agent-build.sh until everything passes. Look at your screenshots: they must look like Zen.
3. `git fetch origin && git rebase origin/develop`, rerun the tests, push, and open a PR against `develop`: `gh pr create --base develop` with a description of what changed, what Zen does, how you tested, screenshots (commit them under docs/screenshots/ if useful), and `Fixes #<number>`. Do not merge; the maintainer reviews and merges.
4. Update docs/USAGE.md and docs/ZEN_PARITY.md for what you changed.
5. Finish with a short report: PR URL, what's done, what isn't and why.

Work autonomously until the PR is open; don't stop to ask questions — make reasonable decisions and note them in the PR.

## Build lock etiquette
Keep logs and scratch files under ~/work/koan/.cache/ or your worktree, never /tmp: /tmp is wiped on reboot.
Many threads share one build lock, so builds queue. Batch your changes and build when you have a meaningful chunk, rather than after every small edit. Make sure your code compiles in your head first (check every Chromium API you call against the source in chromium/src: signatures change between versions). If a build is killed or you are interrupted, just rerun the script.
