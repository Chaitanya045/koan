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
  for (const char* range :
       {"bytes 0-99/100", "bytes 40-98/100", "bytes 40-99/200", "bytes 40-99/*",
        "40-99/100", ""}) {
    EXPECT_EQ(ClassifyResponse(206, range, 40, 100), BodyStart::kReject)
        << range;
  }
  EXPECT_EQ(ClassifyResponse(206, "bytes 0-99/100", 0, 100),
            BodyStart::kReject);
  EXPECT_EQ(ClassifyResponse(416, "", 40, 100), BodyStart::kReject);
  EXPECT_EQ(ClassifyResponse(404, "", 0, 100), BodyStart::kReject);
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
  EXPECT_EQ(flow.OnPrepared(true), Step::kNone);

  EXPECT_EQ(flow.OnDownloaded(true), Step::kPrepare);
  EXPECT_EQ(flow.stage(), Stage::kPreparing);
  EXPECT_EQ(flow.OnPrepared(true), Step::kAnnounceReady);
  EXPECT_EQ(flow.stage(), Stage::kReady);
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt),
            Step::kNone);
  EXPECT_EQ(flow.OnDownloaded(true), Step::kNone);
}

TEST(KoanUpdateInstallTest, FlowRetriesThenFallsBack) {
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  EXPECT_EQ(flow.OnDownloaded(false), Step::kRetryLater);
  EXPECT_EQ(flow.failures(), 1);
  EXPECT_EQ(flow.retry_delay(), base::Minutes(10));
  EXPECT_EQ(flow.stage(), Stage::kDownloading);
  static_assert(kMaxDownloadAttempts == 2);
  EXPECT_EQ(flow.OnDownloaded(false), Step::kOfferManual);
  EXPECT_EQ(flow.stage(), Stage::kManual);
  // Not offered twice for the same version; a newer one starts over.
  EXPECT_EQ(flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt),
            Step::kNone);
  EXPECT_EQ(flow.OnUpdateFound("0.0.5", InstallMode::kAutomatic, std::nullopt),
            Step::kDownload);
  EXPECT_EQ(flow.failures(), 0);
}

TEST(KoanUpdateInstallTest, FlowFallsBackWhenPrepareFails) {
  InstallFlow flow;
  flow.OnUpdateFound("0.0.4", InstallMode::kAutomatic, std::nullopt);
  flow.OnDownloaded(true);
  EXPECT_EQ(flow.OnPrepared(false), Step::kOfferManual);
  EXPECT_EQ(flow.stage(), Stage::kManual);
}

TEST(KoanUpdateInstallTest, FlowOffersTheReleasePageWhenBlocked) {
  for (Blocker blocker :
       {Blocker::kUnsupported, Blocker::kTranslocated, Blocker::kReadOnly,
        Blocker::kNotWritable, Blocker::kNoFile, Blocker::kHeld}) {
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
  EXPECT_EQ(flow.OnDownloaded(true), Step::kNone);
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
  EXPECT_EQ(flow.OnPrepared(true), Step::kAnnounceReady);
}

}  // namespace
}  // namespace koan::updates
