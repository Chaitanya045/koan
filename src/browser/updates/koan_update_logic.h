// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UPDATES_KOAN_UPDATE_LOGIC_H_
#define KOAN_BROWSER_UPDATES_KOAN_UPDATE_LOGIC_H_

#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "url/gurl.h"

// Koan's updates come from its tagged GitHub releases. Each one carries a
// signed manifest, koan-update.json, written by scripts/ci/update_manifest.py
// (its docstring is the spec this follows). The app reads the latest
// release's manifest and trusts only what verifies against the Ed25519 key
// built into it (koan_update_public_key.txt).
//
// Pure and platform-neutral, so the unit tests need nothing else: the check
// and the notices (koan_updates.cc), and the Mac's download and install
// (#550), share it. Where the manifest comes from and which key it is checked
// against are koan_update_source.h's.
namespace koan::updates {

// The one repository Koan takes updates from, whatever the clone's remote.
inline constexpr char kRepository[] = "koan-browser/koan";

// The latest release's manifest. A release asset, not the GitHub API: no
// rate limit. GitHub redirects it to its asset host. releases/latest skips
// drafts and pre-releases, and release.yml marks only the highest version as
// the latest, so nightlies never show up here.
inline constexpr char kManifestUrl[] =
    "https://github.com/koan-browser/koan/releases/latest/download/"
    "koan-update.json";

// Where a platform's file must be, and the release notes.
inline constexpr char kDownloadUrlPrefix[] =
    "https://github.com/koan-browser/koan/releases/download/";
inline constexpr char kReleasesUrlPrefix[] =
    "https://github.com/koan-browser/koan/releases/";

// update_manifest.py's SCHEMA and MESSAGE_PREFIX.
inline constexpr int kManifestSchema = 1;
inline constexpr char kSignedMessagePrefix[] = "koan-update-v1\n";

// update_manifest.py's PLATFORMS.
inline constexpr char kPlatformMacArm64[] = "mac-arm64";
inline constexpr char kPlatformLinuxX8664[] = "linux-x86_64";

// The manifest is about 1 KB; anything much bigger isn't one.
inline constexpr size_t kMaxManifestSize = 16 * 1024;

// At most one check every 12 hours, the first a minute after the first
// window (not at startup).
inline constexpr base::TimeDelta kCheckInterval = base::Hours(12);
inline constexpr base::TimeDelta kFirstCheckDelay = base::Minutes(1);

// A raw Ed25519 public key.
using PublicKey = std::array<uint8_t, 32>;

// The key from the base64 of its 32 raw bytes (koan_update_public_key.txt's
// format; surrounding whitespace is ignored), or nullopt.
std::optional<PublicKey> ParsePublicKey(std::string_view base64);

// This build's platform in the manifest ("mac-arm64", "linux-x86_64"), or
// nullopt on one that has no releases: then Koan doesn't check.
std::optional<std::string_view> CurrentPlatform();

// Whether `text` is a SemVer 2.0 version ("0.0.3", "0.0.3-alpha.1",
// "1.0.0+build.5").
bool IsValidVersion(std::string_view text);

// SemVer precedence, pre-releases included: 0.0.2-alpha.1 < 0.0.3-alpha.1 <
// 0.0.3 < 0.0.10; build metadata doesn't count. Negative, zero or positive as
// `a` is lower than, the same as or higher than `b`; nullopt if either isn't
// a version.
std::optional<int> CompareVersions(std::string_view a, std::string_view b);

// Whether `candidate` is a higher version than `current` (false if either
// isn't a version).
bool IsNewerVersion(std::string_view candidate, std::string_view current);

// The bytes a platform's entry signs (update_manifest.py's message()):
// "koan-update-v1\n" platform "\n" version "\n" sha256 "\n" size "\n".
std::string SignedMessage(std::string_view platform,
                          std::string_view version,
                          std::string_view sha256,
                          int64_t size);

// Whether `url` is a file of one of Koan's releases
// (https://github.com/koan-browser/koan/releases/download/<tag>/<file>, no
// port, credentials, query or fragment), and a page under
// https://github.com/koan-browser/koan/releases/ (the release notes).
bool IsReleaseDownloadUrl(const GURL& url);
bool IsReleasesPageUrl(const GURL& url);

// A release, from a manifest whose entry for this platform verified.
struct UpdateInfo {
  UpdateInfo();
  UpdateInfo(const UpdateInfo&);
  UpdateInfo& operator=(const UpdateInfo&);
  UpdateInfo(UpdateInfo&&);
  UpdateInfo& operator=(UpdateInfo&&);
  ~UpdateInfo();

  // Signed: the version, and the platform's file by size and SHA-256.
  std::string version;
  std::string platform;
  int64_t size = 0;
  // 64 lowercase hex digits.
  std::string sha256;
  // Not signed, but checked: the file (IsReleaseDownloadUrl()) and the
  // release notes (IsReleasesPageUrl()).
  GURL url;
  GURL notes_url;
  // Not signed, informational: the release's Chromium version (may be
  // empty).
  std::string chromium;
};

// Why a manifest was ignored.
enum class ManifestError {
  // Not a JSON object.
  kNotJson,
  // No "schema", or one other than kManifestSchema.
  kUnknownSchema,
  // A field missing or of the wrong form (version, sizes, hashes, the
  // signature's base64).
  kMalformed,
  // No entry for this platform (Linux, until releases carry a Flatpak).
  kNoPlatform,
  // The file's or the notes' URL is not one of Koan's releases.
  kBadUrl,
  // The signature doesn't verify against the key.
  kBadSignature,
};

// The release `json` describes for `platform`, if it is a schema 1 manifest
// and that platform's entry is signed by `public_key` (Ed25519, BoringSSL),
// and its URLs are Koan's releases. Other platforms' entries aren't looked at.
base::expected<UpdateInfo, ManifestError> ParseManifest(
    std::string_view json,
    std::string_view platform,
    const PublicKey& public_key);

// Whether to show "Update Available!" for `latest`: it is newer than this
// build's `current` version, and than `notified`, the version it was last
// shown for (empty if never). So once per version, and not again for an
// older one if releases/latest ever goes back.
bool ShouldNotify(std::string_view latest,
                  std::string_view current,
                  std::string_view notified);

// How long to wait before the next check, the last one being at `last_check`
// (null if never): kCheckInterval after it, but no sooner than
// kFirstCheckDelay from `now`. A `last_check` in the future (a clock that
// went back) counts as now.
base::TimeDelta NextCheckDelay(base::Time last_check, base::Time now);

// What makes one build differ from another for "Update Complete!": Koan's
// version and Chromium's, e.g. "0.0.3/158.0.8127.18". Zen compares its
// version and build ID the same way (ZenUpdates.mjs).
std::string BuildStamp(std::string_view koan_version,
                       std::string_view chromium_version);

// A version's release page (releases/tag/v<version>), or an empty URL if
// `version` isn't one.
GURL ReleasePageUrl(std::string_view version);

// Where to report what broke.
GURL IssuesUrl();

// On Linux, the command that installs a release's Flatpak once it is in
// `download_dir`: "flatpak install --user --reinstall <dir>/<file>", the
// path quoted for a POSIX shell where it needs it. nullopt if the release's
// file isn't a plainly named .flatpak.
std::optional<std::string> FlatpakInstallCommand(const UpdateInfo& update,
                                                 std::string_view download_dir);

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UPDATES_KOAN_UPDATE_LOGIC_H_
