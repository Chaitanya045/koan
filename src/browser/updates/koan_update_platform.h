// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UPDATES_KOAN_UPDATE_PLATFORM_H_
#define KOAN_BROWSER_UPDATES_KOAN_UPDATE_PLATFORM_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "koan/browser/updates/koan_update_install_logic.h"

// The mechanics of installing an update, behind a small interface so that
// what Koan decides stays in koan_update_install_logic.h. Only the Mac has
// them (koan_update_platform_mac.mm): hdiutil, ditto, the quarantine
// attribute, codesign, and the helper that swaps the apps with
// renamex_np(RENAME_SWAP) once Koan has exited (koan_update_helper_main.mm).
// Elsewhere CreateInstallPlatform() returns null and Koan only tells the user
// about a release (#549).
namespace koan::updates {

// The helper, in the app bundle (Contents/Resources/bin; //koan/cli's bundle
// data), and its switches.
inline constexpr char kHelperName[] = "koan-update-helper";
inline constexpr char kHelperPidSwitch[] = "pid";
inline constexpr char kHelperMarkerSwitch[] = "marker";
inline constexpr char kHelperRelaunchSwitch[] = "relaunch";
inline constexpr char kHelperRollBackSwitch[] = "roll-back";
// The helper's own log, next to the marker.
inline constexpr char kHelperLogName[] = "helper.log";

struct PrepareRequest {
  PrepareRequest();
  PrepareRequest(const PrepareRequest&);
  PrepareRequest& operator=(const PrepareRequest&);
  ~PrepareRequest();

  // The verified download.
  base::FilePath disk_image;
  // StagingDir(): created empty by the caller; the copy goes in it.
  base::FilePath staging_dir;
  // The running app (its file name is the copy's).
  base::FilePath app;
  // The version the manifest signs, and this build's.
  std::string version;
  std::string running_version;
  // codesign -R's argument (UpdateSource::code_requirement).
  std::string code_requirement;
};

struct PrepareResult {
  std::optional<PrepareError> error;
  // <staging_dir>/<app's file name>.
  base::FilePath staged_app;
  // For the log.
  std::string detail;
};

struct HelperRequest {
  HelperRequest();
  HelperRequest(const HelperRequest&);
  HelperRequest& operator=(const HelperRequest&);
  ~HelperRequest();

  // <Updates>/install.json.
  base::FilePath marker;
  // Open the app once the swap is done (Restart to update), with
  // `relaunch_args` (argv without the program).
  bool relaunch = false;
  std::vector<std::string> relaunch_args;
  // Swap the old copy back in (the new version failed to start).
  bool roll_back = false;
};

class InstallPlatform {
 public:
  virtual ~InstallPlatform() = default;

  // Where the running app is, and whether Koan can replace it. Blocking.
  virtual InstallLocation Inspect(const base::FilePath& updates_dir) = 0;

  // The running app's bundle ID.
  virtual std::string BundleId() = 0;

  // Mounts `request.disk_image` read-only, copies the app in it into
  // `request.staging_dir`, unmounts it, removes the quarantine attribute and
  // checks the copy: its signature against `request.code_requirement`, then
  // CheckStagedBundle(). Blocking (a few seconds to a minute).
  virtual PrepareResult Prepare(const PrepareRequest& request) = 0;

  // Only the checks of a copy staged before a restart. Blocking.
  virtual PrepareResult Verify(const base::FilePath& staged_app,
                               const PrepareRequest& request) = 0;

  // Starts the helper, which waits for this process to exit. Doesn't block:
  // fine on the UI thread while the app terminates. False if it didn't
  // start.
  virtual bool StartHelper(const HelperRequest& request) = 0;
};

// Null where Koan can't install updates itself.
std::unique_ptr<InstallPlatform> CreateInstallPlatform();

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UPDATES_KOAN_UPDATE_PLATFORM_H_
