# Contributing to Koan

## Branches and releases

- `develop` is the default branch and is always releasable. It is protected:
  changes land through pull requests, and CI must pass.
- Work on a branch named after the change (`feat/…`, `fix/…`, `build/…`,
  `ci/…`, `docs/…`), open a PR against `develop` that closes an issue, and
  squash-merge it.
- Releases are tagged from `develop` (`vX.Y.Z`, matching `VERSION`) and
  published with `scripts/release.sh`; see `docs/RELEASING.md`.

## Where code goes

- Koan code lives in `src/` (GN `//koan`). Keep upstream Chromium changes to
  small hook patches that call into `//koan`: edit `chromium/src`, then
  `scripts/apply_patches.sh export` to refresh `patches/`.
- Never change global compile flags or include paths (that forces a full,
  10–15 hour rebuild); see `docs/phase1-design.md` §0.
- Product name and logo changes go through `scripts/apply_branding.sh`, not
  patches.
- Every user-visible feature sits behind a `koan::features` flag with a
  `chrome://flags` entry.

## Checks

CI (`.github/workflows/ci.yml`) runs on every PR:

- every patch applies to the Chromium revision in `CHROMIUM_VERSION`;
- `python -m unittest discover -s scripts/tests`;
- clang-format (Chromium style, 22.1.8), shellcheck, `node --check`.

Run them locally before pushing:

```sh
scripts/ci/check_patches.sh
python3 -m unittest discover -s scripts/tests
git ls-files 'src/*.cc' 'src/*.h' | xargs clang-format --style=file --dry-run --Werror
```

Browser behaviour is tested off-screen against a real build (see
`.agent/notes/build.md`): `scripts/shot_ui.sh` for screenshots,
`scripts/ui_session.sh` + `ui_tree.mjs` + `ui_input.mjs` for native UI,
`scripts/run_profile.sh` + `cdp_key.mjs` / `cdp_click.mjs` for real input.
Describe what you verified in the PR.

## Code style

Chromium's C++ style. Match the surrounding code, keep comments for the
non-obvious, and use the MIT header from existing files.
