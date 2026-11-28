// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_install_logic.h"

#include <array>
#include <string>

#include "base/files/file_path.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::updates {
namespace {

constexpr char kSha256[] =
    "f69b66205beddb182db65c2039d9c63aaebbd64130ad8a222b0cf22a40bfac70";

base::FilePath P(const char* path) {
  return base::FilePath(path);
}

UpdateInfo Update(const char* url) {
  UpdateInfo update;
  update.version = "0.0.4";
  update.platform = kPlatformMacArm64;
  update.size = 16;
  update.sha256 = kSha256;
  update.url = GURL(url);
  return update;
}

InstallLocation Location() {
  InstallLocation location;
  location.app = P("/Applications/Koan.app");
  location.writable = true;
  location.updates_on_app_volume = true;
  return location;
}

InstallMarker Marker(MarkerState state) {
  InstallMarker marker;
  marker.state = state;
  marker.version = "0.0.4";
  marker.from_version = "0.0.3";
  marker.app = P("/Applications/Koan.app");
  marker.staged = P("/Data/Koan/Updates/0.0.4/Koan.app");
  return marker;
}

TEST(KoanUpdateInstallTest, LocationBlockers) {
  EXPECT_EQ(CheckLocation(Location()), std::nullopt);

  InstallLocation location = Location();
  // HFS+ or exFAT: the swap would fail once Koan has quit.
  location.rename_swap = false;
  EXPECT_EQ(CheckLocation(location), Blocker::kNoRenameSwap);
  location.writable = false;
  EXPECT_EQ(CheckLocation(location), Blocker::kNotWritable);
  // Running from the disk image, or from App Translocation's copy (which is
  // read-only too): those come first.
  location.read_only = true;
  EXPECT_EQ(CheckLocation(location), Blocker::kReadOnly);
  location.translocated = true;
  EXPECT_EQ(CheckLocation(location), Blocker::kTranslocated);
}

TEST(KoanUpdateInstallTest, StagingDirIsOnTheAppsVolume) {
  const base::FilePath updates = P("/Data/Koan/Updates");
  InstallLocation location = Location();
  EXPECT_EQ(StagingDir(location, updates, "0.0.4"), updates.Append("0.0.4"));

  // The app on another volume: a hidden folder next to it.
  location.app = P("/Volumes/Fast/Apps/Koan.app");
  location.updates_on_app_volume = false;
  const base::FilePath sibling = StagingDir(location, updates, "0.0.4");
  EXPECT_EQ(sibling, P("/Volumes/Fast/Apps/.Koan-update-0.0.4"));

  // Both are places the helper accepts; nothing else is.
  EXPECT_TRUE(
      IsStagedPathFor(location.app, sibling.Append("Koan.app"), updates));
  EXPECT_TRUE(IsStagedPathFor(
      location.app, updates.Append("0.0.4").Append("Koan.app"), updates));
  EXPECT_FALSE(IsStagedPathFor(location.app, location.app, updates));
  EXPECT_FALSE(IsStagedPathFor(
      location.app, updates.Append("0.0.4").Append("Other.app"), updates));
  EXPECT_FALSE(IsStagedPathFor(
      location.app, updates.Append("latest").Append("Koan.app"), updates));
  EXPECT_FALSE(
      IsStagedPathFor(location.app, P("/tmp/0.0.4/Koan.app"), updates));
  EXPECT_FALSE(IsStagedPathFor(
      location.app, P("/Volumes/Fast/Apps/.Koan-update-x/Koan.app"), updates));
  EXPECT_FALSE(IsStagedPathFor(
      location.app, updates.Append("0.0.4/../0.0.5/Koan.app"), updates));
  EXPECT_FALSE(IsStagedPathFor(location.app, P("0.0.4/Koan.app"), updates));
}

TEST(KoanUpdateInstallTest, DownloadFileName) {
  EXPECT_EQ(DownloadFileName(Update("https://github.com/koan-browser/koan/"
                                    "releases/download/v0.0.4/"
                                    "Koan-0.0.4-arm64.dmg")),
            "Koan-0.0.4-arm64.dmg");
  for (const char* url :
       {"https://github.com/koan-browser/koan/releases/download/v0.0.4/"
        "Koan-0.0.4-x86_64.flatpak",
        "https://github.com/koan-browser/koan/releases/download/v0.0.4/.dmg",
        "https://github.com/koan-browser/koan/releases/download/v0.0.4/"
        ".hidden.dmg",
        "https://github.com/koan-browser/koan/releases/download/v0.0.4/"
        "Koan%20Setup.dmg",
        "https://github.com/koan-browser/koan/releases/download/v0.0.4/"}) {
    EXPECT_EQ(DownloadFileName(Update(url)), std::nullopt) << url;
  }
}

TEST(KoanUpdateInstallTest, RetriesBackOff) {
  EXPECT_EQ(RetryDelay(1), base::Minutes(10));
  EXPECT_EQ(RetryDelay(2), base::Minutes(20));
  EXPECT_EQ(RetryDelay(3), base::Minutes(40));
  EXPECT_EQ(RetryDelay(100), base::Hours(6));
}

TEST(KoanUpdateInstallTest, ResumesWithARange) {
  EXPECT_EQ(RangeHeaderFor(0, 100), std::nullopt);
  EXPECT_EQ(RangeHeaderFor(40, 100), "bytes=40-");
  // Complete (or too long): start again, the hash decides.
  EXPECT_EQ(RangeHeaderFor(100, 100), std::nullopt);
  EXPECT_EQ(RangeHeaderFor(120, 100), std::nullopt);

  EXPECT_EQ(ClassifyResponse(200, "", 0, 100), BodyStart::kReplace);
  // A server that ignores the Range sends it all again.
  EXPECT_EQ(ClassifyResponse(200, "", 40, 100), BodyStart::kReplace);
  EXPECT_EQ(ClassifyResponse(206, "bytes 40-99/100", 40, 100),
            BodyStart::kAppend);
  // A 206 that isn't exactly the rest, or a 416: the partial file goes.
  for (const char* range :
       {"bytes 0-99/100", "bytes 40-98/100", "bytes 40-99/200", "bytes 40-99/*",
        "40-99/100", ""}) {
    EXPECT_EQ(ClassifyResponse(206, range, 40, 100), BodyStart::kDiscard)
        << range;
  }
  EXPECT_EQ(ClassifyResponse(206, "bytes 0-99/100", 0, 100),
            BodyStart::kDiscard);
  EXPECT_EQ(ClassifyResponse(416, "", 40, 100), BodyStart::kDiscard);
  // Anything else is the server's trouble: the partial file stays.
  for (int code : {0, 304, 404, 429, 500, 502, 503}) {
    EXPECT_EQ(ClassifyResponse(code, "", 40, 100), BodyStart::kRetry) << code;
  }
}

TEST(KoanUpdateInstallTest, OnlyAFailureWithoutProgressCounts) {
  // Nothing arrived.
  EXPECT_FALSE(MadeProgress(40, 40, 0));
  EXPECT_FALSE(MadeProgress(0, 0, 0));
  // Resumed at 40 and got further.
  EXPECT_TRUE(MadeProgress(40, 40, 1));
  EXPECT_TRUE(MadeProgress(0, 0, 10));
  // A 200 that started over and didn't get past what was on disk before:
  // no progress, or a server that keeps failing early would be retried
  // forever.
  EXPECT_FALSE(MadeProgress(40, 0, 40));
  EXPECT_TRUE(MadeProgress(40, 0, 41));
}

TEST(KoanUpdateInstallTest, MatchesTheSignedSizeAndHash) {
  const UpdateInfo update = Update(
      "https://github.com/koan-browser/koan/releases/download/v0.0.4/"
      "Koan-0.0.4-arm64.dmg");
  std::array<uint8_t, 32> digest;
  ASSERT_TRUE(base::HexStringToSpan(kSha256, digest));
  EXPECT_TRUE(MatchesRelease(update, 16, digest));
  EXPECT_FALSE(MatchesRelease(update, 17, digest));
  digest[31] ^= 1;
  EXPECT_FALSE(MatchesRelease(update, 16, digest));
  EXPECT_FALSE(MatchesRelease(update, 16, base::span(digest).first<31u>()));
}

TEST(KoanUpdateInstallTest, PinsTheReleaseCertificate) {
  EXPECT_EQ(
      ReleaseCodeRequirement(),
      "=certificate leaf = H\"41c53b537255ba68d030856528bdaccd559e0b50\"");
}

TEST(KoanUpdateInstallTest, OnlyARejectedReleaseIsHeld) {
  for (PrepareError error : {PrepareError::kSignature, PrepareError::kBundleId,
                             PrepareError::kVersion}) {
    EXPECT_TRUE(IsRejection(error)) << static_cast<int>(error);
  }
  // A full disk, a busy hdiutil, a timeout: tried again.
  for (PrepareError error :
       {PrepareError::kMount, PrepareError::kNoApp, PrepareError::kCopy,
        PrepareError::kQuarantine, PrepareError::kTimeout}) {
    EXPECT_FALSE(IsRejection(error)) << static_cast<int>(error);
  }
}

TEST(KoanUpdateInstallTest, StagedBundleChecks) {
  const StagedBundle good{"dev.koan.Koan", "0.0.4"};
  EXPECT_EQ(CheckStagedBundle(good, "dev.koan.Koan", "0.0.4", "0.0.3"),
            std::nullopt);
  EXPECT_EQ(CheckStagedBundle({"dev.other.App", "0.0.4"}, "dev.koan.Koan",
                              "0.0.4", "0.0.3"),
            PrepareError::kBundleId);
  EXPECT_EQ(CheckStagedBundle(good, "", "0.0.4", "0.0.3"),
            PrepareError::kBundleId);
  // Not the version the manifest signs, or not newer than this build.
  EXPECT_EQ(CheckStagedBundle(good, "dev.koan.Koan", "0.0.5", "0.0.3"),
            PrepareError::kVersion);
  EXPECT_EQ(CheckStagedBundle(good, "dev.koan.Koan", "0.0.4", "0.0.4"),
            PrepareError::kVersion);
  EXPECT_EQ(CheckStagedBundle({"dev.koan.Koan", ""}, "dev.koan.Koan", "0.0.4",
                              "0.0.3"),
            PrepareError::kVersion);
}

TEST(KoanUpdateInstallTest, MarkerRoundTrips) {
  InstallMarker marker = Marker(MarkerState::kSwapped);
  marker.launches = 2;
  marker.error = "EPERM";
  const std::optional<InstallMarker> parsed =
      ParseMarker(SerializeMarker(marker));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->state, MarkerState::kSwapped);
  EXPECT_EQ(parsed->version, "0.0.4");
  EXPECT_EQ(parsed->from_version, "0.0.3");
  EXPECT_EQ(parsed->app, marker.app);
  EXPECT_EQ(parsed->staged, marker.staged);
  EXPECT_EQ(parsed->launches, 2);
  EXPECT_EQ(parsed->error, "EPERM");

  for (MarkerState state : {MarkerState::kReady, MarkerState::kGood,
                            MarkerState::kRolledBack, MarkerState::kFailed}) {
    const std::optional<InstallMarker> other =
        ParseMarker(SerializeMarker(Marker(state)));
    ASSERT_TRUE(other);
    EXPECT_EQ(other->state, state);
  }
}

TEST(KoanUpdateInstallTest, MarkerRejectsWhatIsNotOne) {
  EXPECT_FALSE(ParseMarker(""));
  EXPECT_FALSE(ParseMarker("[]"));
  EXPECT_FALSE(ParseMarker(R"({"schema": 2, "state": "ready"})"));
  const auto with = [](const char* key, const char* value) {
    InstallMarker marker = Marker(MarkerState::kReady);
    std::string json = SerializeMarker(marker);
    const std::string from = std::string("\"") + key + "\": ";
    const size_t at = json.find(from);
    const size_t end = json.find_first_of(",\n}", at + from.size());
    json.replace(at + from.size(), end - at - from.size(), value);
    return json;
  };
  EXPECT_TRUE(ParseMarker(with("state", "\"ready\"")));
  EXPECT_FALSE(ParseMarker(with("state", "\"done\"")));
  EXPECT_FALSE(ParseMarker(with("version", "\"latest\"")));
  EXPECT_FALSE(ParseMarker(with("from_version", "4")));
  EXPECT_FALSE(ParseMarker(with("app", "\"Koan.app\"")));
  EXPECT_FALSE(ParseMarker(with("staged", "\"/a/../Koan.app\"")));
  EXPECT_FALSE(ParseMarker(with("launches", "-1")));
}

TEST(KoanUpdateInstallTest, MarkerKeepsTheOldRequirement) {
  // With one.
  InstallMarker marker = Marker(MarkerState::kSwapped);
  marker.old_requirement =
      R"(identifier "org.koan.Koan" and certificate leaf = H"ab12")";
  std::string json = SerializeMarker(marker);
  std::optional<InstallMarker> parsed = ParseMarker(json);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->old_requirement, marker.old_requirement);

  // Without one (a marker Koan wrote before the swap, or an unsigned old
  // app): left out, and read back as empty.
  const std::string without = SerializeMarker(Marker(MarkerState::kSwapped));
  EXPECT_EQ(without.find("old_requirement"), std::string::npos);
  parsed = ParseMarker(without);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->old_requirement, "");

  // Malformed: not a string.
  marker.old_requirement = "x";
  json = SerializeMarker(marker);
  const std::string field = R"("old_requirement": "x")";
  ASSERT_NE(json.find(field), std::string::npos);
  for (const char* value : {"4", "null", "[]", "{}"}) {
    std::string bad = json;
    bad.replace(bad.find(field), field.size(),
                std::string(R"("old_requirement": )") + value);
    EXPECT_FALSE(ParseMarker(bad)) << bad;
  }
}

TEST(KoanUpdateInstallTest, DesignatedRequirementFromCodesign) {
  EXPECT_EQ(DesignatedRequirementFromCodesign(
                "Executable=/Applications/Koan.app/Contents/MacOS/Koan\n"
                "designated => identifier \"org.koan.Koan\" and anchor apple "
                "generic and certificate leaf[subject.OU] = \"ABCDE12345\"\n"),
            R"(identifier "org.koan.Koan" and anchor apple generic and )"
            R"(certificate leaf[subject.OU] = "ABCDE12345")");
  // Ad-hoc: the implicit requirement pins the code itself.
  EXPECT_EQ(DesignatedRequirementFromCodesign(
                "Executable=/a/Koan.app/Contents/MacOS/Koan\n"
                "# designated => cdhash H\"0123abcd\"\n"),
            R"(cdhash H"0123abcd")");
  // Unsigned, a failure, or nothing after the arrow.
  EXPECT_EQ(DesignatedRequirementFromCodesign(
                "/a/Koan.app: code object is not signed at all\n"),
            std::nullopt);
  EXPECT_EQ(DesignatedRequirementFromCodesign(""), std::nullopt);
  EXPECT_EQ(DesignatedRequirementFromCodesign("designated => \n"),
            std::nullopt);
}

TEST(KoanUpdateInstallTest, LaunchDecisions) {
  const base::FilePath app = P("/Applications/Koan.app");
  EXPECT_EQ(DecideOnLaunch(std::nullopt, "0.0.3", app).action,
            LaunchAction::kNone);

  // Another copy of Koan's install is left alone.
  EXPECT_EQ(DecideOnLaunch(Marker(MarkerState::kSwapped), "0.0.4",
                           P("/Volumes/Dev/out/Koan.app"))
                .action,
            LaunchAction::kNone);

  // Staged and still newer: offer it again. Installed by hand meanwhile:
  // drop it.
  EXPECT_EQ(DecideOnLaunch(Marker(MarkerState::kReady), "0.0.3", app).action,
            LaunchAction::kResumeReady);
  EXPECT_EQ(DecideOnLaunch(Marker(MarkerState::kReady), "0.0.4", app).action,
            LaunchAction::kDiscard);

  // The new version's launches are counted, and the third unconfirmed one
  // rolls back.
  InstallMarker marker = Marker(MarkerState::kSwapped);
  LaunchDecision decision = DecideOnLaunch(marker, "0.0.4", app);
  EXPECT_EQ(decision.action, LaunchAction::kCountLaunch);
  EXPECT_EQ(decision.marker.launches, 1);
  decision = DecideOnLaunch(decision.marker, "0.0.4", app);
  EXPECT_EQ(decision.action, LaunchAction::kCountLaunch);
  EXPECT_EQ(decision.marker.launches, 2);
  decision = DecideOnLaunch(decision.marker, "0.0.4", app);
  EXPECT_EQ(decision.action, LaunchAction::kRollBack);
  EXPECT_EQ(decision.marker.launches, 3);

  // Swapped, but the old version runs: nothing to confirm.
  EXPECT_EQ(DecideOnLaunch(Marker(MarkerState::kSwapped), "0.0.3", app).action,
            LaunchAction::kDiscard);

  for (MarkerState state :
       {MarkerState::kGood, MarkerState::kRolledBack, MarkerState::kFailed}) {
    EXPECT_EQ(DecideOnLaunch(Marker(state), "0.0.3", app).action,
              LaunchAction::kFinish);
  }
}

TEST(KoanUpdateInstallTest, HoldsARolledBackVersion) {
  const base::Time now = base::Time::FromSecondsSinceUnixEpoch(1'800'000'000);
  InstallHold hold;
  EXPECT_FALSE(IsHeld(hold, "0.0.4", now));

  hold = NextHold(hold, "0.0.4", now);
  EXPECT_EQ(hold.count, 1);
  EXPECT_TRUE(IsHeld(hold, "0.0.4", now + base::Days(6)));
  EXPECT_FALSE(IsHeld(hold, "0.0.4", now + base::Days(7)));
  EXPECT_FALSE(IsHeld(hold, "0.0.5", now));

  // A second failure of the same version: for good.
  hold = NextHold(hold, "0.0.4", now + base::Days(8));
  EXPECT_EQ(hold.count, 2);
  EXPECT_TRUE(IsHeld(hold, "0.0.4", now + base::Days(400)));
  EXPECT_FALSE(IsHeld(hold, "0.0.5", now));

  // Another version starts over.
  hold = NextHold(hold, "0.0.5", now);
  EXPECT_EQ(hold.count, 1);
  EXPECT_FALSE(IsHeld(hold, "0.0.4", now));
}

TEST(KoanUpdateInstallTest, FlowDownloadsPreparesAndAnnounces) {
  InstallFlow flow;
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt),
            Step::kDownload);
  EXPECT_EQ(flow.stage(), Stage::kDownloading);
  // One release at a time.
  EXPECT_EQ(flow.OnUpdateFound("0.0.5", InstallMode::kAutomatic, std::nullopt),
            Step::kNone);
  EXPECT_EQ(flow.version(), "0.0.4");
  EXPECT_EQ(flow.OnPrepared(std::nullopt), Step::kNone);

  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kOk), Step::kPrepare);
  EXPECT_EQ(flow.stage(), Stage::kPreparing);
  EXPECT_EQ(flow.OnPrepared(std::nullopt), Step::kAnnounceReady);
  EXPECT_EQ(flow.stage(), Stage::kReady);
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt),
            Step::kNone);
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kOk), Step::kNone);
}

TEST(KoanUpdateInstallTest, FlowRetriesThenFallsBack) {
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kFailed), Step::kRetryLater);
  EXPECT_EQ(flow.failures(), 1);
  EXPECT_EQ(flow.retry_delay(), base::Minutes(10));
  EXPECT_EQ(flow.stage(), Stage::kDownloading);
  static_assert(kMaxDownloadAttempts == 2);
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kFailed), Step::kOfferManual);
  EXPECT_EQ(flow.stage(), Stage::kManual);
  // Not offered twice for the same version; a newer one starts over.
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt),
            Step::kNone);
  EXPECT_EQ(flow.OnUpdateFound("0.0.5", InstallMode::kAutomatic, std::nullopt),
            Step::kDownload);
  EXPECT_EQ(flow.failures(), 0);
}

TEST(KoanUpdateInstallTest, FlowResumesASlowDownload) {
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  // A slow link times out again and again, each time further on: resumed
  // soon, and never counted.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(flow.OnDownloaded(DownloadResult::kFailedWithProgress),
              Step::kRetryLater);
    EXPECT_EQ(flow.failures(), 0);
    EXPECT_EQ(flow.retry_delay(), kResumeDelay);
    EXPECT_EQ(flow.stage(), Stage::kDownloading);
  }
  // A try without progress counts, and waits the usual time.
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kFailed), Step::kRetryLater);
  EXPECT_EQ(flow.failures(), 1);
  EXPECT_EQ(flow.retry_delay(), base::Minutes(10));
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kFailedWithProgress),
            Step::kRetryLater);
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kOk), Step::kPrepare);
}

TEST(KoanUpdateInstallTest, FlowFallsBackWhenTheReleaseIsRejected) {
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  flow.OnDownloaded(DownloadResult::kOk);
  EXPECT_EQ(flow.OnPrepared(PrepareError::kSignature), Step::kOfferManual);
  EXPECT_EQ(flow.stage(), Stage::kManual);
}

TEST(KoanUpdateInstallTest, FlowTriesAPrepareAgain) {
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  flow.OnDownloaded(DownloadResult::kOk);
  // A full disk: later, with the image kept (the next try finds it whole).
  EXPECT_EQ(flow.OnPrepared(PrepareError::kCopy), Step::kRetryLater);
  EXPECT_EQ(flow.stage(), Stage::kDownloading);
  EXPECT_EQ(flow.retry_delay(), base::Minutes(10));
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kOk), Step::kPrepare);
  // Up to kMaxDownloadAttempts tries in all.
  EXPECT_EQ(flow.OnPrepared(PrepareError::kTimeout), Step::kOfferManual);
  EXPECT_EQ(flow.stage(), Stage::kManual);
}

TEST(KoanUpdateInstallTest, FlowFollowsTheInstallMode) {
  // "Automatically" while waiting for Update: the download starts.
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAskFirst, std::nullopt);
  EXPECT_EQ(flow.OnModeChanged(InstallMode::kAskFirst), Step::kNone);
  EXPECT_EQ(flow.OnModeChanged(InstallMode::kAutomatic), Step::kDownload);
  EXPECT_EQ(flow.stage(), Stage::kDownloading);
  EXPECT_EQ(flow.OnModeChanged(InstallMode::kAutomatic), Step::kNone);
  // "Let me choose" while downloading: it stops and asks.
  flow.OnDownloaded(DownloadResult::kFailed);
  EXPECT_EQ(flow.OnModeChanged(InstallMode::kAskFirst), Step::kAskUser);
  EXPECT_EQ(flow.stage(), Stage::kWaitingForUser);
  EXPECT_EQ(flow.failures(), 0);
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kOk), Step::kNone);

  // Staged: the mode no longer matters.
  InstallFlow ready;
  ready.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  ready.OnDownloaded(DownloadResult::kOk);
  EXPECT_EQ(ready.OnModeChanged(InstallMode::kAskFirst), Step::kNone);
  ready.OnPrepared(std::nullopt);
  EXPECT_EQ(ready.OnModeChanged(InstallMode::kAskFirst), Step::kNone);
  EXPECT_EQ(ready.stage(), Stage::kReady);
}

TEST(KoanUpdateInstallTest, FlowStopsWhenChecksAreTurnedOff) {
  InstallFlow downloading;
  downloading.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  EXPECT_EQ(downloading.OnChecksTurnedOff(), Step::kCancel);
  EXPECT_EQ(downloading.stage(), Stage::kIdle);
  EXPECT_EQ(downloading.OnDownloaded(DownloadResult::kOk), Step::kNone);
  // Back on, the release found again resumes it.
  EXPECT_EQ(
      downloading.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt),
      Step::kDownload);

  InstallFlow asking;
  asking.OnUpdateFound("0.0.4", InstallMode::kAskFirst, std::nullopt);
  EXPECT_EQ(asking.OnChecksTurnedOff(), Step::kCancel);
  EXPECT_EQ(asking.OnUserAccepted(), Step::kNone);

  InstallFlow manual;
  manual.OnUpdateFound("0.0.4", InstallMode::kAutomatic, Blocker::kReadOnly);
  EXPECT_EQ(manual.OnChecksTurnedOff(), Step::kNone);
  EXPECT_EQ(manual.stage(), Stage::kIdle);

  // A staged copy stays ready, and one being prepared goes on.
  InstallFlow preparing;
  preparing.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  preparing.OnDownloaded(DownloadResult::kOk);
  EXPECT_EQ(preparing.OnChecksTurnedOff(), Step::kNone);
  EXPECT_EQ(preparing.OnPrepared(std::nullopt), Step::kAnnounceReady);
  EXPECT_EQ(preparing.OnChecksTurnedOff(), Step::kNone);
  EXPECT_EQ(preparing.stage(), Stage::kReady);
}

TEST(KoanUpdateInstallTest, RestartDecisions) {
  EXPECT_EQ(DecideRestart(false, false, false), RestartAction::kNone);
  EXPECT_EQ(DecideRestart(false, true, true), RestartAction::kNone);
  EXPECT_EQ(DecideRestart(true, false, false), RestartAction::kRestart);
  EXPECT_EQ(DecideRestart(true, false, true), RestartAction::kAsk);
  // Another profile's Koan first, agent or not: restarting wouldn't update.
  EXPECT_EQ(DecideRestart(true, true, false), RestartAction::kQuitOtherFirst);
  EXPECT_EQ(DecideRestart(true, true, true), RestartAction::kQuitOtherFirst);
}

TEST(KoanUpdateInstallTest, HelperPaths) {
  EXPECT_EQ(HelperPathIn(P("/Applications/Koan.app")),
            P("/Applications/Koan.app/Contents/Resources/bin/"
              "koan-update-helper"));
  EXPECT_EQ(HelperBundle(P("/Applications/Koan.app/Contents/Resources/bin/"
                           "koan-update-helper")),
            P("/Applications/Koan.app"));
  for (const char* path :
       {"/Applications/Koan.app/Contents/Resources/bin/other",
        "/Applications/Koan.app/Contents/MacOS/koan-update-helper",
        "/Applications/Koan/Contents/Resources/bin/koan-update-helper",
        "/tmp/koan-update-helper", "koan-update-helper",
        "Koan.app/Contents/Resources/bin/koan-update-helper",
        "/A/../Koan.app/Contents/Resources/bin/koan-update-helper"}) {
    EXPECT_EQ(HelperBundle(P(path)), base::FilePath()) << path;
  }
}

TEST(KoanUpdateInstallTest, HelperChecksTheMarker) {
  const base::FilePath app = P("/Applications/Koan.app");
  const base::FilePath updates = P("/Data/Koan/Updates");
  EXPECT_EQ(HelperRefusal(Marker(MarkerState::kReady), HelperTask::kInstall,
                          app, updates),
            std::nullopt);
  EXPECT_EQ(HelperRefusal(Marker(MarkerState::kSwapped), HelperTask::kRollBack,
                          app, updates),
            std::nullopt);
  EXPECT_NE(HelperRefusal(std::nullopt, HelperTask::kInstall, app, updates),
            std::nullopt);
  // Not the state the task needs.
  EXPECT_NE(HelperRefusal(Marker(MarkerState::kSwapped), HelperTask::kInstall,
                          app, updates),
            std::nullopt);
  EXPECT_NE(HelperRefusal(Marker(MarkerState::kReady), HelperTask::kRollBack,
                          app, updates),
            std::nullopt);
  for (MarkerState state :
       {MarkerState::kGood, MarkerState::kRolledBack, MarkerState::kFailed}) {
    EXPECT_NE(HelperRefusal(Marker(state), HelperTask::kInstall, app, updates),
              std::nullopt);
  }
  // A marker written for another app (a same-user process trying to have
  // the helper swap something else in) or by another copy of Koan.
  EXPECT_NE(HelperRefusal(Marker(MarkerState::kReady), HelperTask::kInstall,
                          P("/Users/me/Applications/Koan.app"), updates),
            std::nullopt);
  EXPECT_NE(HelperRefusal(Marker(MarkerState::kReady), HelperTask::kInstall,
                          base::FilePath(), updates),
            std::nullopt);
  InstallMarker other = Marker(MarkerState::kReady);
  other.app = P("/Applications/Other.app");
  other.staged = P("/Data/Koan/Updates/0.0.4/Other.app");
  EXPECT_NE(HelperRefusal(other, HelperTask::kInstall, app, updates),
            std::nullopt);
  // A copy where Koan doesn't stage one.
  InstallMarker elsewhere = Marker(MarkerState::kReady);
  elsewhere.staged = P("/tmp/0.0.4/Koan.app");
  EXPECT_NE(HelperRefusal(elsewhere, HelperTask::kInstall, app, updates),
            std::nullopt);
}

TEST(KoanUpdateInstallTest, HelpersDontWaitForEachOther) {
  const base::FilePath app = P("/Applications/Koan.app");
  EXPECT_TRUE(BlocksSwap(app, "/Applications/Koan.app/Contents/MacOS/Koan"));
  EXPECT_TRUE(BlocksSwap(app,
                         "/Applications/Koan.app/Contents/Frameworks/Koan "
                         "Framework.framework/Helpers/Koan Helper.app/"
                         "Contents/MacOS/Koan Helper"));
  EXPECT_TRUE(BlocksSwap(app,
                         "/Applications/Koan.app/Contents/Resources/bin/"
                         "koan-agent"));
  // Another helper (another profile's, or one still waiting to roll back).
  EXPECT_FALSE(BlocksSwap(app,
                          "/Applications/Koan.app/Contents/Resources/bin/"
                          "koan-update-helper"));
  EXPECT_FALSE(BlocksSwap(app, "/Applications/Koan.app2/Contents/MacOS/Koan"));
  EXPECT_FALSE(BlocksSwap(app, "/Applications/Koan.app"));
  EXPECT_FALSE(BlocksSwap(app, ""));
  EXPECT_FALSE(BlocksSwap(base::FilePath(), "/Contents/MacOS/Koan"));
}

TEST(KoanUpdateInstallTest, PassesOnlyATestRequirementToTheHelper) {
  EXPECT_EQ(CodeRequirementSwitchValue(ReleaseCodeRequirement()), std::nullopt);
  EXPECT_EQ(CodeRequirementSwitchValue(""), std::nullopt);
  // What GetUpdateSource() made of --koan-update-code-requirement=<text>.
  EXPECT_EQ(CodeRequirementSwitchValue("=certificate leaf = H\"abc\""),
            "certificate leaf = H\"abc\"");
}

TEST(KoanUpdateInstallTest, SweepsTheUpdatesFolder) {
  const std::optional<InstallMarker> none;
  const char kName[] = "Koan-0.0.5-arm64.dmg";
  for (const char* kept : {"Koan-0.0.5-arm64.dmg", "Koan-0.0.5-arm64.dmg.part",
                           "install.json", "helper.log", "helper.old.log"}) {
    EXPECT_FALSE(ShouldSweepUpdatesEntry(P("/Data/Koan/Updates").Append(kept),
                                         kName, none))
        << kept;
  }
  for (const char* swept : {"Koan-0.0.4-arm64.dmg", "Koan-0.0.4-arm64.dmg.part",
                            "0.0.4", "install.json.tmp"}) {
    EXPECT_TRUE(ShouldSweepUpdatesEntry(P("/Data/Koan/Updates").Append(swept),
                                        kName, none))
        << swept;
  }
  // The folder the marker names stays, also another copy's (its old app,
  // until its new version has started).
  InstallMarker other = Marker(MarkerState::kSwapped);
  other.app = P("/Users/me/Applications/Koan.app");
  EXPECT_FALSE(
      ShouldSweepUpdatesEntry(P("/Data/Koan/Updates/0.0.4"), kName, other));
  EXPECT_TRUE(
      ShouldSweepUpdatesEntry(P("/Data/Koan/Updates/0.0.3"), kName, other));
}

TEST(KoanUpdateInstallTest, SweepsStaleStagingFolders) {
  const base::FilePath app = P("/Applications/Koan.app");
  const std::optional<InstallMarker> none;
  // Older than the running version: nothing will install it.
  EXPECT_TRUE(IsStaleSiblingStaging(app, P("/Applications/.Koan-update-0.0.3"),
                                    "0.0.4", none));
  // The running version's: the old app after a swap, kept until the new
  // version has started, also another profile's (whose marker this isn't).
  EXPECT_FALSE(IsStaleSiblingStaging(app, P("/Applications/.Koan-update-0.0.4"),
                                     "0.0.4", none));
  // Newer: another profile's copy, perhaps, or the next prepare redoes it.
  EXPECT_FALSE(IsStaleSiblingStaging(app, P("/Applications/.Koan-update-0.0.5"),
                                     "0.0.4", none));
  // The one the marker names stays, whatever its version.
  InstallMarker marked = Marker(MarkerState::kSwapped);
  marked.staged = P("/Applications/.Koan-update-0.0.3/Koan.app");
  EXPECT_FALSE(IsStaleSiblingStaging(app, P("/Applications/.Koan-update-0.0.3"),
                                     "0.0.4", marked));
  // Not a staging folder of this app's.
  for (const char* entry :
       {"/Applications/.Other-update-0.0.3", "/Applications/Koan-update-0.0.3",
        "/Applications/.Koan-update-x", "/Applications/.Koan-update-",
        "/Users/me/.Koan-update-0.0.3", "/Applications/Koan.app"}) {
    EXPECT_FALSE(IsStaleSiblingStaging(app, P(entry), "0.0.4", none)) << entry;
  }
}

TEST(KoanUpdateInstallTest, FindsImagesLeftAttached) {
  const base::FilePath updates = P("/Data/Koan/Updates");
  EXPECT_TRUE(
      IsUpdateImage(P("/Data/Koan/Updates/Koan-0.0.4-arm64.dmg"), updates));
  EXPECT_TRUE(IsUpdateImage(P("/Data/Koan/Updates/Koan-0.0.4-arm64.dmg.part"),
                            updates));
  for (const char* image :
       {"/Users/me/Downloads/Koan-0.0.4-arm64.dmg",
        "/Data/Koan/Updates/0.0.4/Koan-0.0.4-arm64.dmg",
        "/Data/Koan/Updates/disk.img", "Koan-0.0.4-arm64.dmg"}) {
    EXPECT_FALSE(IsUpdateImage(P(image), updates)) << image;
  }
}

TEST(KoanUpdateInstallTest, FlowOffersTheReleasePageWhenBlocked) {
  for (Blocker blocker :
       {Blocker::kUnsupported, Blocker::kTranslocated, Blocker::kReadOnly,
        Blocker::kNotWritable, Blocker::kNoRenameSwap, Blocker::kNoFile,
        Blocker::kHeld}) {
    InstallFlow flow;
    EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, blocker),
              Step::kOfferManual);
    EXPECT_EQ(flow.stage(), Stage::kManual);
  }
}

TEST(KoanUpdateInstallTest, FlowAsksFirst) {
  InstallFlow flow;
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAskFirst, std::nullopt),
            Step::kAskUser);
  EXPECT_EQ(flow.stage(), Stage::kWaitingForUser);
  // Nothing downloads until Update is clicked.
  EXPECT_EQ(flow.OnDownloaded(DownloadResult::kOk), Step::kNone);
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAskFirst, std::nullopt),
            Step::kNone);
  EXPECT_EQ(flow.OnUserAccepted(), Step::kDownload);
  EXPECT_EQ(flow.stage(), Stage::kDownloading);
  EXPECT_EQ(flow.OnUserAccepted(), Step::kNone);
}

TEST(KoanUpdateInstallTest, FlowResumesAStagedCopy) {
  InstallFlow flow;
  EXPECT_EQ(flow.OnStagedCopyFound("0.0.4"), Step::kVerifyStaged);
  EXPECT_EQ(flow.stage(), Stage::kPreparing);
  EXPECT_EQ(flow.OnStagedCopyFound("0.0.4"), Step::kNone);
  EXPECT_EQ(flow.OnPrepared(std::nullopt), Step::kAnnounceReady);
}

}  // namespace
}  // namespace koan::updates
