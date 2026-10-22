# Releasing Koan

Releases are cut from `develop` by `scripts/release.sh`, on a machine with
the Chromium checkout (CI can't build Chromium).

## Version numbers

`VERSION` holds Koan's semantic version. Bump the minor version for new
features, the patch version for fixes, and note the Chromium version each
release is built on in `CHANGELOG.md`. Moving to a new Chromium milestone is
at least a minor release.

## Steps

1. Merge everything for the release into `develop` (via PRs, CI green).
2. In a release PR: set `VERSION`, move the `[Unreleased]` changelog entries
   under `## [X.Y.Z] - YYYY-MM-DD`, and merge it.
3. On `develop`, in sync with origin:

   ```sh
   scripts/release.sh --dry-run   # build, test and package, no upload
   scripts/release.sh             # the same, then tag and publish
   ```

`release.sh` refuses to run on a dirty or out-of-date tree, an existing tag,
or a missing changelog section. It then:

- checks and applies the hook patches and branding;
- builds `out/Release` incrementally (`KOAN_CONFIG=release`);
- runs `chrome --version` and the end-to-end tests (`scripts/e2e`) against
  the release binary;
- packages `dist/koan-X.Y.Z-linux-x64.tar.xz` + `SHA256SUMS`
  (`scripts/package.sh`);
- tags `vX.Y.Z`, pushes the tag and creates the GitHub release with the
  changelog section as notes and both files attached.

## What a release contains

The tarball unpacks to `koan-X.Y.Z/` with the browser (stripped), its
resources, locales and GPU libraries, `koan` (launcher), `install.sh` /
`uninstall.sh` (per-user install into `~/.local/opt/koan`, `koan` on the
PATH, app-menu entry and icons), `USAGE.md`, `LICENSE` and
`THIRD_PARTY_NOTICES.md`.

It can also run in place: `koan-X.Y.Z/koan`.
