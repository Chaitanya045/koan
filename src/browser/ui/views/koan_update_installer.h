// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_UPDATE_INSTALLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_UPDATE_INSTALLER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "base/timer/wall_clock_timer.h"
#include "koan/browser/updates/koan_update_install_logic.h"
#include "koan/browser/updates/koan_update_logic.h"
#include "koan/browser/updates/koan_update_platform.h"
#include "koan/browser/updates/koan_update_source.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

namespace base {
class SequencedTaskRunner;
}

namespace network {
class SimpleURLLoader;
namespace mojom {
class URLResponseHead;
}
}  // namespace network

namespace koan::updates {

// What the installer asks the sidebar to show (koan_updates.cc).
enum class InstallNotice {
  // "Update Available!" with Update ("let me choose").
  kAskToUpdate,
  // Staged: "Restart to update".
  kRestartToUpdate,
  // Koan can't install it itself: #549's notice with the release page.
  kDownloadByHand,
};

// What Start() finds out on the file sequence.
struct UpdateStartState {
  InstallLocation location;
  std::optional<InstallMarker> marker;
};

// Where a download stands before it starts.
struct UpdateDownloadStart {
  base::FilePath final_path;
  base::FilePath part_path;
  // The file is there already and is the release's.
  bool complete = false;
  // Bytes in the partial file, to resume from.
  int64_t partial = 0;
};

// The download's partial file, on the installer's file sequence.
class UpdatePartialFile {
 public:
  UpdatePartialFile();
  UpdatePartialFile(const UpdatePartialFile&) = delete;
  UpdatePartialFile& operator=(const UpdatePartialFile&) = delete;
  ~UpdatePartialFile();

  // Opens `path`, to append to what is there or emptied. False on failure.
  bool Open(const base::FilePath& path, bool append);
  bool Write(const std::string& data);
  void Close();
  // Closes and deletes it: it doesn't continue the file the server has.
  void Discard();
  // Closes it, and renames it to `final_path` if it is `update`'s file (its
  // signed size and SHA-256); deletes it otherwise. Whether it was.
  bool Finish(const base::FilePath& final_path, const UpdateInfo& update);

 private:
  base::FilePath path_;
  base::File file_;
};

// Koan updating itself on the Mac (#550), with koan_update_install_logic.h's
// decisions and koan_update_platform.h's mechanics:
//
// - A newer release that verified (koan_updates.cc) is downloaded in the
//   background into <user data>/Updates (resumed with a Range after a failure
//   or a restart), and kept only if its size and SHA-256 are the signed ones.
// - Its app is staged and checked (signature pinned to the release
//   certificate, bundle ID, version), and install.json says so.
// - Chromium's UpgradeDetector is told through BuildState (the app menu's
//   badge and "Relaunch to update"; upgrade_detector_impl.cc.patch), and the
//   sidebar says "Restart to update", again after 6 days.
// - When Koan terminates with it staged, the helper is started: it swaps the
//   apps once Koan has exited, and opens the new one if this was a restart.
//   Chromium's own relaunch is turned off for that restart (the helper
//   relaunches with the same arguments), so the session still comes back.
// - The next launch confirms the new version, or rolls it back when it keeps
//   failing to start.
//
// One per browser process, from the first normal window to the end.
class UpdateInstaller : public network::SimpleURLLoaderStreamConsumer {
 public:
  using ShowNotice =
      base::RepeatingCallback<void(const UpdateInfo&, InstallNotice)>;

  UpdateInstaller(std::unique_ptr<InstallPlatform> platform,
                  UpdateSource source,
                  ShowNotice show_notice);
  UpdateInstaller(const UpdateInstaller&) = delete;
  UpdateInstaller& operator=(const UpdateInstaller&) = delete;
  ~UpdateInstaller() override;

  // Finds out where Koan is and what the last install left (the marker):
  // confirms, rolls back or cleans up, then takes releases.
  void Start();
  // A normal window is shown: the new version's confirmation waits for one.
  void OnWindowShown();
  // A release newer than this build whose manifest verified.
  void OnUpdateFound(const UpdateInfo& update);
  // Update in "Update Available!".
  void Accept();
  // Whether an update is staged, waiting for a restart.
  bool IsReady() const;
  // Finds out (off the UI thread) whether another Koan runs from the app,
  // which the helper couldn't replace: `callback` gets whether one does.
  void CheckOtherInstances(base::OnceCallback<void(bool)> callback);
  // "Check for updates" or "When a new version is out" changed.
  void OnSettingsChanged();

 private:
  void OnStartState(UpdateStartState state);
  void TakeUpdate(const UpdateInfo& update);
  void Run(Step step);

  void StartDownload();
  void OnDownloadStart(UpdateDownloadStart start);
  void Fetch(int64_t partial);
  void OnResponseStarted(const GURL& final_url,
                         const network::mojom::URLResponseHead& head);
  void OnWritten(base::OnceClosure resume, bool ok);
  void OnFinished(bool ok);
  // Ends this try as a failure, keeping the partial file for the next one
  // unless `discard_partial`.
  void AbortDownload(bool discard_partial);
  // Stops the download and the wait for a retry, keeping the partial file.
  void CancelDownload();
  void OnDownloadFinished(DownloadResult result);

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view data, base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

  PrepareRequest MakePrepareRequest() const;
  void Prepare(bool staged_before);
  void OnPrepared(PrepareResult result);
  void AnnounceReady();
  void RemindToRestart();

  void Confirm();
  void HoldVersion(std::string_view version);
  InstallHold ReadHold() const;
  void WatchTermination();
  void OnAppTerminating();

  base::FilePath marker_path() const;

  const std::unique_ptr<InstallPlatform> platform_;
  const UpdateSource source_;
  const ShowNotice show_notice_;
  // Downloads, staging and clean-up: may run for a minute, never holds up
  // the app's exit.
  const scoped_refptr<base::SequencedTaskRunner> file_runner_;
  // The confirmation's marker write, which must land before Koan exits.
  const scoped_refptr<base::SequencedTaskRunner> marker_runner_;

  base::FilePath updates_dir_;
  InstallLocation location_;
  bool started_ = false;
  bool window_shown_ = false;
  // The release found before Start() finished, or while the new version
  // wasn't confirmed yet.
  std::optional<UpdateInfo> pending_;

  InstallFlow flow_;
  UpdateInfo update_;
  // A copy staged before the restart, offered again when its release is
  // found again.
  std::optional<InstallMarker> resume_;
  // The marker of the new version, until it is confirmed.
  std::optional<InstallMarker> unconfirmed_;
  bool rolling_back_ = false;
  base::OneShotTimer confirm_timer_;

  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::SequenceBound<UpdatePartialFile> partial_file_;
  base::FilePath final_path_;
  base::FilePath part_path_;
  // Bytes on disk when the request was made, those the Range asked to
  // continue from (0 without one), those the response continues from (0 if
  // it starts over), and those received since.
  int64_t on_disk_ = 0;
  int64_t partial_ = 0;
  int64_t offset_ = 0;
  int64_t received_ = 0;
  base::OneShotTimer retry_timer_;

  // Wall-clock: the reminder survives the Mac sleeping.
  base::WallClockTimer reminder_timer_;
  base::CallbackListSubscription terminating_;
  bool watching_termination_ = false;

  base::WeakPtrFactory<UpdateInstaller> weak_factory_{this};
};

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_UPDATE_INSTALLER_H_
