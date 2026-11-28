// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_logic.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "build/build_config.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace koan::updates {

namespace {

constexpr size_t kSignatureSize = 64;
constexpr size_t kSha256HexSize = 64;
// The largest integer a JSON number (a double) holds exactly.
constexpr double kMaxExactDouble = 9007199254740992.0;  // 2^53

bool IsDigits(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, base::IsAsciiDigit<char>);
}

// A SemVer numeric part: digits, no leading zero (but "0").
bool IsNumericIdentifier(std::string_view text) {
  return IsDigits(text) && (text.size() == 1 || text[0] != '0');
}

// [0-9A-Za-z-]+
bool IsIdentifier(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '-';
  });
}

// Two numeric identifiers (no leading zeros, so any length): by length,
// then digit by digit.
int CompareNumbers(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return a.size() < b.size() ? -1 : 1;
  }
  return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
}

struct SemVer {
  std::string_view major;
  std::string_view minor;
  std::string_view patch;
  std::vector<std::string_view> pre_release;
};

// Views into `text`.
std::optional<SemVer> ParseSemVer(std::string_view text) {
  // Build metadata: identifiers after '+', ignored for precedence.
  if (const size_t plus = text.find('+'); plus != std::string_view::npos) {
    for (std::string_view part :
         base::SplitStringPiece(text.substr(plus + 1), ".",
                                base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
      if (!IsIdentifier(part)) {
        return std::nullopt;
      }
    }
    text = text.substr(0, plus);
  }
  SemVer version;
  if (const size_t dash = text.find('-'); dash != std::string_view::npos) {
    version.pre_release =
        base::SplitStringPiece(text.substr(dash + 1), ".",
                               base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    for (std::string_view part : version.pre_release) {
      if (!IsIdentifier(part) ||
          (IsDigits(part) && !IsNumericIdentifier(part))) {
        return std::nullopt;
      }
    }
    text = text.substr(0, dash);
  }
  const std::vector<std::string_view> core = base::SplitStringPiece(
      text, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (core.size() != 3 || !std::ranges::all_of(core, IsNumericIdentifier)) {
    return std::nullopt;
  }
  version.major = core[0];
  version.minor = core[1];
  version.patch = core[2];
  return version;
}

int ComparePreRelease(const std::vector<std::string_view>& a,
                      const std::vector<std::string_view>& b) {
  // A release is higher than any of its pre-releases.
  if (a.empty() || b.empty()) {
    return a.empty() == b.empty() ? 0 : (a.empty() ? 1 : -1);
  }
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    const bool a_numeric = IsDigits(a[i]);
    const bool b_numeric = IsDigits(b[i]);
    int result;
    if (a_numeric && b_numeric) {
      result = CompareNumbers(a[i], b[i]);
    } else if (a_numeric != b_numeric) {
      // Numeric identifiers are lower than alphanumeric ones.
      result = a_numeric ? -1 : 1;
    } else {
      result = a[i].compare(b[i]) < 0 ? -1 : (a[i] == b[i] ? 0 : 1);
    }
    if (result) {
      return result;
    }
  }
  // More identifiers, all else equal: higher.
  return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool IsSha256Hex(std::string_view text) {
  return text.size() == kSha256HexSize && std::ranges::all_of(text, [](char c) {
           return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'f');
         });
}

// A non-negative integer, as update_manifest.py writes a size. A JSON number
// past int's range is read as a double: one that is a whole number is fine.
std::optional<int64_t> ReadSize(const base::Value* value) {
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int()) {
    return value->GetInt() < 0 ? std::nullopt
                               : std::optional<int64_t>(value->GetInt());
  }
  if (value->is_double()) {
    const double size = value->GetDouble();
    if (size >= 0 && size <= kMaxExactDouble && std::floor(size) == size) {
      return static_cast<int64_t>(size);
    }
  }
  return std::nullopt;
}

// Whether `url` is https://github.com/... and its spec starts with
// `prefix`, with more after it, and nothing that could make a GitHub page
// something else: a port, credentials, a query or a fragment. GURL has
// already resolved any "." or ".." segments.
bool IsUnder(const GURL& url, std::string_view prefix) {
  return url.is_valid() && url.SchemeIs(url::kHttpsScheme) && !url.has_port() &&
         !url.has_username() && !url.has_password() && !url.has_query() &&
         !url.has_ref() && url.spec().size() > prefix.size() &&
         base::StartsWith(url.spec(), prefix);
}

// A shell word: as is if it is plain, else in single quotes.
std::string ShellQuote(std::string_view text) {
  const bool plain = !text.empty() && std::ranges::all_of(text, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '/' || c == '.' || c == '_' ||
           c == '-' || c == '+' || c == ',' || c == ':' || c == '@';
  });
  if (plain) {
    return std::string(text);
  }
  std::string quoted = "'";
  for (char c : text) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  return quoted;
}

}  // namespace

UpdateInfo::UpdateInfo() = default;
UpdateInfo::UpdateInfo(const UpdateInfo&) = default;
UpdateInfo& UpdateInfo::operator=(const UpdateInfo&) = default;
UpdateInfo::UpdateInfo(UpdateInfo&&) = default;
UpdateInfo& UpdateInfo::operator=(UpdateInfo&&) = default;
UpdateInfo::~UpdateInfo() = default;

std::optional<PublicKey> ParsePublicKey(std::string_view base64) {
  std::optional<std::vector<uint8_t>> raw =
      base::Base64Decode(base::TrimWhitespaceASCII(base64, base::TRIM_ALL));
  if (!raw || raw->size() != std::tuple_size_v<PublicKey>) {
    return std::nullopt;
  }
  PublicKey key;
  std::ranges::copy(*raw, key.begin());
  return key;
}

std::optional<std::string_view> CurrentPlatform() {
#if BUILDFLAG(IS_MAC) && defined(ARCH_CPU_ARM64)
  return kPlatformMacArm64;
#elif BUILDFLAG(IS_LINUX) && defined(ARCH_CPU_X86_64)
  return kPlatformLinuxX8664;
#else
  return std::nullopt;
#endif
}

bool IsValidVersion(std::string_view text) {
  return ParseSemVer(text).has_value();
}

std::optional<int> CompareVersions(std::string_view a, std::string_view b) {
  const std::optional<SemVer> left = ParseSemVer(a);
  const std::optional<SemVer> right = ParseSemVer(b);
  if (!left || !right) {
    return std::nullopt;
  }
  for (const auto part : {&SemVer::major, &SemVer::minor, &SemVer::patch}) {
    if (const int result = CompareNumbers((*left).*part, (*right).*part)) {
      return result;
    }
  }
  return ComparePreRelease(left->pre_release, right->pre_release);
}

bool IsNewerVersion(std::string_view candidate, std::string_view current) {
  const std::optional<int> result = CompareVersions(candidate, current);
  return result && *result > 0;
}

std::string SignedMessage(std::string_view platform,
                          std::string_view version,
                          std::string_view sha256,
                          int64_t size) {
  return base::StrCat({kSignedMessagePrefix, platform, "\n", version, "\n",
                       sha256, "\n", base::NumberToString(size), "\n"});
}

bool IsReleaseDownloadUrl(const GURL& url) {
  if (!IsUnder(url, kDownloadUrlPrefix)) {
    return false;
  }
  // <tag>/<file>: both there, nothing empty.
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      std::string_view(url.spec()).substr(sizeof(kDownloadUrlPrefix) - 1), "/",
      base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  return parts.size() == 2 && !parts[0].empty() && !parts[1].empty();
}

bool IsReleasesPageUrl(const GURL& url) {
  return IsUnder(url, kReleasesUrlPrefix);
}

base::expected<UpdateInfo, ManifestError> ParseManifest(
    std::string_view json,
    std::string_view platform,
    const PublicKey& public_key) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return base::unexpected(ManifestError::kNotJson);
  }
  const base::DictValue& manifest = value->GetDict();
  if (manifest.FindInt("schema") != kManifestSchema) {
    return base::unexpected(ManifestError::kUnknownSchema);
  }
  const std::string* version = manifest.FindString("version");
  const std::string* notes_url = manifest.FindString("notes_url");
  const base::DictValue* platforms = manifest.FindDict("platforms");
  const base::Value* chromium = manifest.Find("chromium");
  if (!version || !IsValidVersion(*version) || !notes_url || !platforms ||
      (chromium && !chromium->is_string())) {
    return base::unexpected(ManifestError::kMalformed);
  }
  const base::Value* entry_value = platforms->Find(platform);
  if (!entry_value) {
    return base::unexpected(ManifestError::kNoPlatform);
  }
  if (!entry_value->is_dict()) {
    return base::unexpected(ManifestError::kMalformed);
  }
  const base::DictValue& entry = entry_value->GetDict();
  const std::string* url = entry.FindString("url");
  const std::string* sha256 = entry.FindString("sha256");
  const std::string* signature_base64 = entry.FindString("signature");
  const std::optional<int64_t> size = ReadSize(entry.Find("size"));
  if (!url || !sha256 || !IsSha256Hex(*sha256) || !size || !signature_base64) {
    return base::unexpected(ManifestError::kMalformed);
  }
  std::optional<std::vector<uint8_t>> signature =
      base::Base64Decode(*signature_base64);
  if (!signature || signature->size() != kSignatureSize) {
    return base::unexpected(ManifestError::kMalformed);
  }
  UpdateInfo update;
  update.url = GURL(*url);
  update.notes_url = GURL(*notes_url);
  if (!IsReleaseDownloadUrl(update.url) ||
      !IsReleasesPageUrl(update.notes_url)) {
    return base::unexpected(ManifestError::kBadUrl);
  }
  const std::string message = SignedMessage(platform, *version, *sha256, *size);
  if (ED25519_verify(reinterpret_cast<const uint8_t*>(message.data()),
                     message.size(), signature->data(),
                     public_key.data()) != 1) {
    return base::unexpected(ManifestError::kBadSignature);
  }
  update.version = *version;
  update.platform = std::string(platform);
  update.size = *size;
  update.sha256 = *sha256;
  if (chromium) {
    update.chromium = chromium->GetString();
  }
  return update;
}

bool ShouldNotify(std::string_view latest,
                  std::string_view current,
                  std::string_view notified) {
  return IsNewerVersion(latest, current) &&
         (!IsValidVersion(notified) || IsNewerVersion(latest, notified));
}

base::TimeDelta NextCheckDelay(base::Time last_check, base::Time now) {
  if (last_check.is_null() || last_check > now) {
    return last_check.is_null() ? kFirstCheckDelay : kCheckInterval;
  }
  return std::max(kFirstCheckDelay, last_check + kCheckInterval - now);
}

std::string BuildStamp(std::string_view koan_version,
                       std::string_view chromium_version) {
  return base::StrCat({koan_version, "/", chromium_version});
}

GURL ReleasePageUrl(std::string_view version) {
  if (!IsValidVersion(version)) {
    return GURL();
  }
  return GURL(base::StrCat({kReleasesUrlPrefix, "tag/v", version}));
}

GURL IssuesUrl() {
  return GURL(base::StrCat({"https://github.com/", kRepository, "/issues"}));
}

std::optional<std::string> FlatpakInstallCommand(
    const UpdateInfo& update,
    std::string_view download_dir) {
  const std::string file = update.url.ExtractFileName();
  const bool plain = std::ranges::all_of(file, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '.' || c == '_' || c == '-';
  });
  if (!plain || file.size() <= sizeof(".flatpak") - 1 ||
      !base::EndsWith(file, ".flatpak") || download_dir.empty()) {
    return std::nullopt;
  }
  std::string_view dir = download_dir;
  while (dir.size() > 1 && dir.back() == '/') {
    dir.remove_suffix(1);
  }
  const std::string path =
      dir == "/" ? base::StrCat({"/", file}) : base::StrCat({dir, "/", file});
  return base::StrCat(
      {"flatpak install --user --reinstall ", ShellQuote(path)});
}

}  // namespace koan::updates
