#!/usr/bin/env bash
# Publishes a Koan release: verifies, builds, tests, packages, tags and
# uploads to GitHub.
#
#   scripts/release.sh            release the version in VERSION
#   scripts/release.sh --dry-run  everything except tagging and uploading
#
# Requirements: clean `develop` in sync with origin, a `## [<VERSION>]`
# section in CHANGELOG.md, and gh logged in. See docs/RELEASING.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
DRY_RUN=false
[[ ${1:-} == --dry-run ]] && DRY_RUN=true

VERSION="$(cat VERSION)"
TAG="v$VERSION"
REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"

step() { printf '\n== %s\n' "$*"; }

step "Checking the tree"
[[ "$(git rev-parse --abbrev-ref HEAD)" == develop ]] ||
  { echo "release from develop" >&2; exit 1; }
[[ -z "$(git status --porcelain)" ]] || { echo "tree not clean" >&2; exit 1; }
git fetch -q origin develop
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/develop)" ]] ||
  { echo "develop is not in sync with origin" >&2; exit 1; }
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  echo "$TAG already exists" >&2
  exit 1
fi
notes="$(awk -v v="$VERSION" '
  $0 ~ "^## \\[" v "\\]" {on=1; next}
  on && /^## \[/ {exit}
  on {print}' CHANGELOG.md)"
[[ -n "${notes//[[:space:]]/}" ]] ||
  { echo "CHANGELOG.md has no section for $VERSION" >&2; exit 1; }

step "Applying patches and branding"
scripts/apply_patches.sh check
scripts/apply_patches.sh apply
scripts/apply_branding.sh

step "Building the release (incremental)"
KOAN_CONFIG=release scripts/build.sh chrome

step "Testing the release build"
"$ROOT/chromium/src/out/Release/chrome" --version
for t in shortcuts glance; do
  KOAN_OUT=out/Release scripts/e2e/run.sh "$t"
done

step "Packaging"
rm -rf dist
scripts/package.sh

if $DRY_RUN; then
  step "Dry run: not tagging or uploading. Release notes:"
  echo "$notes"
  exit 0
fi

step "Tagging $TAG and publishing"
git tag -a "$TAG" -m "Koan $VERSION"
git push origin "$TAG"
chromium="$(head -1 CHROMIUM_VERSION)"
gh release create "$TAG" --repo "$REPO" --title "Koan $VERSION" \
  --notes "$notes

---
Built on Chromium $chromium. Linux x86-64.

Install: download the tarball, check it against SHA256SUMS, then
\`tar xf koan-$VERSION-linux-x64.tar.xz && koan-$VERSION/install.sh\`." \
  dist/koan-"$VERSION"-linux-x64.tar.xz dist/SHA256SUMS
echo "Released $TAG: https://github.com/$REPO/releases/tag/$TAG"
