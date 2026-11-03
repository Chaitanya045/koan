# Contributing to Koan

Thanks for your interest in Koan. This guide covers how to report issues,
set up a development build, and get a change merged. Everyone taking part
is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

- [Before you start](#before-you-start)
- [Development setup](#development-setup)
- [How Koan changes Chromium](#how-koan-changes-chromium)
- [Code style](#code-style)
- [Testing](#testing)
- [Commits](#commits)
- [Pull requests](#pull-requests)
- [Releases and Chromium updates](#releases-and-chromium-updates)
- [Licensing](#licensing)

## Before you start

### Issues

- Search the [existing issues](https://github.com/Chaitanya045/koan/issues)
  before opening a new one.
- Use the bug report or feature request template, and be specific: the
  Koan version, your desktop (e.g. GNOME 48 on Wayland), the steps to
  reproduce, and what you expected.
- Check whether the problem goes away when you switch the Koan feature off
  in `chrome://flags`. If it doesn't, it's probably a Chromium bug and
  belongs on the [Chromium tracker](https://issues.chromium.org).
- Report security vulnerabilities privately, as described in
  [SECURITY.md](SECURITY.md), never in a public issue.

### Contributions

- For anything beyond a small fix, start with an issue, and wait for a
  maintainer to confirm the bug or agree on the feature before writing code.
  A Chromium build is expensive, so agreeing on the approach first saves
  everyone time.
- To work on an existing issue, comment on it so others know.
- Koan follows Zen's behaviour. When a change is about how something
  should behave, say how Zen does it. Zen's source is the reference, and
  [docs/ZEN_PARITY.md](docs/ZEN_PARITY.md) records where Koan matches or
  deliberately differs.
- AI-assisted contributions are welcome. You're responsible for every line
  you submit: understand it, build it and test it yourself.

## Development setup

### Requirements

- Linux x86-64, on X11 or Wayland. Koan is developed on an Arch-based
  distribution; any distribution that can build Chromium should work.
  For macOS, see [macOS](#macos) below.
- 16 GB of RAM (more is better), and 100 GB or more of free disk space.
- The tools Chromium's
  [Linux build instructions](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md)
  list. `scripts/bootstrap.sh` installs depot_tools and runs Chromium's
  dependency hooks for you.

### Getting the source and building

```sh
git clone https://github.com/Chaitanya045/koan.git && cd koan
./scripts/bootstrap.sh          # depot_tools and the Chromium revision in CHROMIUM_VERSION
./scripts/apply_patches.sh      # link chromium/src/koan -> src/, apply patches/
./scripts/apply_branding.sh     # Koan's name and logos
./scripts/build.sh              # development build: chromium/src/out/Koan/chrome
```

The first build takes 10–15 hours on an 8-core machine. After that,
changes to Koan's code (`src/`) rebuild in minutes. `JOBS=<n>` limits
parallel compile jobs, and each job can take about 1 GB of RAM.
`KOAN_CONFIG=release` builds the optimised `out/Release` configuration
instead.

### macOS

Koan is coming to macOS on Apple Silicon
([#51](https://github.com/Chaitanya045/koan/issues/51)). It builds from
source with the same scripts, but the Mac build is new, so expect rough
edges. You need:

- An Apple Silicon Mac with the full **Xcode** (not only the Command Line
  Tools). The scripts use `DEVELOPER_DIR`, not `xcode-select`.
  Install its Metal toolchain too (a separate download since Xcode 26,
  needed for ANGLE's shaders): `xcodebuild -downloadComponent MetalToolchain`.
- About **60 GB** of free disk: roughly 30 GB for the checkout and the rest
  for `out/Koan`. `build/koan-mac.gn` builds without debug symbols to keep
  it small.

Nothing else: the logo PNGs the Mac needs are rendered on Linux and
committed in `branding/mac/rendered/`, and only Xcode's `iconutil` and
`actool` run on the Mac. The scripts run with macOS's own bash 3.2 and BSD
tools.

**Everything stays in one folder**, e.g. `~/work/koan-mac`, so Chromium's
tools (depot_tools, vpython, CIPD, siso) and Apple's don't scatter caches
across your home directory. The folder's `env.sh` points `HOME`, `TMPDIR`
and the tool caches into it, and every script refuses to run on macOS until
it has been sourced:

```sh
# ~/work/koan-mac/env.sh, with Koan cloned to ~/work/koan-mac/koan.
# Source this before any Koan command on this Mac.
export KOAN_MAC="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export HOME="$KOAN_MAC/home"
export TMPDIR="$KOAN_MAC/tmp"
export XDG_CACHE_HOME="$KOAN_MAC/home/.cache"
export XDG_CONFIG_HOME="$KOAN_MAC/home/.config"
export VPYTHON_VIRTUALENV_ROOT="$KOAN_MAC/home/.vpython-root"
export CIPD_CACHE_DIR="$KOAN_MAC/home/.cipd-cache"
export DEPOT_TOOLS_METRICS=0
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export PATH="$KOAN_MAC/koan/depot_tools:$PATH"
```

```sh
source ~/work/koan-mac/env.sh && cd "$KOAN_MAC/koan"
./scripts/bootstrap.sh          # Chromium for target_os mac, at CHROMIUM_VERSION
./scripts/apply_patches.sh
./scripts/apply_branding.sh     # also builds the app icon (iconutil, actool)
./scripts/build.sh              # build/koan-mac.gn: chromium/src/out/Koan/Koan.app
./scripts/run_mac.sh            # test run, profile in $KOAN_MAC/profile
```

`JOBS` defaults to 2, 4 or 6 compile jobs depending on the memory free when
the build starts; set it to override.

What still lands outside the folder:

- Xcode itself, in `/Applications`.
- The app, when you use it day to day (opened from Finder or the Dock):
  its profile in `~/Library/Application Support/Koan`, its cache in
  `~/Library/Caches/Koan`, its preferences and saved window state under
  `~/Library` (`dev.koan.Koan`), a code-signing clone under `/var/folders`,
  and its "Koan Safe Storage" item in the login keychain.
  `scripts/run_mac.sh` keeps a test run inside the folder instead.
- Possibly some caches of Apple's tools under `/var/folders` (e.g. `xcrun`'s
  lookup cache). The scripts point Apple's tools at the folder's home
  (`CFFIXED_USER_HOME`), but not every tool honours it.

The app is unsigned and meant for local use. Each rebuild changes its ad-hoc
signature, so macOS asks again for access to the keychain item.
`install_desktop.sh` and the off-screen test tools (`scripts/e2e/`,
`shot_ui.sh`, ...) use Linux's headless Ozone platform, so test behaviour on
Linux.

### macOS nightly build

[`.github/workflows/macos.yml`](.github/workflows/macos.yml) builds
`Koan.app` on GitHub's free macOS runners every night from `develop`, and
publishes `Koan-<version>-<sha>-arm64.dmg` as a run artifact and as the
rolling `nightly` pre-release (see
[Installing the macOS build](docs/USAGE.md#installing-the-macos-build)).
It is the only place the Mac code is compiled without the maintainer's
Mac, so it catches Mac-only build errors.

- It uses the same scripts as above (`bootstrap.sh`, `apply_patches.sh`,
  `apply_branding.sh`, `build.sh` with `build/koan-mac.gn`), with
  `scripts/ci/mac_env.sh` as the runner's `env.sh`.
- A first build takes much longer than GitHub's 6-hour job limit, so it
  runs as a chain: a fetch job, then build jobs that each build for about
  5 hours and hand `out/Koan` on to the next as an artifact, then a
  packaging step and the release. A whole run takes about 16 hours
  (4 build jobs).
- **It never runs on pushes.** A run is a full build of about 16 hours,
  so it runs only nightly or when asked for. To build a branch or pull
  request with Mac-specific code (a compile check plus a .dmg to try):
  - add the **`macos-build` label** to the pull request (builds its head
    once; remove and add the label again for another run), or
  - `gh workflow run macos.yml -f ref=<branch>` (or
    `-f ref=refs/pull/<n>/head`), or "Run workflow" on the Actions tab.

  Pull requests from forks are never built automatically. On every pull
  request, `ci.yml` lints the workflows (actionlint) and scripts
  (shellcheck), which is the cheap check for changes to the Mac build.
- The nightly is skipped when nothing that goes into the app (`src/`,
  `patches/`, `branding/`, `build/`, `scripts/`, `CHROMIUM_VERSION`, the
  macOS workflows) changed since the commit the `nightly` release was built
  from. Manual runs always build.
- **If a job fails** for a reason outside the code (a network error, a
  runner problem), use "Re-run failed jobs": the run carries on from the
  last job that finished, with the same commit (the build artifacts are
  kept for 2 days). A fix in the code needs a new run.

How it works and what was measured: [.agent/notes/macos-ci.md](.agent/notes/macos-ci.md).

### Repository layout

| Path | Contents |
|---|---|
| `src/` | All of Koan's code, built as GN `//koan` (symlinked as `chromium/src/koan`) |
| `patches/` | Hook patches for Chromium: one `<upstream path>.patch` per changed upstream file |
| `branding/` | Logo sources (SVG) and the macOS app icon (`mac/AppIcon.icon`). `scripts/apply_branding.sh` renders them into Chromium |
| `build/` | GN arguments: `koan.gn` (development), `koan-release.gn` (release), `koan-mac.gn` (macOS) |
| `scripts/` | Bootstrap, build, patch and branding tools, and test helpers |
| `scripts/e2e/` | End-to-end tests that drive a real Koan build off-screen |
| `docs/` | User and maintainer documentation |
| `.agent/notes/` | Design notes, research, decisions and review records |
| `chromium/`, `depot_tools/` | The Chromium checkout and its tools (not tracked) |

## How Koan changes Chromium

Koan keeps its distance from upstream code so that moving to a new Chromium
version stays cheap.

- **Koan's code lives in `src/`.** New views, controllers, prefs and features
  go there, in `namespace koan`.
- **Upstream changes are hook patches.** When Chromium needs to call into
  Koan, add the smallest possible hook: a few lines that call a `//koan`
  function, plus the `BUILD.gn` dependency. Don't move upstream logic into a
  patch.
  1. Edit the file in `chromium/src`.
  2. Run `./scripts/apply_patches.sh export` to refresh `patches/`.
  3. Add a row for the new hook to the hook table in
     [docs/REBASE.md](docs/REBASE.md).
- **Never change global compile flags, include paths or widely included
  headers** unless a maintainer agrees. Any of these forces a full rebuild
  of Chromium.
- **Product name and logos** go through `scripts/apply_branding.sh`, not
  patches. After changing `branding/*.svg`, run
  `scripts/apply_branding.sh --render-mac` on Linux and commit
  `branding/mac/rendered/`.
- **Every user-visible feature is behind a flag:** a `koan::features`
  `base::Feature` with a `chrome://flags` entry. That lets users and bug
  reports turn it off.

## Code style

- Follow [Chromium's C++ style](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++.md)
  and the conventions of the surrounding code. Prefer existing Chromium and
  Koan patterns over new abstractions.
- Format C++ with clang-format 22.1.8 and the repository's `.clang-format`:
  `git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file -i`.
- Format GN files (`BUILD.gn`, `*.gni`) with `gn format`, never with clang-format (it reads them as C++ and corrupts
  the comments; `.clang-format-ignore` stops it). `scripts/ci/check_gn_format.sh` checks them; files that predate it are
  listed in `scripts/ci/gn_format_exceptions.txt`.
- New files start with Koan's license header:

  ```cpp
  // Copyright 2026 The Koan Authors
  // Use of this source code is governed by the MIT license that can be
  // found in the LICENSE file.
  ```

  A file that **ports Zen Browser's code, data tables, SVG paths, strings or
  style values** (more than matching a behaviour or a few numbers) is
  covered by Zen's MPL-2.0 instead. Give it this header, list it in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and keep the ported
  material in as few files as practical:

  ```cpp
  // This Source Code Form is subject to the terms of the Mozilla Public
  // License, v. 2.0. If a copy of the MPL was not distributed with this
  // file, You can obtain one at https://mozilla.org/MPL/2.0/.
  //
  // Contains material ported from Zen Browser, Copyright the Zen Browser
  // contributors (https://github.com/zen-browser/desktop), MPL-2.0:
  //   ZenSomething.mjs
  // Koan's changes, Copyright 2026 The Koan Authors, are under the same
  // licence.
  ```

- **UI colours and shapes come from tokens.** Use Chromium or Koan colour
  IDs, mapped in a colour mixer, and shared radius and inset constants. Never
  hard-code `SkColor`s or radii, so themes can restyle the whole UI.
- Comment what isn't obvious from the code, such as why a hook exists or
  which Zen behaviour a value comes from. Don't leave dead code or
  commented-out code behind.

## Testing

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs on every
pull request. It checks that:

- every patch applies to the Chromium revision in `CHROMIUM_VERSION`;
- the Python unit tests (the branding rules) pass;
- clang-format, shellcheck and `node --check` find no problems.

CI doesn't build Chromium on Linux, so the C++ tests (`koan_unittests` and
`koan_browsertests`) run on your build instead, and the macOS CI can compile
them (see below).

Run the same checks locally before pushing:

```sh
scripts/ci/check_patches.sh
python3 -m unittest discover -s scripts/tests
git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file --dry-run --Werror
shellcheck -S warning scripts/*.sh scripts/ci/*.sh scripts/e2e/*.sh
```

The macOS build runs separately, every night and on demand
([macOS nightly build](#macos-nightly-build)).

Browser behaviour is tested against a real build, off-screen, so nothing
opens on your desktop:

- `scripts/e2e/run.sh <test>` runs one end-to-end test from
  `scripts/e2e/<test>.mjs` (for example `layout`, `shortcuts`, `glance`,
  `workspaces`, `scrollbars`). The tests inspect native views through UI
  DevTools and send real input through the DevTools protocol.
- `scripts/shot_ui.sh <out.png> -- <url>` renders the whole browser window,
  UI included, to a screenshot.
- `scripts/run_profile.sh` runs Koan on a persistent test profile, which is
  useful for testing restarts and session restore.

Add an end-to-end test for new behaviour, and keep the existing ones
passing. For UI changes, compare your screenshots with Zen's.

Icons (`.agent/notes/koan-icons.md`) have two tests of their own:

- `koan_icons_unittests` (`src/browser/ui/vector_icons/koan_icons_unittest.cc`):
  the registry table, the gfx hook (a Chromium icon paints pixel-equal to its
  Koan icon) and the URL bar magnifier's ink. It is not in CI and not part of
  another test binary: build it with `scripts/build.sh koan_icons_unittests`
  and run `out/Koan/koan_icons_unittests`.
- `scripts/e2e/icons.mjs` checks which icon each surface paints, at which
  size. It reads `--koan-icon-trace=<file>`, a developer switch that only
  builds with DCHECKs have (it does nothing in a release build); run it with
  `scripts/e2e/run.sh icons`.

### Which test to write

Koan has three kinds of tests. Pick the cheapest one that
can fail for the reason you care about.

| Test | Use it for | Run it |
| --- | --- | --- |
| Unit test (`koan_unittests`) | Pure logic: scoring and ordering, parsing and formatting, tables of data, prefs defaults, colour maths. It must link without the browser. | `scripts/test_unit.sh`, a few seconds after a short build |
| Browser test (`koan_browsertests`) | State that lives in the browser's own objects: Spaces, pinned tabs, what Koan keeps in the session and gets back after a restart. | `scripts/test_browser.sh`, on request: it starts a browser per test |
| End-to-end test (`scripts/e2e`) | What the user sees and does: views, layout, input, menus, animations. | `scripts/e2e/run.sh <test>` |

#### Adding a unit test

1. Put the logic in a small `source_set` that depends on `//base` (and
   `//ui/base` or the like) only, not on `//chrome/browser`. If it sits in a
   big file next to views, move it out, as `koan_fuzzy_score.cc` and
   `koan_space_title.cc` were moved out of the command palette and the
   Spaces; keep the original call site a one-liner.
2. Write `foo_unittest.cc` next to `foo.cc` with plain gtest, and list it in
   the `sources` of `koan_unittests` in `src/test/BUILD.gn`. Add the
   `source_set` to its `deps`.
3. Run `scripts/test_unit.sh --gtest_filter='Foo*'`.

Test the edge cases that an end-to-end run can't reach cheaply: empty input,
the largest and smallest values, malformed prefs, and each platform's branch
(`BUILDFLAG(IS_MAC)`). A test that reads a table (like the shortcut table's)
should check its invariants, so that a new row that breaks them fails at
once.

#### Adding a browser test

Derive from `InProcessBrowserTest`, put the file in `src/test/browser/` and
list it in `browsertests_impl` in `src/test/BUILD.gn`. `koan_browser_test_utils.h`
has helpers for Koan's controllers. To test a restart, write a `PRE_Foo` test
(the first browser, closed at the end) and a `Foo` test (the next one, on the
same profile and with "continue where I left off" set), as
`koan_session_restore_browsertest.cc` does. Use `chrome://` pages, which load
without a server.

Both targets also build for macOS (`is_mac`), and the macOS CI can compile
them. Keep platform code in them behind `BUILDFLAG(IS_MAC)` too.

## Commits

- **Title:** one line that says what changed for the user, in plain words,
  65 characters or fewer, so the squash-merged title with ` (#123)` stays
  within 72. For example:

  ```
  Open Glance on Alt+click, like Zen
  Thin overlay scrollbars, like Zen
  docs: Koan vs Zen parity matrix
  ```

  Use a `docs:`, `ci:`, `build:` or `notes:` prefix for changes that don't
  touch the browser.
- **Body:** if the title isn't enough, explain why the change was made and
  anything a reviewer or a future rebase needs to know.
- **History:** keep it tidy. Fold fixes into the commit they fix
  (`git commit --fixup` and an interactive rebase), and split large changes
  into commits that each build.

## Pull requests

- Branch from `develop` with a descriptive name (`feat/…`, `fix/…`,
  `docs/…`, `build/…`, `ci/…`), and open the pull request against `develop`.
- Keep each pull request to one issue, and link it with `Fixes #123`. If an
  issue is only partly done, write `Part of #123` and list what's left.
- Fill in the template:
  - what changed;
  - how Zen does it;
  - how you tested it;
  - screenshots or a short video for UI changes.

  Mark the pull request as a draft until it builds and its tests pass.
- Update [docs/USAGE.md](docs/USAGE.md) for new features and shortcuts, and
  [docs/ZEN_PARITY.md](docs/ZEN_PARITY.md) when parity changes.
- Rebase on `develop` before review. `develop` is protected: pull requests
  need passing CI and are reviewed by a maintainer before they're
  squash-merged.

## Releases and Chromium updates

- `develop` is the default branch and is always kept releasable. Releases
  are tagged from it as `vX.Y.Z`.
- Moving to a new Chromium version is a dedicated pull request that updates
  `CHROMIUM_VERSION` (always a release tag), refreshes every patch, and
  re-audits the hooks, as [docs/REBASE.md](docs/REBASE.md) describes.
  Point releases of our milestone are taken within days, a new milestone
  within about two weeks of reaching Stable; the `chromium-update` issue says
  when one is due.

## Licensing

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE), except in files that carry the MPL-2.0 header
because they port Zen Browser material: your changes there are under the
MPL-2.0. Changes to upstream Chromium files in `patches/` remain under
Chromium's BSD-style license; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
