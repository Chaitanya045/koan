// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UPDATES_KOAN_UPDATE_INSTALL_LOGIC_H_
#define KOAN_BROWSER_UPDATES_KOAN_UPDATE_INSTALL_LOGIC_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/time/time.h"
#include "koan/browser/updates/koan_update_logic.h"

// What Koan decides when it updates itself on the Mac (#550), the way Zen's
// (Firefox's) updater does: download the release in the background, check it,
// stage a copy of the new app, offer "Restart to update", and swap the apps
// once Koan has quit, keeping the old copy until the new one has started.
//
// Pure and platform-neutral, so all of it is unit-tested on any platform. The
// Mac's mechanics (hdiutil, codesign, the quarantine attribute, the swap)
// are koan_update_platform.h's; the download and the order of things are
// koan_update_installer.cc's; the helper that swaps once Koan has exited is
// koan_update_helper_main.mm.
namespace koan::updates {

// The settings (Look and Feel > Updates), Firefox's app.update.auto:
// "Automatically download and install updates" (on by default), or "Check
// for updates, but let me choose to install them".
enum class InstallMode {
  kAutomatic,
  kAskFirst,
};

// ---------------------------------------------------------------------------
// Whether Koan can replace itself.

// Where the running app is (koan_update_platform.h finds out).
struct InstallLocation {
  // The running app bundle (…/Koan.app).
  base::FilePath app;
  // Gatekeeper's App Translocation runs it from a read-only copy.
  bool translocated = false;
  // It is on a read-only volume: run from the disk image.
  bool read_only = false;
  // This user may rename the app and the folder it is in (the swap does).
  bool writable = false;
  // Koan's Updates folder is on the app's volume, so a copy staged there
  // swaps in with one rename.
  bool updates_on_app_volume = false;
  // The app's volume swaps two names in one rename (renamex_np's
  // RENAME_SWAP, VOL_CAP_INT_RENAME_SWAP): APFS does, HFS+ and exFAT don't.
  // True when the volume doesn't say.
  bool rename_swap = true;
};

// Why Koan can't install a release itself, and sends the user to the release
// page instead (#549's "Download" notice).
enum class Blocker {
  // No installer on this platform (Linux: the Flatpak can't replace itself).
  kUnsupported,
  kTranslocated,
  kReadOnly,
  kNotWritable,
  // The app's volume can't swap two names in one rename (HFS+, exFAT).
  kNoRenameSwap,
  // The release has no file Koan knows how to install (DownloadFileName()).
  kNoFile,
  // This version was rolled back or failed to install (IsHeld()).
  kHeld,
};

// What, if anything, keeps Koan at `location` from replacing itself.
std::optional<Blocker> CheckLocation(const InstallLocation& location);

// The folder to stage `version`'s copy in, so that the swap is one rename on
// one volume: <updates_dir>/<version> when the Updates folder is on the
// app's volume, else a hidden folder next to the app,
// <app's folder>/.<app name>-update-<version>. The copy is
// <folder>/<app's file name>.
base::FilePath StagingDir(const InstallLocation& location,
                          const base::FilePath& updates_dir,
                          std::string_view version);

// Whether `staged` is a copy StagingDir() could have placed for `app` (the
// helper checks the marker against this before it renames anything).
bool IsStagedPathFor(const base::FilePath& app,
                     const base::FilePath& staged,
                     const base::FilePath& updates_dir);

// ---------------------------------------------------------------------------
// The download.

// What the file is saved as: the URL's last segment ("Koan-0.0.4-arm64.dmg"),
// if it is a plain name (letters, digits, '.', '-', '_', '+') that ends in
// ".dmg"; nullopt otherwise.
std::optional<std::string> DownloadFileName(const UpdateInfo& update);

// The partial file a download writes to, kept across failures and restarts
// so the next try resumes it.
inline constexpr char kPartialSuffix[] = ".part";

// Firefox's app.update.download.maxAttempts (2 in Zen): after that many
// failed downloads of a version, Koan stops and offers the release page. A
// try that got further than the try before it doesn't count
// (DownloadResult::kFailedWithProgress): a slow link resumes until it is done.
inline constexpr int kMaxDownloadAttempts = 2;

// How long to wait before the next try after `failures` failed ones.
base::TimeDelta RetryDelay(int failures);

// How long to wait before resuming a try that made progress.
inline constexpr base::TimeDelta kResumeDelay = base::Seconds(30);

// Whether a try that failed got the file further than the `on_disk` bytes
// it started with: its response continued from `offset` and `received`
// bytes of it arrived. Each such try ends further on, and the file has a
// signed size, so resuming them always ends.
bool MadeProgress(int64_t on_disk, int64_t offset, int64_t received);

enum class DownloadResult {
  // Complete, and the file the manifest signs (MatchesRelease()).
  kOk,
  // Counts toward kMaxDownloadAttempts.
  kFailed,
  // MadeProgress(): resumed after kResumeDelay, not counted.
  kFailedWithProgress,
};

// The Range header that resumes a download with `partial` of `size` bytes
// on disk ("bytes=<partial>-"), or nullopt to start from the beginning.
std::optional<std::string> RangeHeaderFor(int64_t partial, int64_t size);

// What to do with the body of a response to a request for `size` bytes made
// with `partial` bytes on disk (RangeHeaderFor()).
enum class BodyStart {
  // 206 for exactly the rest: append to the partial file.
  kAppend,
  // 200: the whole file, so start the partial file again.
  kReplace,
  // A 206 that isn't the rest of the file, or 416 (the server doesn't have
  // that range): the partial file is no good. Drop it, count a failure.
  kDiscard,
  // Anything else (a 5xx, 429, 404…): count a failure and keep the partial
  // file for the next try.
  kRetry,
};
BodyStart ClassifyResponse(int response_code,
                           std::string_view content_range,
                           int64_t partial,
                           int64_t size);

// Whether a file of `file_size` bytes with SHA-256 `digest` is the one
// `update` signs.
bool MatchesRelease(const UpdateInfo& update,
                    int64_t file_size,
                    base::span<const uint8_t> digest);

// ---------------------------------------------------------------------------
// The staged copy.

// The release certificate ("Koan Release Signing",
// scripts/ci/codesign_identity.txt; #457, #458), pinned by its SHA-1. The
// real certificate's designated requirement reads `certificate root`, so the
// copy is checked with an explicit requirement (codesign -R), not by
// comparing the requirement's text.
inline constexpr char kReleaseCertificateSha1[] =
    "41c53b537255ba68d030856528bdaccd559e0b50";

// `=certificate leaf = H"<kReleaseCertificateSha1>"`: the requirement a
// staged copy must meet (codesign --verify --deep --strict -R).
std::string ReleaseCodeRequirement();

// What the staged copy's Info.plist says.
struct StagedBundle {
  std::string bundle_id;
  // KoanVersion (//koan/version), the version the release signs.
  std::string koan_version;
};

// Why a release couldn't be staged.
enum class PrepareError {
  kMount,
  kNoApp,
  kCopy,
  kQuarantine,
  // hdiutil, ditto or codesign didn't finish in time.
  kTimeout,
  kSignature,
  kBundleId,
  kVersion,
};

// Whether `error` says the release itself is no good (its signature, bundle
// ID or version): the version is held (IsHeld()) and its disk image goes.
// Other errors (a full disk, a busy hdiutil, a timeout) may pass: the image
// is kept, and preparing it is tried again (InstallFlow::OnPrepared()).
bool IsRejection(PrepareError error);

// Whether the copy in `staged` may replace the running app: the same bundle
// ID (`bundle_id`), the version the manifest signs (`update_version`), and
// newer than `running_version`.
std::optional<PrepareError> CheckStagedBundle(const StagedBundle& staged,
                                              std::string_view bundle_id,
                                              std::string_view update_version,
                                              std::string_view running_version);

// ---------------------------------------------------------------------------
// The install marker: <Updates>/install.json, shared by Koan and the helper.

inline constexpr char kMarkerFileName[] = "install.json";
inline constexpr int kMarkerSchema = 1;

enum class MarkerState {
  // Staged and checked: the helper swaps it in when Koan exits.
  kReady,
  // Swapped: the new version is installed, the old copy is at `staged`
  // until the new one has started.
  kSwapped,
  // The new version started: the old copy can go.
  kGood,
  // The new version didn't start, and the old copy was put back.
  kRolledBack,
  // The helper couldn't swap (`error` says why); nothing changed.
  kFailed,
};

struct InstallMarker {
  InstallMarker();
  InstallMarker(const InstallMarker&);
  InstallMarker& operator=(const InstallMarker&);
  ~InstallMarker();

  MarkerState state = MarkerState::kReady;
  // The version being installed, and the one it replaces.
  std::string version;
  std::string from_version;
  // The installed app, and the copy staged for it (after the swap, the old
  // app).
  base::FilePath app;
  base::FilePath staged;
  // Launches of the new version that haven't been confirmed yet.
  int launches = 0;
  // Why the helper failed, for the log.
  std::string error;
};

std::string SerializeMarker(const InstallMarker& marker);
// nullopt unless `json` is a schema 1 marker with absolute paths and valid
// versions.
std::optional<InstallMarker> ParseMarker(std::string_view json);

// A launch of the new version counts as good once it has had a window for
// kConfirmDelay, or when it quits normally.
inline constexpr base::TimeDelta kConfirmDelay = base::Seconds(10);

// A new version that started this many times without being confirmed is
// rolled back on its next launch.
inline constexpr int kMaxUnconfirmedLaunches = 2;

// What to do at startup about a marker.
enum class LaunchAction {
  // No marker, or another copy of Koan's.
  kNone,
  // This is the new version, not confirmed yet: count the launch (the
  // decision's marker has the new count) and confirm it later.
  kCountLaunch,
  // This is the new version and it failed to start too often: have the
  // helper put the old copy back, and quit.
  kRollBack,
  // A newer version is still staged: check the copy again, and offer it.
  kResumeReady,
  // Done (confirmed, rolled back or failed): remove the old or rejected
  // copy and the marker. A rollback or a failure holds the version.
  kFinish,
  // A marker that no longer means anything: remove it and its copy.
  kDiscard,
};

struct LaunchDecision {
  LaunchAction action = LaunchAction::kNone;
  InstallMarker marker;
};

LaunchDecision DecideOnLaunch(const std::optional<InstallMarker>& marker,
                              std::string_view running_version,
                              const base::FilePath& running_app);

// ---------------------------------------------------------------------------
// The Updates folder.

// Whether `entry`, in the Updates folder, may go when a download of
// `download_name` starts: anything but that download and its partial file,
// the marker, the helper's logs and the copy `marker` names (whichever
// copy of Koan it is for: another copy's old app, kept until its new version
// has started, included).
bool ShouldSweepUpdatesEntry(const base::FilePath& entry,
                             std::string_view download_name,
                             const std::optional<InstallMarker>& marker);

// Whether a disk image attached from `image_path` is one of Koan's
// downloads, left attached by a prepare that didn't finish (Koan quit, or
// hdiutil was stopped): detached at startup.
bool IsUpdateImage(const base::FilePath& image_path,
                   const base::FilePath& updates_dir);

// Whether `entry`, next to `app`, is a staging folder an earlier install left
// behind: a hidden <app's folder>/.<app name>-update-<version> folder for a
// version older than `running_version` (no copy of Koan from `app` will
// install it, and no copy's old app waits in it: that one is named for the
// running version) that `marker` doesn't name either.
bool IsStaleSiblingStaging(const base::FilePath& app,
                           const base::FilePath& entry,
                           std::string_view running_version,
                           const std::optional<InstallMarker>& marker);

// ---------------------------------------------------------------------------
// The helper (koan_update_helper_main.mm).

// In the app bundle (Contents/Resources/bin; //koan/cli's bundle data).
inline constexpr char kHelperName[] = "koan-update-helper";
// Its log, next to the marker, and the one before it: helper.log is started
// again once it is past kMaxHelperLogSize.
inline constexpr char kHelperLogName[] = "helper.log";
inline constexpr char kHelperOldLogName[] = "helper.old.log";
inline constexpr int64_t kMaxHelperLogSize = 1024 * 1024;

// <app>/Contents/Resources/bin/koan-update-helper.
base::FilePath HelperPathIn(const base::FilePath& app);

// The app a helper at `helper` (an absolute path) is part of: the .app that
// HelperPathIn() gives `helper` for, or an empty path.
base::FilePath HelperBundle(const base::FilePath& helper);

// What the helper was started for.
enum class HelperTask {
  // --roll-back absent: swap a kReady copy in.
  kInstall,
  // --roll-back: put the old copy (kSwapped) back.
  kRollBack,
};

// Why the helper refuses `marker`, or nullopt if it may swap its copy in:
// the marker is the one `task` expects, names the app the helper is part of
// (`own_app`; a same-user process could otherwise write a marker that swaps
// any app) and a copy StagingDir() could have placed for it. The copy's
// signature is checked again right before the swap, too.
std::optional<std::string> HelperRefusal(
    const std::optional<InstallMarker>& marker,
    HelperTask task,
    const base::FilePath& own_app,
    const base::FilePath& updates_dir);

// Whether a process running the executable `process_path` keeps the helper
// from replacing `app` (both resolved): anything that runs from inside it,
// except another helper (two helpers would otherwise wait for each other).
bool BlocksSwap(const base::FilePath& app, std::string_view process_path);

// The code requirement switch's value (kCodeRequirementSwitch,
// koan_update_source.h) that passes `requirement` (UpdateSource's
// code_requirement) to the helper: none for ReleaseCodeRequirement(), which
// the helper requires by itself, else the test hooks' text. The helper reads
// it back with GetUpdateSource(), which takes it only in a build with
// koan_update_test_hooks.
std::optional<std::string> CodeRequirementSwitchValue(
    std::string_view requirement);

// ---------------------------------------------------------------------------
// Holding a version back after a rollback or a failed install.

struct InstallHold {
  std::string version;
  // Until then; null: for good.
  base::Time until;
  // How often this version failed.
  int count = 0;
};

// The first rollback or failure of a version holds it for kHoldPeriod (the
// next check doesn't retry it straight away), a second one for good: from
// then on only the release page is offered for it.
inline constexpr base::TimeDelta kHoldPeriod = base::Days(7);

InstallHold NextHold(const InstallHold& current,
                     std::string_view version,
                     base::Time now);
bool IsHeld(const InstallHold& hold, std::string_view version, base::Time now);

// ---------------------------------------------------------------------------
// The state machine: from a release found to "Restart to update".

// If the user doesn't restart, Zen reminds them after 6 days.
inline constexpr base::TimeDelta kRestartReminderDelay = base::Days(6);

// What "Restart to update" does.
enum class RestartAction {
  // No update is staged.
  kNone,
  // Another Koan (another profile's) runs from the app: the helper couldn't
  // replace it, so restarting would only bring the old version back. Tell
  // the user to quit that one first.
  kQuitOtherFirst,
  // An agent is using Koan: ask first.
  kAsk,
  kRestart,
};
RestartAction DecideRestart(bool ready,
                            bool other_instance_running,
                            bool agent_using_koan);

enum class Stage {
  kIdle,
  // "Check for updates, but let me choose": "Update Available!" with Update
  // is shown, nothing downloads until it is clicked.
  kWaitingForUser,
  kDownloading,
  // Mounting, copying and checking the copy (or, after a restart, checking
  // the copy staged before).
  kPreparing,
  // Staged: "Restart to update".
  kReady,
  // Koan can't install this version itself: the release page is offered.
  kManual,
};

// What the installer does next.
enum class Step {
  kNone,
  // Show "Update Available!" with Update.
  kAskUser,
  // Start the download (or resume it).
  kDownload,
  // Try the download again after retry_delay().
  kRetryLater,
  // Mount the disk image, stage its app and check it.
  kPrepare,
  // Check the copy staged before a restart.
  kVerifyStaged,
  // Tell Chromium's UpgradeDetector, show "Restart to update".
  kAnnounceReady,
  // Show #549's notice with the release page.
  kOfferManual,
  // Stop the download or the wait for a retry (the settings changed).
  kCancel,
};

class InstallFlow {
 public:
  InstallFlow();
  InstallFlow(const InstallFlow&) = delete;
  InstallFlow& operator=(const InstallFlow&) = delete;
  ~InstallFlow();

  // A release newer than this build verified. `blocker`: why Koan can't
  // install it itself, if it can't. One release at a time: while one is
  // downloading, preparing or ready, others wait for the next check.
  Step OnUpdateFound(std::string_view version,
                     InstallMode mode,
                     std::optional<Blocker> blocker);
  // At startup, a copy of `version` staged before the restart (the
  // marker's kResumeReady).
  Step OnStagedCopyFound(std::string_view version);
  // Update was clicked in "Update Available!".
  Step OnUserAccepted();
  // A try of the download ended.
  Step OnDownloaded(DownloadResult result);
  // The copy was staged and checked (no error), or not. A rejection
  // (IsRejection()) offers the release page; another error tries again
  // later, up to kMaxDownloadAttempts tries in all.
  Step OnPrepared(std::optional<PrepareError> error);
  // "When a new version is out" changed: "let me choose" stops a download
  // and asks; "automatically" starts the one that was waiting for Update.
  Step OnModeChanged(InstallMode mode);
  // "Check for updates" was turned off: a download, or the wait for Update,
  // stops (the partial file stays). A staged copy stays ready, as Firefox
  // still installs an update it has staged.
  Step OnChecksTurnedOff();

  Stage stage() const { return stage_; }
  const std::string& version() const { return version_; }
  int failures() const { return failures_; }
  base::TimeDelta retry_delay() const {
    return resume_soon_ ? kResumeDelay : RetryDelay(failures_);
  }

 private:
  Stage stage_ = Stage::kIdle;
  std::string version_;
  int failures_ = 0;
  // The last try made progress (DownloadResult::kFailedWithProgress).
  bool resume_soon_ = false;
};

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UPDATES_KOAN_UPDATE_INSTALL_LOGIC_H_
