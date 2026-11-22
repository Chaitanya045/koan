# Linux CI and the Flatpak

The nightly Linux build in `.github/workflows/linux.yml`: Koan for x86_64
as a Flatpak bundle, built on GitHub's free runners. User-facing docs are in
docs/USAGE.md ("Installing the Linux build").

**Status: proposal.** decisions.md still says "macOS only, no Linux
packaging" (#5 not planned, #13 closed). This was built on a fork to show a
working Linux release before asking the maintainer to take it. Nothing here
changes the macOS chain or the Linux development build.

## Shape

The macOS chain's design (macos-ci.md), copied rather than shared, so the
macOS files stay untouched:

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
  `.git` removed, `gn gen`), up to 8 slices of `linux-build.yml`, check,
  cleanup. `flatpak-check` runs `linux_package.sh` on a fake build in
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
- The profile is `~/.var/app/dev.koan.Koan/config/chromium` (unbranded
  Chromium's folder name on Linux; Koan doesn't rename it).
- App id `dev.koan.Koan`, the Mac bundle id. Flathub would need a domain
  the project controls; a bundle installed from a file doesn't.

`linux_package.sh` stages the files Chromium's Linux installer ships (the
list from #13's `scripts/package.sh`, plus `resources/`), renders icons from
`branding/logo.svg`, and runs flatpak-builder (`--install-deps-from=flathub`,
`--disable-rofiles-fuse`) and `flatpak build-bundle` with Flathub as the
runtime repo, so `flatpak install ./Koan-….flatpak` fetches the runtime.

## Checked

- 2026-11-22, locally: `linux_package.sh` with `KOAN_CI_OUT` set to an
  upstream Chromium snapshot (Linux_x64 r1751584, `chrome-linux.zip`)
  built a 141 MB bundle; `flatpak install --user` and
  `flatpak run dev.koan.Koan --headless --dump-dom data:...` rendered the
  page. `G_MESSAGES_DEBUG=cobalt` showed `zypak-wrapper.sh
  /app/koan/chrome` with no `--no-sandbox`.
- Not yet: a full CI run of the chain, and a real Koan build in the
  Flatpak on a desktop (Wayland and X11, GPU, Agent control's socket under
  the sandbox's config dir).

## Open

- Agent control: the `koan` tool is staged into `/app/koan/` if the build
  makes it, but agents outside the sandbox reach Koan's socket under
  `~/.var/app/dev.koan.Koan/config/chromium/AgentControl/` only if they
  know that path. Not looked at yet.
- Tagged releases (`release.yml`) build only the Mac; a Linux leg would
  call `linux-chain.yml` with `release: true`.
