// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_install_logic.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace koan::updates {

namespace {

constexpr size_t kSha256Size = 32;

// Retries back off from 10 minutes, to at most 6 hours.
constexpr base::TimeDelta kFirstRetryDelay = base::Minutes(10);
constexpr base::TimeDelta kMaxRetryDelay = base::Hours(6);

constexpr auto kMarkerStates =
    std::to_array<std::pair<MarkerState, const char*>>({
        {MarkerState::kReady, "ready"},
        {MarkerState::kSwapped, "swapped"},
        {MarkerState::kGood, "good"},
        {MarkerState::kRolledBack, "rolled_back"},
        {MarkerState::kFailed, "failed"},
    });

const char* MarkerStateName(MarkerState state) {
  for (const auto& [value, name] : kMarkerStates) {
    if (value == state) {
      return name;
    }
  }
  return "";
}

std::optional<MarkerState> MarkerStateFromName(std::string_view name) {
  for (const auto& [value, state_name] : kMarkerStates) {
    if (name == state_name) {
      return value;
    }
  }
  return std::nullopt;
}

bool IsPlainFileName(std::string_view name) {
  return !name.empty() && name[0] != '.' &&
         std::ranges::all_of(name, [](char c) {
           return base::IsAsciiAlphaNumeric(c) || c == '.' || c == '-' ||
                  c == '_' || c == '+';
         });
}

// An absolute path with no "..": what the marker and the helper accept.
bool IsCleanAbsolutePath(const base::FilePath& path) {
  return path.IsAbsolute() && !path.ReferencesParent() &&
         path.BaseName() != base::FilePath(base::FilePath::kCurrentDirectory);
}

// The hidden staging folder's prefix next to `app`: ".Koan-update-".
std::string SiblingStagingPrefix(const base::FilePath& app) {
  return base::StrCat(
      {".", app.BaseName().RemoveFinalExtension().value(), "-update-"});
}

// "bytes <first>-<last>/<total>".
bool ParseContentRange(std::string_view text,
                       int64_t* first,
                       int64_t* last,
                       int64_t* total) {
  constexpr std::string_view kUnit = "bytes ";
  if (!base::StartsWith(text, kUnit)) {
    return false;
  }
  text.remove_prefix(kUnit.size());
  const size_t dash = text.find('-');
  const size_t slash = text.find('/');
  if (dash == std::string_view::npos || slash == std::string_view::npos ||
      dash > slash) {
    return false;
  }
  return base::StringToInt64(text.substr(0, dash), first) &&
         base::StringToInt64(text.substr(dash + 1, slash - dash - 1), last) &&
         base::StringToInt64(text.substr(slash + 1), total);
}

}  // namespace

std::optional<Blocker> CheckLocation(const InstallLocation& location) {
  if (location.translocated) {
    return Blocker::kTranslocated;
  }
  if (location.read_only) {
    return Blocker::kReadOnly;
  }
  if (!location.writable) {
    return Blocker::kNotWritable;
  }
  return std::nullopt;
}

base::FilePath StagingDir(const InstallLocation& location,
                          const base::FilePath& updates_dir,
                          std::string_view version) {
  if (location.updates_on_app_volume) {
    return updates_dir.AppendASCII(version);
  }
  return location.app.DirName().Append(
      base::StrCat({SiblingStagingPrefix(location.app), version}));
}

bool IsStagedPathFor(const base::FilePath& app,
                     const base::FilePath& staged,
                     const base::FilePath& updates_dir) {
  if (!IsCleanAbsolutePath(app) || !IsCleanAbsolutePath(staged) ||
      staged == app || staged.BaseName() != app.BaseName()) {
    return false;
  }
  const base::FilePath folder = staged.DirName();
  const std::string name = folder.BaseName().value();
  if (folder.DirName() == updates_dir) {
    return IsValidVersion(name);
  }
  const std::string prefix = SiblingStagingPrefix(app);
  return folder.DirName() == app.DirName() && base::StartsWith(name, prefix) &&
         IsValidVersion(std::string_view(name).substr(prefix.size()));
}

std::optional<std::string> DownloadFileName(const UpdateInfo& update) {
  const std::string name = update.url.ExtractFileName();
  if (!IsPlainFileName(name) || !base::EndsWith(name, ".dmg") ||
      name.size() == std::string_view(".dmg").size()) {
    return std::nullopt;
  }
  return name;
}

base::TimeDelta RetryDelay(int failures) {
  base::TimeDelta delay = kFirstRetryDelay;
  for (int i = 1; i < failures && delay < kMaxRetryDelay; ++i) {
    delay *= 2;
  }
  return std::min(delay, kMaxRetryDelay);
}

std::optional<std::string> RangeHeaderFor(int64_t partial, int64_t size) {
  if (partial <= 0 || partial >= size) {
    return std::nullopt;
  }
  return base::StrCat({"bytes=", base::NumberToString(partial), "-"});
}

BodyStart ClassifyResponse(int response_code,
                           std::string_view content_range,
                           int64_t partial,
                           int64_t size) {
  if (response_code == 200) {
    return BodyStart::kReplace;
  }
  int64_t first = 0;
  int64_t last = 0;
  int64_t total = 0;
  if (response_code == 206 && partial > 0 &&
      ParseContentRange(content_range, &first, &last, &total) &&
      first == partial && last == size - 1 && total == size) {
    return BodyStart::kAppend;
  }
  return BodyStart::kReject;
}

bool MatchesRelease(const UpdateInfo& update,
                    int64_t file_size,
                    base::span<const uint8_t> digest) {
  return file_size == update.size && digest.size() == kSha256Size &&
         base::HexEncodeLower(digest) == update.sha256;
}

std::string ReleaseCodeRequirement() {
  return base::StrCat(
      {"=certificate leaf = H\"", kReleaseCertificateSha1, "\""});
}

std::optional<PrepareError> CheckStagedBundle(
    const StagedBundle& staged,
    std::string_view bundle_id,
    std::string_view update_version,
    std::string_view running_version) {
  if (bundle_id.empty() || staged.bundle_id != bundle_id) {
    return PrepareError::kBundleId;
  }
  if (staged.koan_version != update_version ||
      !IsNewerVersion(staged.koan_version, running_version)) {
    return PrepareError::kVersion;
  }
  return std::nullopt;
}

InstallMarker::InstallMarker() = default;
InstallMarker::InstallMarker(const InstallMarker&) = default;
InstallMarker& InstallMarker::operator=(const InstallMarker&) = default;
InstallMarker::~InstallMarker() = default;

std::string SerializeMarker(const InstallMarker& marker) {
  base::DictValue dict;
  dict.Set("schema", kMarkerSchema);
  dict.Set("state", MarkerStateName(marker.state));
  dict.Set("version", marker.version);
  dict.Set("from_version", marker.from_version);
  dict.Set("app", marker.app.AsUTF8Unsafe());
  dict.Set("staged", marker.staged.AsUTF8Unsafe());
  dict.Set("launches", marker.launches);
  dict.Set("error", marker.error);
  return base::WriteJsonWithOptions(dict,
                                    base::JSONWriter::OPTIONS_PRETTY_PRINT)
      .value_or(std::string());
}

std::optional<InstallMarker> ParseMarker(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& dict = value->GetDict();
  const std::string* state = dict.FindString("state");
  const std::string* version = dict.FindString("version");
  const std::string* from_version = dict.FindString("from_version");
  const std::string* app = dict.FindString("app");
  const std::string* staged = dict.FindString("staged");
  const std::optional<int> launches = dict.FindInt("launches");
  const std::string* error = dict.FindString("error");
  if (dict.FindInt("schema") != kMarkerSchema || !state || !version ||
      !IsValidVersion(*version) || !from_version ||
      !IsValidVersion(*from_version) || !app || !staged || !launches ||
      *launches < 0) {
    return std::nullopt;
  }
  InstallMarker marker;
  const std::optional<MarkerState> parsed_state = MarkerStateFromName(*state);
  if (!parsed_state) {
    return std::nullopt;
  }
  marker.state = *parsed_state;
  marker.version = *version;
  marker.from_version = *from_version;
  marker.app = base::FilePath::FromUTF8Unsafe(*app);
  marker.staged = base::FilePath::FromUTF8Unsafe(*staged);
  marker.launches = *launches;
  marker.error = error ? *error : std::string();
  if (!IsCleanAbsolutePath(marker.app) || !IsCleanAbsolutePath(marker.staged)) {
    return std::nullopt;
  }
  return marker;
}

LaunchDecision DecideOnLaunch(const std::optional<InstallMarker>& marker,
                              std::string_view running_version,
                              const base::FilePath& running_app) {
  LaunchDecision decision;
  if (!marker) {
    return decision;
  }
  decision.marker = *marker;
  // Another copy's install (a development build sharing the profile, an app
  // that was moved): leave it to that copy.
  if (marker->app != running_app) {
    return decision;
  }
  switch (marker->state) {
    case MarkerState::kReady:
      decision.action = IsNewerVersion(marker->version, running_version)
                            ? LaunchAction::kResumeReady
                            : LaunchAction::kDiscard;
      break;
    case MarkerState::kSwapped:
      if (CompareVersions(marker->version, running_version) != 0) {
        // Not the version the helper swapped in (put back by hand, or the
        // helper stopped between its swap and the marker): nothing to
        // confirm or roll back.
        decision.action = LaunchAction::kDiscard;
        break;
      }
      ++decision.marker.launches;
      decision.action = decision.marker.launches > kMaxUnconfirmedLaunches
                            ? LaunchAction::kRollBack
                            : LaunchAction::kCountLaunch;
      break;
    case MarkerState::kGood:
    case MarkerState::kRolledBack:
    case MarkerState::kFailed:
      decision.action = LaunchAction::kFinish;
      break;
  }
  return decision;
}

InstallHold NextHold(const InstallHold& current,
                     std::string_view version,
                     base::Time now) {
  InstallHold hold;
  hold.version = std::string(version);
  hold.count = current.version == version ? current.count + 1 : 1;
  hold.until = hold.count >= 2 ? base::Time() : now + kHoldPeriod;
  return hold;
}

bool IsHeld(const InstallHold& hold, std::string_view version, base::Time now) {
  return !hold.version.empty() && hold.version == version && hold.count > 0 &&
         (hold.until.is_null() || now < hold.until);
}

InstallFlow::InstallFlow() = default;
InstallFlow::~InstallFlow() = default;

Step InstallFlow::OnUpdateFound(std::string_view version,
                                InstallMode mode,
                                std::optional<Blocker> blocker) {
  switch (stage_) {
    case Stage::kDownloading:
    case Stage::kPreparing:
    case Stage::kReady:
      return Step::kNone;
    case Stage::kWaitingForUser:
    case Stage::kManual:
      if (version_ == version) {
        return Step::kNone;
      }
      break;
    case Stage::kIdle:
      break;
  }
  version_ = std::string(version);
  failures_ = 0;
  if (blocker) {
    stage_ = Stage::kManual;
    return Step::kOfferManual;
  }
  if (mode == InstallMode::kAskFirst) {
    stage_ = Stage::kWaitingForUser;
    return Step::kAskUser;
  }
  stage_ = Stage::kDownloading;
  return Step::kDownload;
}

Step InstallFlow::OnStagedCopyFound(std::string_view version) {
  if (stage_ != Stage::kIdle) {
    return Step::kNone;
  }
  version_ = std::string(version);
  failures_ = 0;
  stage_ = Stage::kPreparing;
  return Step::kVerifyStaged;
}

Step InstallFlow::OnUserAccepted() {
  if (stage_ != Stage::kWaitingForUser) {
    return Step::kNone;
  }
  stage_ = Stage::kDownloading;
  return Step::kDownload;
}

Step InstallFlow::OnDownloaded(bool ok) {
  if (stage_ != Stage::kDownloading) {
    return Step::kNone;
  }
  if (ok) {
    stage_ = Stage::kPreparing;
    return Step::kPrepare;
  }
  if (++failures_ < kMaxDownloadAttempts) {
    return Step::kRetryLater;
  }
  stage_ = Stage::kManual;
  return Step::kOfferManual;
}

Step InstallFlow::OnPrepared(bool ok) {
  if (stage_ != Stage::kPreparing) {
    return Step::kNone;
  }
  stage_ = ok ? Stage::kReady : Stage::kManual;
  return ok ? Step::kAnnounceReady : Step::kOfferManual;
}

}  // namespace koan::updates
