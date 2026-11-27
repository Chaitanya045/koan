# Linux CI and the Flatpak

The Linux build: Koan for x86_64 as a Flatpak bundle, built on GitHub's
free runners, nightly (`.github/workflows/linux.yml`) and for every tagged
release (`release.yml`, "Releases" below). User-facing docs are in
docs/USAGE.md ("Installing the Linux build").

**Status:** Linux x86_64 ships as a Flatpak, nightly and with tagged
releases, built by CI (the owner decided on 2026-11-26, decisions.md). It was built on a fork (#442) and taken
over as #466. There is still no Windows build. Nothing here changes the Linux
development build.

## Shape

The macOS chain's design (macos-ci.md); the workflows are copies, the
incremental-build scripts (`scripts/ci/find_base.sh`, `incremental.sh`) are
shared:

```
fetch ──► build-01 ──► … ──► build-08 ──► check ──► release
flatpak-check ┘                (.flatpak)          (nightly-linux)
```

- `linux.yml`: triggers and the plan job as in `macos.yml` (nightly
  `53 1 * * *`, `workflow_dispatch`, the `linux-build` label on a pull
  request from the same repository), then `linux-chain.yml`, then the
  `nightly-linux` pre-release on develop. A tag of its own, because the
  macOS release job deletes and recreates `nightly` with only the .dmg.
- `linux-chain.yml`: fetch (`scripts/ci/linux_fetch.sh`: bootstrap with
  `target_os = ["linux"]`, `mac_check_deps.py`, prune, patches, branding,
  `.git` removed, `gn gen`, `.cache/ci-toolchain`), up to 8 slices of
  `linux-build.yml`, check, cleanup. `flatpak-check` runs `linux_package.sh` on a fake build in
  parallel with the fetch, so a broken manifest or Flathub outage fails in
  minutes, not after the build.
- `linux-build.yml`: unpack source and the previous `out/Release`,
  `linux_build_slice.sh` (SIGINT to siso 65 min before the 6 h limit), then
  pack and upload out, or (last slice) a headless smoke test and the
  Flatpak.

Build config: `build/koan-release.gn` (`KOAN_CONFIG=release`, set by
`scripts/ci/linux_env.sh`): no component build, no DCHECKs, no symbols,
Chromium's Debian sysroot. Same reasons as the Mac's for no
`is_official_build`: a ThinLTO link wouldn't fit one slice.

## Incremental builds

A full build is ~73k steps (~8 h of build jobs on these runners). A run
builds on the newest successful `develop` run that kept its tree and
finished `out/Release`, so a nightly rebuilds only what its commits changed.

**Who is a base.** Only a *trusted* run (`keep_base` in `linux-chain.yml` and
`linux-build.yml`, set by `linux.yml`'s plan job): one that built `develop`
itself, on a schedule or by hand with no other `ref`, the same condition
that lets the release job publish `nightly-linux`. Only such a run uploads
`koan-linux-out-final.tar.zst` and keeps its source for 14 days, and its
cleanup deletes the source and out-final of older `develop` runs (finished
ones only: a running chain still needs its source). A labelled pull
request's run, or a manual run of another ref, may still *build on* the
base (`scripts/ci/find_base.sh linux.yml koan-linux`, which only picks
successful `schedule` and `workflow_dispatch` runs of `develop` that still
have both archives), but keeps its source 3 days, uploads no finished
build, and its cleanup deletes only its own source and out slices. So
objects built from unmerged code never end up in the published Flatpak, and
a pull request that moves `CHROMIUM_VERSION` can't delete `develop`'s base.
Forks' pull requests are never built.

siso goes by mtime, so reusing out/ alone is useless: a fresh checkout
re-dates every file. `scripts/ci/incremental.sh` instead turns the base
run's source archive into this commit's tree: the checkout's tracked files are
deleted and the base archive unpacked (so the tree is exactly the base's),
git restores what differs from this commit (only those files get a new
mtime; `.gitignore`d trees are kept), and each patch that changed is
reversed (base's version) and applied (this commit's) with `git apply`, as a
full build does (`GIT_CEILING_DIRECTORIES`, since the tree has no `.git`).
It exits 3, and `linux_fetch.sh` wipes the workspace and does the full fetch,
when `CHROMIUM_VERSION`, the branding or any other fetch input
(`KOAN_FULL_BUILD_INPUTS` in `linux_fetch.sh`: bootstrap, the dependency
check, the pruning, the apply scripts and `linux_fetch.sh` itself, which
holds the `.gclient`) differs from the base's, when the runner image
(`KOAN_TOOLCHAIN`, `ID VERSION_ID` from `/etc/os-release`: Chromium's clang
and sysroot are pinned by `CHROMIUM_VERSION`) differs from
`.cache/ci-toolchain` in the base's tree, or when the archive has expired.
If the base's out/Release is gone by the time slice 1 looks for it, the
build is still correct, just full.

Checked locally on a fake tree (2026-11-22, with the earlier Linux-only
scripts, since replaced by the shared ones): new, changed and removed
patches; a changed and a deleted repo file; untouched files keep their
mtimes; branding change exits 3.

## Releases

`release.yml`'s `build-linux` job calls `linux-chain.yml` with
`ref: <the tag's commit>` and `release: true` (bundle
`Koan-<version>-x86_64.flatpak`, no commit), with no `keep_base`, no secrets
and no environment. It runs in parallel with the Mac `build` job. `publish`
(the `release` environment, on macOS) `needs` both, so a failed leg means no
release. It downloads the chain's `flatpak` artifact, checks the name, that
it's not empty and that the chain built the same commit and Chromium as the
Mac one, writes its `.sha256` with `shasum -a 256` (as `mac_package.sh` does
for the .dmg) and uploads both. Not signed; `koan-update.json` stays
Mac-only (Linux in-app updates would be notify-only, later).
`release_info.py notes` fills `@FLATPAK@` and `@FLATPAK_SHA256@` in the
notes, or adds a Linux section when the notes don't have them.

- **Artifact names don't collide** between the two chains of a run:
  `koan-mac-source.tar.zst`, `koan-mac-out-<n>.tar.zst`,
  `koan-mac-out-final.tar.zst`, `koan-mac-logs-<n>` and `Koan-….dmg`, against
  `koan-linux-…` (same shapes) and `Koan-….flatpak`.
- **Base:** `linux-chain.yml` always looks for a base (`find_base.sh linux.yml
  koan-linux`), and has no `incremental` input like `macos-chain.yml` (whose
  releases build from scratch). So a tag's Linux build builds on the newest
  successful develop nightly run that still has its source and finished
  build (kept 14 days), when `CHROMIUM_VERSION`, the branding and the fetch
  inputs match and the runner image is the same; otherwise it's a full
  build. The base is only ever a trusted develop build, and the tag's tree
  is made exactly this commit's (`incremental.sh`), so the result is the
  same as a full build's.
- Nothing here ran against a real tag yet.

## Runner

`ubuntu-24.04`, 4 cores, 16 GB, `JOBS=4`. `linux_runner_setup.sh` deletes
the image's SDKs (Android, .NET, GHC, Swift, JVMs, CodeQL, ...) and docker
images, and frees the root disk from 86 to 126 GB (of 145; run
45054461809, 2026-11-22). That image has no second disk; when a runner has
one at `/mnt`, the setup adds 4 GB of swap there and puts the archives on
it (`KOAN_SCRATCH`, else `RUNNER_TEMP`). Ubuntu's ImageMagick 6 has no `magick`, which `apply_branding.sh`
calls; the setup puts a `magick` that runs `convert` on PATH.

The tool caches (vpython, CIPD) go to `.cache/linux` (`linux_env.sh`) so
they travel in the source archive, as `.cache/mac` does. HOME stays the
runner's: the macOS one-folder rule isn't needed on Linux.

## The Flatpak

`flatpak/dev.koan.Koan.yml` wraps the ready binary, as Flathub's
`com.google.Chrome` does, rather than building Chromium inside
flatpak-builder (Flathub's `org.chromium.Chromium` does that, with its own
sandbox patches, on Flathub's big builders):

- Runtime `org.freedesktop.Platform` 25.08 and base
  `org.chromium.Chromium.BaseApp` 25.08, the versions com.google.Chrome
  158 ships on. 26.08 exists on Flathub; move when Chrome's does.
- The BaseApp provides `cobalt`, the launcher `/app/bin/koan` execs. It
  reads `/app/etc/cobalt.ini`, sets `CHROME_DESKTOP=dev.koan.Koan.desktop`
  and `CHROME_WRAPPER`, adds flags from
  `~/.var/app/dev.koan.Koan/config/koan-flags.conf`, and runs the browser
  through **zypak**, which turns Chromium's sandbox into Flatpak's
  (`flatpak-spawn --sandbox`). zypak is built in the manifest (same tag as
  com.google.Chrome); `chrome-sandbox` is a stub that fails if called.
- finish-args are com.google.Chrome's without its dconf module (GNOME proxy
  settings).
- The profile is `~/.var/app/dev.koan.Koan/config/koan`: the branding
  renames Chromium's folder (seen on a desktop install, 2026-11-24).
  cobalt.ini's ConfigDir says the same.
- App id `dev.koan.Koan`, the Mac bundle id. Flathub would need a domain
  the project controls; a bundle installed from a file doesn't.

`linux_package.sh` stages the files Chromium's Linux installer ships (the
list from #13's `scripts/package.sh`, plus `resources/`), renders icons from
`branding/logo.svg`, and runs Flathub's `org.flatpak.Builder` (Ubuntu
24.04's flatpak-builder failed `appstreamcli compose` on the same files with
`E: file-read-error`, run 45054968812) (`--install-deps-from=flathub`,
`--disable-rofiles-fuse`) and `flatpak build-bundle` with Flathub as the
runtime repo, so `flatpak install ./Koan-….flatpak` fetches the runtime.

## Checked

- 2026-11-22, locally: `linux_package.sh` with `KOAN_CI_OUT` set to an
  upstream Chromium snapshot (Linux_x64 r1751584, `chrome-linux.zip`)
  built a 141 MB bundle; `flatpak install --user` and
  `flatpak run dev.koan.Koan --headless --dump-dom data:...` rendered the
  page. `G_MESSAGES_DEBUG=cobalt` showed `zypak-wrapper.sh
  /app/koan/chrome` with no `--no-sandbox`.
- 2026-11-22/23, CI run 45061138149 (tnfssc/koan, full build, 1c82a0a):
  fetch 16 min (126 GB free at start, 105 GB after gn gen); build 1
  4 h 55 min, 38077/72946 steps; build 2 3 h 13 min, finished (2 build
  jobs, ~8 h 25 min end to end). Headless check on the runner passed.
  Artifacts: source 5.1 GB, out-final 1.3 GB, Flatpak 133 MB.
- Same day, locally: that run's bundle installed with
  `flatpak install --user` and rendered a page headless inside the
  sandbox; `--version` prints `Koan 158.0.8127.18`.
- 2026-11-23, CI run 45109561372 (7128095, a notes-only commit on top of
  that run): fetch took the incremental path (download 5 min, unpack
  1.5 min; rsync touched only the note, 0 patches changed); build 1
  downloaded out-final and finished `[2/3]` steps in under a minute;
  ~25 min end to end. Cleanup then deleted run 45061138149's source and
  out-final, keeping its Flatpak.
- 2026-11-23, CI run 45257523045 (d44b6ee, koan-browser/koan develop
  merged in, on top of run 45109561372): 5 patches reversed and
  re-applied, `src/` changes copied in; build 1 ran 633 steps; ~43 min
  end to end, headless check passed.
- Not yet: the Flatpak on a desktop
  (Wayland and X11, GPU, Agent control's socket under the sandbox's
  config dir).

## Open

- Agent control: the `koan` tool is staged into `/app/koan/` if the build
  makes it, but agents outside the sandbox reach Koan's socket under
  `~/.var/app/dev.koan.Koan/config/koan/AgentControl/` only if they
  know that path. Not looked at yet.
