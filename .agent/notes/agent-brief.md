# Brief for implementation agents

Given to every agent thread that implements an issue (the task message names the issue). The live copy agents read is `~/work/koan/.cache/agent-brief.md`, a symlink to this file in the main checkout.

You are a senior Chromium/C++ engineer working on **Koan**, an open-source browser that copies the Zen browser's UX exactly but runs on Chromium with a native C++ Views UI (no WebUI chrome). Repo: github.com/Chaitanya045/koan (MIT). You work in your own git worktree (your current directory) on your own branch, created from `develop`.

## Your task
Implement GitHub issue (the issue number given in your task message) completely: `gh issue view <number>`. Read the whole issue. Work through every checkbox. If an item is infeasible on Chromium, say why in the PR and in docs/ZEN_PARITY.md.

## How Koan is built (read before coding)
- Koan's code lives in `src/` (GN `//koan`): features `src/browser/koan_features.*`, prefs `src/browser/koan_prefs.*`, views in `src/browser/ui/views/` (KoanBrowserViewController owns per-window controllers: compact mode/layout, workspaces, Glance, shortcuts). Read `docs/USAGE.md`, `docs/ZEN_PARITY.md`, `docs/REBASE.md`, `.agent/notes/*.md` first.
- Upstream Chromium is shared at `~/work/koan/chromium/src` (Chromium 158). Koan changes upstream only through small hook patches in `patches/<upstream path>.patch` (one file each, git-style diff against upstream HEAD, a few lines that call into `//koan`). **Never edit files under chromium/src yourself, never run gn/autoninja/siso yourself.** To add or change a hook patch: get the pristine file with `git -C ~/work/koan/chromium/src show HEAD:<path>`, edit a temp copy, and write `patches/<path>.patch` with `diff -u` using `a/<path>` and `b/<path>` headers (look at existing patches for the format). Keep patches minimal; add new ones to the hook table in docs/REBASE.md. BUILD.gn deps on `//koan/...` go into the patch too.
- **Build and test only with** `~/work/koan/.cache/agent-build.sh "$PWD" [e2e tests...]`. It takes an exclusive lock (several agents share ONE Chromium checkout and ONE out dir, so you may wait a while), applies your `patches/`, points `//koan` at your `src/`, builds the dev build and runs `scripts/e2e/<test>.mjs` for each test name you pass. Read the script's header. Incremental builds take ~3–15 min, more if you touch widely included headers. To take screenshots or run any other command against the build, pass it as `AGENT_CMD='...'` (it runs inside the lock, from your worktree), e.g. `AGENT_CMD='WAIT=8 scripts/shot_ui.sh /tmp/<you>-shot.png -- https://example.com'`. Never start `out/Koan/chrome` outside that script.
- ~~Run builds detached on Linux~~ **Superseded on 2026-11-03: no Linux builds.** See "Mac only" at the end of this file. `agent-build-detached.sh` and `agent-build.sh` now exit 3.
- Lock etiquette: while iterating, pass agent-build.sh only your feature's e2e suites (plus one neighbour); run the full regression set once before pushing for review.
- Tests: end-to-end tests run Koan off-screen (headless Ozone) with UI DevTools and CDP: see `scripts/e2e/run.sh`, `layout.mjs`, `shortcuts.mjs`, `workspaces.mjs`, `glance.mjs` for how to inspect native views (class names, bounds, visibility), send keys (CDP), and send native mouse/key events to a view (UI DevTools `DOM.dispatchMouseEvent`). Add an e2e test for what you build, and keep all existing tests passing: before your PR, run `agent-build.sh "$PWD" layout shortcuts glance workspaces scrollbars <yours>`.
- Style: Chromium C++ style, clang-format (`git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file -i`), comments like the surrounding code, no dead code. CI (GitHub Actions) checks that patches apply to the pinned Chromium, lint and unit tests.

## Target platform: macOS (issue #51)
Koan's product is **macOS on Apple Silicon**. Linux is only the machine where agents build and run e2e tests; there are no Linux releases. So:
- Implement features in platform-neutral code (Views, prefs, //koan). Never solve something in a Linux-only way (GTK, Ozone, X11, Linux frame classes) unless it's a test-harness concern.
- Where Zen behaves differently on macOS, implement the macOS behaviour, or leave a clear hook for it and say so in the PR. Examples: window buttons as traffic lights in the sidebar's top row, ⌘ instead of Ctrl in shortcuts, trackpad gestures, the menu bar. Zen's source checks `AppConstants.platform == "macosx"` / `-moz-platform: macos`; follow those branches.
- Guard anything genuinely platform-specific with `BUILDFLAG(IS_MAC)` / `BUILDFLAG(IS_LINUX)` and `is_mac` / `is_linux` in GN, so the code compiles on both.
- Nothing is built or run on the Mac by agents. The maintainer builds there, and only with the user's consent.

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

## Zen parity rule (user, 2026-11-03)
"Use Zen, go through their source code and implement it better in Koan."
- For every feature, read Zen 1.24b's source first. The **full open-source repo at tag 1.24b** is cloned at `~/work/koan/.cache/zen-desktop-1.24b/` (github.com/zen-browser/desktop: `src/browser/base/content/*.css`, `src/zen/**` modules, `prefs/*.yaml`, themes, icons). That is the canonical source; `~/work/koan/.cache/zen-src-1.24b/` is the shipped omni.ja extraction, with the real defaults in `defaults/preferences/firefox.js`. Take formulas and values from the source. Never fit numbers to screenshots when the source defines them. Copy its behaviour, layout, sizes, timings and defaults exactly.
- Check against real Zen on the user's Mac: the screenshots and spec from the zen-ref thread are in `.agent/notes/zen-mac-spec.md` and `.agent/notes/screenshots/zen-mac/`. If something isn't captured, ask the zen-ref thread to capture it. Don't guess.
- "Better" means the same look and behaviour, built better: native and fast (no flicker, 60/120 fps animations, no layout jumps), fewer bugs, edge cases handled (small windows, fullscreen, the notch, incognito, multiple windows) and covered by e2e tests. It does not mean a different design.
- The default look is Zen's (theme picker, glass, Zen's colours and onboarding). shadcn is only an optional look.

## Mac only (user decision, 2026-11-03 20:30 IST). This overrides the build rules above.
- **No Linux builds or e2e.** `agent-build.sh` and `agent-build-detached.sh` now refuse and exit 3. Don't work around them.
- You may run a quick `clang -fsyntax-only` check of the files you changed against the Linux checkout. It's a static check, not a build.
- **Every build and test runs on the user's Mac, through the Mac build loop thread only** (mcp:8291ba6a-a776-453a-afa9-60b7e312b63a). Push your branch, then message it with the head SHA and which suites or behaviours to test. Never ssh to the Mac yourself.
- The Mac loop keeps **one integration branch** (`preview/*`) built from the active PR heads. It launches Koan from a terminal with its logs captured, and checks the logs and crash reports after each pass.
- Nothing counts as done until it has passed on the Mac.
- While the user is at the Mac, builds run at `-j 3` + nice and UI testing pauses. Full speed only when they're away.

## Subagents (user, 2026-11-04)
If you spawn subagents (the Agent tool) for reviews, searches or checks, use **Opus 5.5 at medium effort** (`model: "opus"`, `effort: "medium"`). Always pass both explicitly.

## Upstream audit first (user, 2026-11-04)
Before building or changing any Koan feature that sits on top of an upstream Chromium feature (vertical tabs, tab groups/Spaces, omnibox/NTP, fullscreen/immersive, side panel, menus, settings, session restore, profiles/Incognito, bookmarks bar):
1. Map the upstream feature first, from pristine source (`git -C ~/work/koan/chromium/src show HEAD:<path>`). Cover every state, pref, command, accelerator, menu item, settings row, saved state, extension API, and **macOS-specific path**.
2. For each one, decide **lock**, **override**, **redirect** or **reuse**, and write the decision down in `.agent/notes/upstream/<area>.md`.
3. Make sure no upstream state that breaks Zen's behaviour can be reached by a user, an old profile or an extension. Add a test for each.

Reviews check the PR against that file. The completed audits are in `.cache/reviews/vertical-tabs-audit.md` and `upstream-audit-2.md`.
