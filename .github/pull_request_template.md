<!--
Title: a Conventional Commits title, e.g. "fix(urlbar): restore the Cmd+L
card after switching windows" (CONTRIBUTING.md, "Commits").
Labels: a type label and an area label (CONTRIBUTING.md, "Labels").
-->

Fixes #<!-- issue number; use "Part of #…" if the issue isn't fully done -->

## Problem
<!-- What was wrong or missing, and who it affects. -->

## Solution
<!-- What this changes to solve it: what the user sees, and the main code
changes (files and behaviour). For behaviour or UI changes: how Zen does it,
and where Koan differs and why. -->

## How it's confirmed
<!-- The evidence that it works: what you built and ran (Mac steps, e2e and
unit tests, CI runs, with links), screenshots or a short video for UI changes
(Koan, and Zen for comparison), and anything that failed or wasn't tested. -->

## Checklist

- [ ] The title follows Conventional Commits, and the pull request has a type and an area label
- [ ] Builds, and `scripts/e2e/run.sh` passes for the affected and existing tests
- [ ] Upstream changes are minimal hook patches in `patches/`, listed in `docs/REBASE.md`
- [ ] New behaviour is behind a `koan::features` flag
- [ ] UI colours and radii come from colour IDs and shared constants, not hard-coded values
- [ ] `docs/USAGE.md` and `docs/ZEN_PARITY.md` are updated
