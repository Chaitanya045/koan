Fixes #<!-- issue number; use "Part of #…" if the issue isn't fully done -->

## What changed
<!-- What the user sees, and the main code changes. -->

## How Zen does it
<!-- For behaviour or UI changes: what Zen does, and where Koan differs and why. -->

## Testing
<!-- What you built and ran, which e2e tests, and on which desktop (e.g. GNOME 48, Wayland). -->

## Screenshots
<!-- For UI changes: Koan, and Zen for comparison. -->

## Checklist

- [ ] Builds, and `scripts/e2e/run.sh` passes for the affected and existing tests
- [ ] Upstream changes are minimal hook patches in `patches/`, listed in `docs/REBASE.md`
- [ ] New behaviour is behind a `koan::features` flag
- [ ] UI colours and radii come from colour IDs and shared constants, not hard-coded values
- [ ] `docs/USAGE.md` and `docs/ZEN_PARITY.md` are updated
