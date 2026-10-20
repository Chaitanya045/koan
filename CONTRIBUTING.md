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
  Tools). The scripts use `DEVELOPER_DIR`, not `xcode-select`; it defaults
  to `/Applications/Xcode.app/Contents/Developer`.
- About **60 GB** of free disk: roughly 30 GB for the checkout and the rest
  for `out/Koan`. `build/koan-mac.gn` builds without debug symbols to keep
  it small.
- `brew install librsvg` to render the logos. Without it,
  `apply_branding.sh` falls back to macOS's `sips`.

The scripts run with macOS's own bash 3.2 and BSD tools. Keep everything in
one folder, so Chromium's tools don't scatter caches across your home
directory: an `env.sh` that points `HOME`, `TMPDIR` and the tool caches into
that folder, sourced before every Koan command.

```sh
# ~/work/koan-mac/env.sh, with Koan cloned to ~/work/koan-mac/koan
export KOAN_MAC="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export HOME="$KOAN_MAC/home" TMPDIR="$KOAN_MAC/tmp"
mkdir -p "$HOME" "$TMPDIR"
export XDG_CACHE_HOME="$HOME/.cache" XDG_CONFIG_HOME="$HOME/.config"
export VPYTHON_VIRTUALENV_ROOT="$HOME/.vpython-root" CIPD_CACHE_DIR="$HOME/.cipd-cache"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export PATH="$KOAN_MAC/koan/depot_tools:$PATH"
```

```sh
source ~/work/koan-mac/env.sh && cd "$KOAN_MAC/koan"
./scripts/bootstrap.sh          # Chromium for target_os mac, at CHROMIUM_VERSION
./scripts/apply_patches.sh
./scripts/apply_branding.sh     # also builds the app icon (iconutil, actool)
./scripts/build.sh              # build/koan-mac.gn: chromium/src/out/Koan/Koan.app
open chromium/src/out/Koan/Koan.app
```

The app is unsigned and meant for local use. Its profile lives in
`~/Library/Application Support/Koan`. `install_desktop.sh` and the
off-screen test tools (`scripts/e2e/`, `shot_ui.sh`, ...) use Linux's
headless Ozone platform, so test behaviour on Linux.

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
  patches.
- **Every user-visible feature is behind a flag:** a `koan::features`
  `base::Feature` with a `chrome://flags` entry. That lets users and bug
  reports turn it off.

## Code style

- Follow [Chromium's C++ style](https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++.md)
  and the conventions of the surrounding code. Prefer existing Chromium and
  Koan patterns over new abstractions.
- Format C++ with clang-format 22.1.8 and the repository's `.clang-format`:
  `git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file -i`.
- New files start with Koan's license header:

  ```cpp
  // Copyright 2026 The Koan Authors
  // Use of this source code is governed by the MIT license that can be
  // found in the LICENSE file.
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
- the unit tests pass;
- clang-format, shellcheck and `node --check` find no problems.

Run the same checks locally before pushing:

```sh
scripts/ci/check_patches.sh
python3 -m unittest discover -s scripts/tests
git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file --dry-run --Werror
```

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
  `CHROMIUM_VERSION`, refreshes every patch, and re-audits the hooks, as
  [docs/REBASE.md](docs/REBASE.md) describes.

## Licensing

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE). Changes to upstream Chromium files in `patches/`
remain under Chromium's BSD-style license; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
