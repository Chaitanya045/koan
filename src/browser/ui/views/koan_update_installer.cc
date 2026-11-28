// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_update_installer.h"

#include <array>
#include <utility>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/first_run/upgrade_util.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "crypto/hash.h"
#include "koan/browser/koan_prefs.h"
#include "koan/version/koan_version.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace koan::updates {

namespace {

// A try gives up after this, stalled or not; the next one resumes it, at
// once if this one got further (MadeProgress()).
constexpr base::TimeDelta kDownloadTimeout = base::Minutes(15);

constexpr net::NetworkTrafficAnnotationTag kDownloadAnnotation =
    net::DefineNetworkTrafficAnnotation("koan_update_download", R"(
      semantics {
        sender: "Koan update download"
        description:
          "On the Mac, when the update check (koan_update_check) finds a "
          "newer release whose signed manifest verifies, Koan downloads that "
          "release's disk image (about 200 MB) in the background, keeps it "
          "only if its size and SHA-256 are the signed ones, and installs it "
          "when Koan next restarts or quits."
        trigger:
          "A newer release found by the update check, with 'Automatically "
          "download and install updates' on; with 'let me choose', only "
          "after the user clicks Update."
        data:
          "None: the release's fixed URL, and a Range header that resumes a "
          "partial download. No cookies, credentials or identifiers."
        destination: OTHER
        destination_other:
          "github.com (koan-browser/koan's releases), which redirects to "
          "GitHub's release asset host."
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings (chrome://settings/koan/looks), Look and Feel > Updates: "
          "Check for updates, and how to install them."
        policy_exception_justification: "Not implemented."
      })");

PrefService* LocalState() {
  return g_browser_process->local_state();
}

std::optional<InstallMarker> ReadMarker(const base::FilePath& path) {
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return std::nullopt;
  }
  return ParseMarker(json);
}

bool WriteMarker(const base::FilePath& path, const InstallMarker& marker) {
  const bool ok = base::ImportantFileWriter::WriteFileAtomically(
      path, SerializeMarker(marker));
  if (!ok) {
    LOG(ERROR) << "Couldn't write " << path;
  }
  return ok;
}

// Removes the marker and the copy it names (the old app once the new one is
// confirmed; the new one after a rollback or a failure), if it is a place
// Koan staged a copy in.
void RemoveInstall(const InstallMarker& marker,
                   const base::FilePath& updates_dir) {
  if (IsStagedPathFor(marker.app, marker.staged, updates_dir)) {
    base::DeletePathRecursively(marker.staged.DirName());
  }
  base::DeleteFile(updates_dir.Append(kMarkerFileName));
}

// Removes the hidden staging folders next to `app` that earlier installs
// left (IsStaleSiblingStaging()).
void SweepSiblingStaging(const base::FilePath& app,
                         const std::optional<InstallMarker>& marker) {
  if (app.empty()) {
    return;
  }
  base::FileEnumerator entries(app.DirName(), /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    if (IsStaleSiblingStaging(app, entry, koan::version::Full(), marker)) {
      LOG(WARNING) << "Removing " << entry << ", left by an update";
      base::DeletePathRecursively(entry);
    }
  }
}

UpdateStartState ReadStartState(InstallPlatform* platform,
                                const base::FilePath& updates_dir) {
  UpdateStartState state;
  // What a prepare that didn't finish (Koan quit, a tool was stopped) left.
  platform->DetachLeftoverImages(updates_dir);
  state.location = platform->Inspect(updates_dir);
  state.marker = ReadMarker(updates_dir.Append(kMarkerFileName));
  if (!state.location.read_only && !state.location.translocated) {
    SweepSiblingStaging(state.location.app, state.marker);
  }
  return state;
}

bool IsReleaseFile(const base::FilePath& path, const UpdateInfo& update) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  return file.IsValid() &&
         crypto::hash::HashFile(crypto::hash::kSha256, &file, digest) &&
         MatchesRelease(update, file.GetLength(), digest);
}

// Clears out what other downloads left, and finds out whether this one is
// done already or can be resumed.
UpdateDownloadStart StartDownloadOnFileThread(const base::FilePath& updates_dir,
                                              const std::string& name,
                                              const UpdateInfo& update) {
  UpdateDownloadStart start;
  start.final_path = updates_dir.AppendASCII(name);
  start.part_path = updates_dir.AppendASCII(name + kPartialSuffix);
  base::CreateDirectory(updates_dir);
  // The marker's copy stays, also when it is another copy of Koan's (its old
  // app, until its new version has started).
  const std::optional<InstallMarker> marker =
      ReadMarker(updates_dir.Append(kMarkerFileName));
  base::FileEnumerator entries(updates_dir, /*recursive=*/false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    if (ShouldSweepUpdatesEntry(entry, name, marker)) {
      base::DeletePathRecursively(entry);
    }
  }
  if (base::PathExists(start.final_path)) {
    if (IsReleaseFile(start.final_path, update)) {
      start.complete = true;
      return start;
    }
    base::DeleteFile(start.final_path);
  }
  start.partial = base::GetFileSize(start.part_path).value_or(0);
  if (start.partial > update.size) {
    base::DeleteFile(start.part_path);
    start.partial = 0;
  }
  return start;
}

PrepareResult PrepareOnFileThread(InstallPlatform* platform,
                                  const PrepareRequest& request,
                                  const base::FilePath& marker_path) {
  PrepareResult result;
  base::DeletePathRecursively(request.staging_dir);
  if (!base::CreateDirectory(request.staging_dir)) {
    result.error = PrepareError::kCopy;
    return result;
  }
  result = platform->Prepare(request);
  // The copy is staged, or the release is no good: either way the image can
  // go. After another error (a full disk, a timeout) it stays, for the next
  // try.
  if (!result.error || IsRejection(*result.error)) {
    base::DeleteFile(request.disk_image);
  }
  if (!result.error) {
    InstallMarker marker;
    marker.state = MarkerState::kReady;
    marker.version = request.version;
    marker.from_version = request.running_version;
    marker.app = request.app;
    marker.staged = result.staged_app;
    if (!WriteMarker(marker_path, marker)) {
      result.error = PrepareError::kCopy;
    }
  }
  if (result.error) {
    base::DeletePathRecursively(request.staging_dir);
  }
  return result;
}

PrepareResult VerifyOnFileThread(InstallPlatform* platform,
                                 const InstallMarker& marker,
                                 const PrepareRequest& request,
                                 const base::FilePath& updates_dir) {
  PrepareResult result = platform->Verify(marker.staged, request);
  if (result.error) {
    RemoveInstall(marker, updates_dir);
  }
  return result;
}

void ConfirmOnFileThread(const InstallMarker& marker,
                         const base::FilePath& updates_dir) {
  WriteMarker(updates_dir.Append(kMarkerFileName), marker);
}

// Chromium's own relaunch command line (upgrade_util), for the helper to
// open the new app with: the same switches, and --restart so the session is
// restored.
std::vector<std::string> RelaunchArgs() {
  std::vector<std::string> args =
      upgrade_util::GetRelaunchCommandLine(
          *base::CommandLine::ForCurrentProcess(),
          browser_shutdown::RestartMode::kRestartLastSession)
          .argv();
  args.erase(args.begin());
  return args;
}

}  // namespace

UpdatePartialFile::UpdatePartialFile() = default;
UpdatePartialFile::~UpdatePartialFile() = default;

bool UpdatePartialFile::Open(const base::FilePath& path, bool append) {
  path_ = path;
  file_.Initialize(
      path, append ? (base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND)
                   : (base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE));
  if (!file_.IsValid()) {
    LOG(ERROR) << "Couldn't open " << path << ": "
               << base::File::ErrorToString(file_.error_details());
  }
  return file_.IsValid();
}

bool UpdatePartialFile::Write(const std::string& data) {
  return file_.IsValid() &&
         file_.WriteAtCurrentPosAndCheck(base::as_byte_span(data));
}

void UpdatePartialFile::Close() {
  file_.Close();
}

void UpdatePartialFile::Discard() {
  file_.Close();
  if (!path_.empty()) {
    base::DeleteFile(path_);
  }
}

bool UpdatePartialFile::Finish(const base::FilePath& final_path,
                               const UpdateInfo& update) {
  file_.Close();
  const bool ok = !path_.empty() && IsReleaseFile(path_, update) &&
                  base::ReplaceFile(path_, final_path, nullptr);
  if (!ok) {
    LOG(ERROR) << "The download isn't Koan " << update.version
               << "'s signed file";
    base::DeleteFile(path_);
  }
  return ok;
}

UpdateInstaller::UpdateInstaller(std::unique_ptr<InstallPlatform> platform,
                                 UpdateSource source,
                                 ShowNotice show_notice)
    : platform_(std::move(platform)),
      source_(std::move(source)),
      show_notice_(std::move(show_notice)),
      // WithBaseSyncPrimitives(): hdiutil, ditto and codesign are waited
      // for with a timeout (RunTool()).
      file_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::WithBaseSyncPrimitives(),
           base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})),
      marker_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      partial_file_(file_runner_) {}

UpdateInstaller::~UpdateInstaller() = default;

base::FilePath UpdateInstaller::marker_path() const {
  return updates_dir_.Append(kMarkerFileName);
}

void UpdateInstaller::Start() {
  base::FilePath user_data;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    return;
  }
  updates_dir_ = user_data.AppendASCII("Updates");
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadStartState, base::Unretained(platform_.get()),
                     updates_dir_),
      base::BindOnce(&UpdateInstaller::OnStartState,
                     weak_factory_.GetWeakPtr()));
}

void UpdateInstaller::OnStartState(UpdateStartState state) {
  location_ = state.location;
  const LaunchDecision decision =
      DecideOnLaunch(state.marker, koan::version::Full(), location_.app);
  const InstallMarker& marker = decision.marker;
  switch (decision.action) {
    case LaunchAction::kNone:
      break;
    case LaunchAction::kCountLaunch:
      LOG(WARNING) << "Koan " << marker.version << ", launch "
                   << marker.launches << " since the update";
      unconfirmed_ = marker;
      marker_runner_->PostTask(FROM_HERE,
                               base::BindOnce(base::IgnoreResult(&WriteMarker),
                                              marker_path(), marker));
      WatchTermination();
      if (window_shown_) {
        OnWindowShown();
      }
      break;
    case LaunchAction::kRollBack: {
      // Started too often without getting as far as a window: the helper
      // puts the old copy back once Koan has quit, and opens it.
      LOG(ERROR) << "Koan " << marker.version << " didn't start "
                 << kMaxUnconfirmedLaunches << " times; rolling back to "
                 << marker.from_version;
      marker_runner_->PostTask(FROM_HERE,
                               base::BindOnce(base::IgnoreResult(&WriteMarker),
                                              marker_path(), marker));
      HelperRequest request;
      request.marker = marker_path();
      request.relaunch = true;
      request.relaunch_args = RelaunchArgs();
      request.roll_back = true;
      request.code_requirement = source_.code_requirement;
      if (platform_->StartHelper(request)) {
        rolling_back_ = true;
        chrome::AttemptExit();
        return;
      }
      break;
    }
    case LaunchAction::kResumeReady:
      resume_ = marker;
      break;
    case LaunchAction::kFinish:
      if (marker.state == MarkerState::kRolledBack ||
          marker.state == MarkerState::kFailed) {
        LOG(ERROR) << "Koan " << marker.version << " wasn't installed"
                   << (marker.error.empty() ? "" : ": ") << marker.error;
        HoldVersion(marker.version);
      }
      [[fallthrough]];
    case LaunchAction::kDiscard:
      file_runner_->PostTask(
          FROM_HERE, base::BindOnce(&RemoveInstall, marker, updates_dir_));
      break;
  }
  started_ = true;
  if (pending_ && !unconfirmed_) {
    const UpdateInfo update = std::move(*pending_);
    pending_.reset();
    TakeUpdate(update);
  }
}

void UpdateInstaller::OnWindowShown() {
  window_shown_ = true;
  if (unconfirmed_ && !confirm_timer_.IsRunning()) {
    confirm_timer_.Start(
        FROM_HERE, kConfirmDelay,
        base::BindOnce(&UpdateInstaller::Confirm, base::Unretained(this)));
  }
}

void UpdateInstaller::OnUpdateFound(const UpdateInfo& update) {
  if (!started_ || unconfirmed_ || rolling_back_) {
    pending_ = update;
    return;
  }
  TakeUpdate(update);
}

void UpdateInstaller::TakeUpdate(const UpdateInfo& update) {
  if (resume_) {
    if (resume_->version == update.version) {
      update_ = update;
      Run(flow_.OnStagedCopyFound(update.version));
      return;
    }
    // A newer release than the staged one: that copy goes.
    file_runner_->PostTask(
        FROM_HERE, base::BindOnce(&RemoveInstall, *resume_, updates_dir_));
    resume_.reset();
  }
  std::optional<Blocker> blocker = CheckLocation(location_);
  if (!blocker && !DownloadFileName(update)) {
    blocker = Blocker::kNoFile;
  }
  if (!blocker && IsHeld(ReadHold(), update.version, base::Time::Now())) {
    blocker = Blocker::kHeld;
  }
  const InstallMode mode = LocalState()->GetBoolean(prefs::kUpdatesAutoInstall)
                               ? InstallMode::kAutomatic
                               : InstallMode::kAskFirst;
  const Step step = flow_.OnUpdateFound(update.version, mode, blocker);
  if (step != Step::kNone) {
    update_ = update;
  }
  Run(step);
}

void UpdateInstaller::Accept() {
  Run(flow_.OnUserAccepted());
}

bool UpdateInstaller::IsReady() const {
  return flow_.stage() == Stage::kReady && !rolling_back_;
}

void UpdateInstaller::CheckOtherInstances(
    base::OnceCallback<void(bool)> callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&InstallPlatform::OtherInstanceRunning,
                     base::Unretained(platform_.get())),
      std::move(callback));
}

void UpdateInstaller::OnSettingsChanged() {
  if (!started_ || rolling_back_) {
    return;
  }
  if (!LocalState()->GetBoolean(prefs::kUpdatesCheckEnabled)) {
    Run(flow_.OnChecksTurnedOff());
    return;
  }
  Run(flow_.OnModeChanged(LocalState()->GetBoolean(prefs::kUpdatesAutoInstall)
                              ? InstallMode::kAutomatic
                              : InstallMode::kAskFirst));
}

void UpdateInstaller::CancelDownload() {
  retry_timer_.Stop();
  if (loader_) {
    loader_.reset();
    // The partial file stays, for when the download is started again.
    partial_file_.AsyncCall(&UpdatePartialFile::Close);
  }
}

void UpdateInstaller::Run(Step step) {
  switch (step) {
    case Step::kNone:
      return;
    case Step::kCancel:
      CancelDownload();
      return;
    case Step::kAskUser:
      CancelDownload();
      show_notice_.Run(update_, InstallNotice::kAskToUpdate);
      return;
    case Step::kDownload:
      StartDownload();
      return;
    case Step::kRetryLater:
      retry_timer_.Start(FROM_HERE, flow_.retry_delay(),
                         base::BindOnce(&UpdateInstaller::StartDownload,
                                        base::Unretained(this)));
      return;
    case Step::kPrepare:
      Prepare(/*staged_before=*/false);
      return;
    case Step::kVerifyStaged:
      Prepare(/*staged_before=*/true);
      return;
    case Step::kAnnounceReady:
      AnnounceReady();
      return;
    case Step::kOfferManual:
      show_notice_.Run(update_, InstallNotice::kDownloadByHand);
      return;
  }
}

void UpdateInstaller::StartDownload() {
  const std::optional<std::string> name = DownloadFileName(update_);
  if (!name || flow_.stage() != Stage::kDownloading ||
      g_browser_process->IsShuttingDown()) {
    return;
  }
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&StartDownloadOnFileThread, updates_dir_, *name, update_),
      base::BindOnce(&UpdateInstaller::OnDownloadStart,
                     weak_factory_.GetWeakPtr()));
}

void UpdateInstaller::OnDownloadStart(UpdateDownloadStart start) {
  // Stopped meanwhile (the settings changed), or started twice.
  if (flow_.stage() != Stage::kDownloading || loader_ ||
      g_browser_process->IsShuttingDown()) {
    return;
  }
  final_path_ = start.final_path;
  part_path_ = start.part_path;
  if (start.complete) {
    OnDownloadFinished(DownloadResult::kOk);
    return;
  }
  Fetch(start.partial);
}

void UpdateInstaller::Fetch(int64_t partial) {
  auto request = std::make_unique<network::ResourceRequest>();
  // The test hooks' local copy, or the release's file (IsReleaseDownloadUrl()
  // checked it). Either way only the signed size and SHA-256 are trusted.
  request->url =
      source_.download_url.is_empty() ? update_.url : source_.download_url;
  request->method = net::HttpRequestHeaders::kGetMethod;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  const std::optional<std::string> range =
      RangeHeaderFor(partial, update_.size);
  if (range) {
    request->headers.SetHeader(net::HttpRequestHeaders::kRange, *range);
  }
  on_disk_ = partial;
  partial_ = range ? partial : 0;
  offset_ = 0;
  received_ = 0;
  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kDownloadAnnotation);
  loader_->SetTimeoutDuration(kDownloadTimeout);
  loader_->SetOnResponseStartedCallback(base::BindOnce(
      &UpdateInstaller::OnResponseStarted, base::Unretained(this)));
  loader_->DownloadAsStream(
      g_browser_process->shared_url_loader_factory().get(), this);
}

void UpdateInstaller::OnResponseStarted(
    const GURL& final_url,
    const network::mojom::URLResponseHead& head) {
  const int code = head.headers ? head.headers->response_code() : 0;
  const std::string content_range =
      head.headers
          ? head.headers->GetNormalizedHeader("Content-Range").value_or("")
          : std::string();
  switch (ClassifyResponse(code, content_range, partial_, update_.size)) {
    case BodyStart::kAppend:
      offset_ = partial_;
      break;
    case BodyStart::kReplace:
      offset_ = 0;
      break;
    case BodyStart::kDiscard:
      LOG(ERROR) << "Response " << code << " (" << content_range
                 << ") doesn't continue " << part_path_ << "; starting over";
      AbortDownload(/*discard_partial=*/true);
      return;
    case BodyStart::kRetry:
      LOG(ERROR) << "Unexpected response " << code << " for " << final_url;
      AbortDownload(/*discard_partial=*/false);
      return;
  }
  partial_file_.AsyncCall(&UpdatePartialFile::Open)
      .WithArgs(part_path_, offset_ > 0);
}

void UpdateInstaller::OnDataReceived(std::string_view data,
                                     base::OnceClosure resume) {
  received_ += static_cast<int64_t>(data.size());
  if (offset_ + received_ > update_.size) {
    LOG(ERROR) << "The download is longer than the signed size";
    AbortDownload(/*discard_partial=*/true);
    return;
  }
  partial_file_.AsyncCall(&UpdatePartialFile::Write)
      .WithArgs(std::string(data))
      .Then(base::BindOnce(&UpdateInstaller::OnWritten,
                           weak_factory_.GetWeakPtr(), std::move(resume)));
}

void UpdateInstaller::OnWritten(base::OnceClosure resume, bool ok) {
  if (!loader_) {
    return;
  }
  if (!ok) {
    // A full disk, say: what was written is still good.
    LOG(ERROR) << "Couldn't write " << part_path_;
    AbortDownload(/*discard_partial=*/false);
    return;
  }
  std::move(resume).Run();
}

void UpdateInstaller::OnComplete(bool success) {
  const int net_error = loader_ ? loader_->NetError() : 0;
  loader_.reset();
  if (!success) {
    // The partial file stays: the next try resumes it.
    LOG(WARNING) << "The update download failed: " << net_error << " after "
                 << received_ << " bytes";
    partial_file_.AsyncCall(&UpdatePartialFile::Close);
    OnDownloadFinished(MadeProgress(on_disk_, offset_, received_)
                           ? DownloadResult::kFailedWithProgress
                           : DownloadResult::kFailed);
    return;
  }
  // A file that isn't the signed one (its hash) is deleted, and counts.
  partial_file_.AsyncCall(&UpdatePartialFile::Finish)
      .WithArgs(final_path_, update_)
      .Then(base::BindOnce(&UpdateInstaller::OnFinished,
                           weak_factory_.GetWeakPtr()));
}

void UpdateInstaller::OnFinished(bool ok) {
  OnDownloadFinished(ok ? DownloadResult::kOk : DownloadResult::kFailed);
}

void UpdateInstaller::OnRetry(base::OnceClosure start_retry) {
  // The loader has no retries set (each try resumes from the file instead).
  AbortDownload(/*discard_partial=*/false);
}

void UpdateInstaller::AbortDownload(bool discard_partial) {
  loader_.reset();
  if (discard_partial) {
    partial_file_.AsyncCall(&UpdatePartialFile::Discard);
  } else {
    partial_file_.AsyncCall(&UpdatePartialFile::Close);
  }
  OnDownloadFinished(DownloadResult::kFailed);
}

void UpdateInstaller::OnDownloadFinished(DownloadResult result) {
  Run(flow_.OnDownloaded(result));
}

PrepareRequest UpdateInstaller::MakePrepareRequest() const {
  PrepareRequest request;
  request.disk_image = final_path_;
  request.staging_dir = StagingDir(location_, updates_dir_, update_.version);
  request.app = location_.app;
  request.version = update_.version;
  request.running_version = std::string(koan::version::Full());
  request.code_requirement = source_.code_requirement;
  return request;
}

void UpdateInstaller::Prepare(bool staged_before) {
  // Not while quitting: hdiutil and ditto would outlive Koan (an image left
  // attached is detached at the next start, a half copy is redone).
  if (g_browser_process->IsShuttingDown()) {
    return;
  }
  const PrepareRequest request = MakePrepareRequest();
  if (staged_before && resume_) {
    file_runner_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&VerifyOnFileThread, base::Unretained(platform_.get()),
                       *resume_, request, updates_dir_),
        base::BindOnce(&UpdateInstaller::OnPrepared,
                       weak_factory_.GetWeakPtr()));
    resume_.reset();
    return;
  }
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&PrepareOnFileThread, base::Unretained(platform_.get()),
                     request, marker_path()),
      base::BindOnce(&UpdateInstaller::OnPrepared, weak_factory_.GetWeakPtr()));
}

void UpdateInstaller::OnPrepared(PrepareResult result) {
  if (result.error) {
    LOG(ERROR) << "Couldn't stage Koan " << update_.version << ": error "
               << static_cast<int>(*result.error) << " " << result.detail;
    // A release that isn't the signed one isn't tried again for a while:
    // the next check offers the release page. Anything else is tried again.
    if (IsRejection(*result.error)) {
      HoldVersion(update_.version);
    }
  }
  Run(flow_.OnPrepared(result.error));
}

void UpdateInstaller::AnnounceReady() {
  LOG(WARNING) << "Koan " << update_.version << " is ready to install";
  // Chromium's app menu badge and "Relaunch to update"
  // (upgrade_detector_impl.cc.patch has UpgradeDetectorImpl observe this
  // without Chromium's own updater). An invalid version counts as an update
  // too.
  g_browser_process->GetBuildState()->SetUpdate(
      BuildState::UpdateType::kNormalUpdate, base::Version(update_.chromium),
      std::nullopt);
  show_notice_.Run(update_, InstallNotice::kRestartToUpdate);
  reminder_timer_.Start(FROM_HERE, base::Time::Now() + kRestartReminderDelay,
                        base::BindOnce(&UpdateInstaller::RemindToRestart,
                                       base::Unretained(this)));
  WatchTermination();
}

void UpdateInstaller::RemindToRestart() {
  if (!IsReady()) {
    return;
  }
  show_notice_.Run(update_, InstallNotice::kRestartToUpdate);
  reminder_timer_.Start(FROM_HERE, base::Time::Now() + kRestartReminderDelay,
                        base::BindOnce(&UpdateInstaller::RemindToRestart,
                                       base::Unretained(this)));
}

void UpdateInstaller::Confirm() {
  if (!unconfirmed_) {
    return;
  }
  InstallMarker marker = std::move(*unconfirmed_);
  unconfirmed_.reset();
  confirm_timer_.Stop();
  marker.state = MarkerState::kGood;
  LOG(WARNING) << "Koan " << marker.version << " started; removing "
               << marker.from_version;
  // The marker first (it must be written before Koan exits), then the old
  // copy, which may take a while and needn't finish.
  marker_runner_->PostTaskAndReply(
      FROM_HERE, base::BindOnce(&ConfirmOnFileThread, marker, updates_dir_),
      base::BindOnce(
          [](scoped_refptr<base::SequencedTaskRunner> runner,
             InstallMarker marker, base::FilePath updates_dir) {
            runner->PostTask(
                FROM_HERE, base::BindOnce(&RemoveInstall, marker, updates_dir));
          },
          file_runner_, marker, updates_dir_));
  if (pending_ && !g_browser_process->IsShuttingDown()) {
    const UpdateInfo update = std::move(*pending_);
    pending_.reset();
    TakeUpdate(update);
  }
}

InstallHold UpdateInstaller::ReadHold() const {
  InstallHold hold;
  hold.version = LocalState()->GetString(prefs::kUpdatesHeldVersion);
  hold.until = LocalState()->GetTime(prefs::kUpdatesHeldUntil);
  hold.count = LocalState()->GetInteger(prefs::kUpdatesHeldCount);
  return hold;
}

void UpdateInstaller::HoldVersion(std::string_view version) {
  const InstallHold hold = NextHold(ReadHold(), version, base::Time::Now());
  LocalState()->SetString(prefs::kUpdatesHeldVersion, hold.version);
  LocalState()->SetTime(prefs::kUpdatesHeldUntil, hold.until);
  LocalState()->SetInteger(prefs::kUpdatesHeldCount, hold.count);
}

void UpdateInstaller::WatchTermination() {
  if (watching_termination_) {
    return;
  }
  watching_termination_ = true;
  terminating_ = browser_shutdown::AddAppTerminatingCallback(base::BindOnce(
      &UpdateInstaller::OnAppTerminating, base::Unretained(this)));
}

void UpdateInstaller::OnAppTerminating() {
  // A normal quit of the new version: it works.
  Confirm();
  if (!IsReady()) {
    return;
  }
  // Chromium's restart (AttemptRestart(), "Restart to update") asks for a
  // relaunch at the end of shutdown. The helper does it instead, after the
  // swap; Chromium's own relauncher would race it to the app. kWasRestarted
  // stays set, so the relaunched Koan restores the session all the same.
  PrefService* local_state = LocalState();
  HelperRequest request;
  request.marker = marker_path();
  request.code_requirement = source_.code_requirement;
  request.relaunch =
      local_state->GetBoolean(::prefs::kRestartLastSessionOnShutdown);
  if (request.relaunch) {
    request.relaunch_args = RelaunchArgs();
  }
  if (!platform_->StartHelper(request)) {
    return;
  }
  // From here the helper opens Koan again on every way out
  // (koan_update_helper_main.mm), whatever fails.
  if (request.relaunch) {
    local_state->ClearPref(::prefs::kRestartLastSessionOnShutdown);
  }
}

}  // namespace koan::updates
