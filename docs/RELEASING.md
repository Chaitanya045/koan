# Releasing Koan

How Koan versions are named, how often they come out, and how to make one.
The workflow is [`.github/workflows/release.yml`](../.github/workflows/release.yml);
the version rules live in one script,
[`src/version/koan_version.py`](../src/version/koan_version.py), which the
build, the packaging and the release workflow all use.

## Channels

| Channel | What it is | Where |
|---|---|---|
| **Nightly** | An ad hoc signed dev build of `develop`, every night that something changed. Replaced each time. | The rolling [`nightly`](https://github.com/koan-browser/koan/releases/tag/nightly) pre-release (`macos.yml`) |
| **Alpha** | Tagged releases for early testers: `vX.Y.Z-alpha.N`. | [Releases](https://github.com/koan-browser/koan/releases), marked **Latest** |
| Beta, Stable | Later. Alphas update in the app first ([Signing and updates](#signing-and-updates)). | |

Every tagged release, alphas included, is a full GitHub release marked
**Latest**, never a GitHub pre-release. The stage is carried by the title
("Koan 0.0.2 (Alpha)") and by the warning at the top of the notes. Only the
rolling Nightly is a pre-release. While every release was a pre-release,
GitHub showed none in the repository's sidebar and `/releases/latest` was a
404.

## Versions

- **Koan uses SemVer:** `VERSION` at the root of the repository holds
  `MAJOR.MINOR.PATCH`, optionally with a pre-release label `-alpha.N`,
  `-beta.N` or `-rc.N` (`0.0.2-alpha.1`). MINOR, PATCH and N stay below 100.
- **Chromium's version goes in the title:**
  `Koan 0.0.2 (Alpha) · Chromium 158.0.8127.18`. The label is `Alpha`,
  `Beta` or `RC` (`Alpha 2` from `.2` on, so two alphas of one version never
  share a title), and none for a final release.
- **In the app:**
  - About Koan and `chrome://version` show `0.0.2 (Alpha) · Chromium
    158.0.8127.18` after the product name.
  - `Info.plist`: `CFBundleShortVersionString` is `0.0.2` (Apple allows only
    three integers there), `KoanVersion` is the whole `0.0.2-alpha.1`.
  - `CFBundleVersion` is `<Chromium BUILD>.<Chromium PATCH>.<Koan counter>`,
    e.g. `8127.18.2101`. The counter is
    `((MAJOR × 100 + MINOR) × 100 + PATCH) × 1000 + stage × 100 + N`, with
    stage 1 for alpha, 2 beta, 3 rc, and 900 for a final release, so it grows
    with SemVer. The whole value only goes up, which the in-app updater
    relies on, as long as no release moves to an older Chromium than the one
    before it: the release workflow checks that against the previous release
    tag. It is also above 0.0.1's `8127.18` (Chromium's own), so
    LaunchServices prefers the newer copy of `Koan.app` when an old one is
    still around.
  - The User-Agent, client hints, the component updater and anything else a
    site or server sees keep Chromium's version.
- **Nightlies** carry the same `VERSION` as `develop` (the next release's),
  and their disk image names add the commit:
  `Koan-0.0.2-alpha.1-1a2b3c4-arm64.dmg`. Releases are
  `Koan-0.0.2-alpha.1-arm64.dmg`.

## Cadence

- **Koan alphas:** when they're ready, roughly every two weeks.
- **Chromium:** a new major about every 8 weeks, following Chrome's
  Extended Stable line, plus its security point releases within days
  ([REBASE.md](REBASE.md)). A Chromium point release on its own is a Koan
  patch release (`0.0.3` → `0.0.4`).

## Making a release

1. **Bump `VERSION`** on `develop` in a pull request, e.g. `0.0.2-alpha.1`.
   After the release, bump it to the next version, so nightlies never claim
   to be a release that's out.
2. **Write `docs/releases/<version>.md`.** Copy the sections of the last one
   (Download/Installing, What's new, Known issues, Build), or start from the
   skeleton the previous release left. `@DMG@` and `@SHA256@` are filled in
   by the workflow. Remove every `TODO(release)`: the workflow refuses to
   publish while one is left. House rules: say what Koan does and how; **no
   comparisons with other browsers and no benchmark tables against
   anything**, Koan's older builds included. Add the version's line to
   [CHANGELOG.md](../CHANGELOG.md).
3. **Merge** that pull request. Label the other pull requests of the release
   (below), since GitHub's generated list of them goes under the notes.
4. **Tag `develop` and push the tag:**
   ```sh
   git fetch origin && git tag v0.0.2-alpha.1 origin/develop
   git push origin v0.0.2-alpha.1
   ```
   or tag it on GitHub and run the workflow by hand:
   `gh workflow run release.yml -f tag=v0.0.2-alpha.1` (`-f draft=true`
   keeps the release as a draft to look at first).
5. **The workflow** checks the tag, `VERSION` and the notes in a few minutes
   (a mismatch fails there, not a day later), then builds (the nightly's
   chain, `macos-chain.yml`; an hour or two, or about 16 hours when it is
   the first build after a full-build input changed: see
   [How releases are built](#how-releases-are-built)), then publishes the release: the
   title, the notes followed by GitHub's generated notes since the previous
   release tag, the `.dmg`, its `.sha256` and `koan-update.json` (see
   [Signing and updates](#signing-and-updates)). Publishing waits for the
   owner's approval in the `release` environment (the run's page shows
   **Review deployments**); the environment only accepts `v*` tags and
   `develop`.
6. **Check the release:** download the `.dmg`, compare its SHA-256, install
   it, and look at About Koan (`0.0.2 (Alpha) · Chromium …`). Then bump
   `VERSION` for the next one (step 1).

To redo a release, delete it (and any leftover draft of it) on GitHub first;
the workflow refuses a tag that already has one. Never move a published tag.

### How releases are built

A full build happens once per full-build input change: a new
`CHROMIUM_VERSION`, a change to the fetch scripts (`KOAN_FULL_BUILD_INPUTS`
in `scripts/ci/mac_fetch.sh` and `linux_fetch.sh`), or a new Xcode or runner
image (`scripts/ci/incremental.sh`). The first trusted build after it is
full, whichever kind it is, a nightly or a release. Every later trusted
build is incremental: it starts from the newest trusted build's tree and
`out/` directory (`scripts/ci/find_base.sh`) and rebuilds only what changed
since, then becomes the base for the next one.

- **Trusted builds** are the develop nightlies (`macos.yml`, `linux.yml` on
  `develop`, scheduled or run by hand without another `ref`) and releases
  (`release.yml` for a `v*` tag, pushed or run by hand). Only they keep
  their tree and finished build (`keep_base`, 14 days), and only they are
  picked as a base. A release counts only when the tag points at the commit
  the run started from, or when it was run from `develop`.
- **Pull-request builds** (and manual runs of other refs) build on a base
  but never become one: unmerged code is never linked into what users
  install.
- A release's tree and `out/` are a nightly's: the same gn args
  (`build/koan-mac.gn`), the update test hooks off in both (checked by
  `scripts/ci/check_update_test_hooks.sh`), the version from `VERSION`.
  What makes it a release, the `.dmg`'s name (`KOAN_RELEASE`) and the
  signing in the publish job, happens after the build is packed as a base.
  So a nightly built on a release (or the other way round) carries nothing
  over that a nightly of the same commit wouldn't have.
- The run's summary says which run it built on ("Incremental: building on
  run N") or "Full build".

### Labels for the generated notes

[`.github/release.yml`](../.github/release.yml) sorts the pull requests by
their first matching label: Features (`enhancement`, `feature`), Fixes
(`bug`, `user-test`), Agents (`agents`), Performance (`performance`), UI
(`ui`, `zen-parity`, `accessibility`), Build and CI (`build`, `ci`,
`release`, `macos`, `chromium-update`), Docs (`docs`, `documentation`), then
everything else. `skip-changelog`, `duplicate`, `invalid`, `wontfix`, `repo`
and `tracker` are left out.

## Signing and updates

**The app's signature.** Releases (a tag, through `release.yml`) are signed
with one fixed, self-signed certificate, "Koan Release Signing" (SHA-1
`41C53B537255BA68D030856528BDACCD559E0B50`, valid until 2056). macOS ties the
"Koan Safe Storage" keychain item, which holds the key to cookies and
passwords, to the signing identity. A fixed identity means an update doesn't
bring the keychain prompt back. It is not a Developer ID certificate and the
app isn't notarised, so Gatekeeper still warns on the first install.
Nightlies stay ad hoc signed.

**The update manifest.** Each release carries `koan-update.json`, made by
[`scripts/ci/update_manifest.py`](../scripts/ci/update_manifest.py). The app
reads it from the fixed address
`https://github.com/koan-browser/koan/releases/latest/download/koan-update.json`
and believes it only if the signature verifies with the public key built
into the app
([`src/browser/updates/koan_update_public_key.txt`](../src/browser/updates/koan_update_public_key.txt),
a raw Ed25519 key in base64): it then says "Update Available!" (#549), and
on the Mac it downloads the disk image it names and installs it on the next
restart (#550, docs/USAGE.md "Updates").
Only a test build (CI makes one for a pull request, never for a release or a
nightly: `koan_update_test_hooks`, `src/browser/updates/BUILD.gn`) can be
pointed at another manifest and key; a release or nightly build that has the
hooks fails before it is packaged (`scripts/ci/check_update_test_hooks.sh`).

```json
{
  "chromium": "158.0.8127.18",
  "notes_url": "https://github.com/koan-browser/koan/releases/tag/v0.0.3-alpha.1",
  "platforms": {
    "mac-arm64": {
      "sha256": "<64 hex digits>",
      "signature": "<base64, 64 bytes>",
      "size": 123456789,
      "url": "https://github.com/koan-browser/koan/releases/download/v0.0.3-alpha.1/Koan-0.0.3-alpha.1-arm64.dmg"
    }
  },
  "schema": 1,
  "version": "0.0.3-alpha.1"
}
```

The signature covers the UTF-8 bytes of
`"koan-update-v1\n" + platform + "\n" + version + "\n" + sha256 + "\n" + size + "\n"`
(`update_manifest.py message` prints them). The version is part of it, so an
older, genuinely signed disk image can't be served as a newer version.
`update_manifest.py verify` checks a manifest against a file and a public key.

- `latest/download` finds the newest release that is not a pre-release and
  not a draft. Alphas must therefore stay full releases (see
  [Channels](#channels)), or updates stop finding them.
- Publishing a release replaces what `latest` points at, so a bad release is
  fixed by publishing a newer one (or by deleting it, which makes `latest`
  point at the one before).

**What updates rely on.** A Mac that has Koan installs the next release
itself, so every release must keep these the same, or installed copies stop
updating (they then offer the release page, and people install by hand):

- **The signing certificate.** A staged copy is accepted only with
  `codesign --verify --deep --strict -R '=certificate leaf = H"<SHA-1>"'` for
  `scripts/ci/codesign_identity.txt` (`kReleaseCertificateSha1` in
  `src/browser/updates/koan_update_install_logic.h`). A new certificate needs
  one release that still carries the old one and pins both.
- **The bundle ID** (`dev.koan.Koan`): the copy's `CFBundleIdentifier` must
  be the running app's.
- **`KoanVersion` in the app's Info.plist**: it must be the manifest's signed
  version, and newer than the running one (`mac_package.sh` checks it
  against `VERSION`).
- **The asset:** one disk image per release, named
  `Koan-<version>-arm64.dmg` (a plain name ending in `.dmg`), with the app at
  the image's root, listed as `mac-arm64` in `koan-update.json` under
  `releases/download/<tag>/`.
- **The helper** `Contents/Resources/bin/koan-update-helper`, signed with the
  same certificate (`mac_package.sh`), and the install marker's format
  (`install.json`, schema 1): the running (old) version's helper installs the
  new one. It checks the copy's signature against the same pinned
  certificate again right before the swap.
- **`Koan --version` exits 0.** After the swap the helper runs
  `Contents/MacOS/<CFBundleExecutable> --version` and puts the old app back
  if it fails.
- **The manifest's address and key** (above).

`scripts/tests/test_update_install.py` checks that the code, the bundle and
the packaging agree on these.

**The secrets** live in the `release` environment of the repository:

| Secret | What |
|---|---|
| `KOAN_CODESIGN_P12` | The "Koan Release Signing" certificate and its private key, as a base64 `.p12` |
| `KOAN_CODESIGN_P12_PASSWORD` | The password of that `.p12` |
| `KOAN_UPDATE_SIGNING_KEY` | The Ed25519 private key (PEM) that signs `koan-update.json` |

The maintainer keeps an offline backup of all three. The app only trusts the
public key it shipped with, so **losing `KOAN_UPDATE_SIGNING_KEY` means every
user has to reinstall by hand**: never rotate it casually. Never commit a
private key or a `.p12`, or paste one into a log, an issue or a pull request.

## What "Alpha" promises

- Settings and profile formats may change between alphas, without a
  migration.
- Chromium's security fixes are taken on a best-effort basis, not on
  Chrome's schedule.
- Updates: until the in-app updater ships, testers install new versions by
  hand. It will only ever install releases signed with Koan's update key
  (see [Signing and updates](#signing-and-updates)).
- The known issues are listed in every release's notes.

## Branches

- Releases are tagged on `develop`, which is kept releasable.
- Cut `release/<chromium-major>` (e.g. `release/158`) from the last release
  tag only when an older release needs a fix after `develop` has moved on
  (usually to a new Chromium major). Tag the fix there; it gets a patch
  version above that release and below `develop`'s.

## Official builds

The official configuration (`scripts/mac/build_official.sh`,
`build/koan-mac-official.gn`: `is_official_build`, ThinLTO, PGO) is about 38%
faster on Speedometer than the dev configuration, and 0.0.1 shipped with it.
It **doesn't fit GitHub's hosted macOS runners**, so release.yml builds with
the nightly's configuration (`build/koan-mac.gn`). Why, measured against the
`xcode-27` runner (3 M1 vCPUs, 7 GB RAM, 6 h per job, ~101 GB of disk after
`mac_runner_setup.sh`; [.agent/notes/macos-ci.md](../.agent/notes/macos-ci.md)):

| | Fits? | Why |
|---|---|---|
| Compile time | yes | A full dev build takes 3 chained jobs (~16 h); most releases build incrementally. The official build is ~86k steps (the M4 Air with 16 GB did 55k of them in 3 h 56 min at `-j 8`), each heavier (no PCH, -O2, bitcode): about 18-24 h, 4-6 slices of the 16 |
| PGO profiles | yes | A DEPS hook with `checkout_pgo_profiles` fetches Chromium's mac-arm profile (~310 MB) and V8's builtins profiles from public Google Cloud Storage; `mac_fetch.sh` would only set the variable |
| Disk | probably | Source 18 GB + bitcode objects 25-35 GB + ThinLTO cache + archives + swap: 70-80 GB of the ~101 GB |
| **The framework's ThinLTO link** | **no** | One build step that does all optimisation and code generation of the framework. `build_official.sh` refuses less than 12 GB of RAM (estimated peak 6-9 GB with 4 threads); the runner has 7 GB in all, so it would swap throughout. On 3 vCPUs it would take an estimated 3-7 h before swapping, longer than a ~5 h slice. A step can't be split across jobs; only lld's ThinLTO cache could carry finished modules over, if `out/KoanOfficial/thinlto-cache` went into the archive, and every resumed link would redo the serial thin-link phase. Untested |
| Hand-over between jobs | slowly | The out dir of bitcode is 25-35 GB against ≤ 1 GB for the dev build: at the ~12 MB/s measured for artifact downloads, 30-50 min of each 6 h job |

ungoogled-chromium-macos's official build ran ~3 days on these runners
without finishing, then moved to bigger third-party runners (~9 h).

**What would make it work**, in order of preference:

1. A **larger GitHub runner**: macOS XLarge (M2 Pro, 5 vCPUs, 14 GB RAM) passes
   the 12 GB check. It is billed per minute even for public repositories
   (about $0.10-0.16 a minute: roughly $60-130 a release for 10-14 h), and the
   repository's owner has to enable larger runners.
2. A **self-hosted Apple Silicon runner** with 16 GB or more (the M4 Air
   builds it in about 4 h), used only by release.yml (tag pushes and manual
   runs, never pull requests: this is a public repository).
3. Bigger third-party Mac runners (Depot, Cirrus, and the like).

The changes then: an input `config: official` on `macos-chain.yml` and
`macos-build.yml` (and the runner label as an input); `mac_fetch.sh` sets
`checkout_pgo_profiles` in `.gclient`; `mac_build_slice.sh` builds
`out/KoanOfficial` with the args `build_official.sh` writes
(`build/koan-mac-official.gn`, `chrome_pgo_phase`, `koan_thin_lto_jobs`,
the Metal toolchain); `mac_archive.sh pack-out` packs `out/KoanOfficial`
with its ThinLTO cache; `mac_package.sh` takes the app from there; a longer
margin before the deadline for the bigger archive.

## Future work

- **Developer ID signing and notarisation:** no more "Apple could not
  verify" dialog on the first install. Needs an Apple Developer account; the
  signing identity and notarisation credentials go into the release
  workflow's secrets. Changing the signing identity brings the keychain
  prompt back once, so it would come with an update.
- **Beta and Stable update feeds,** next to the Alpha one. Nightlies stay
  outside updates.
