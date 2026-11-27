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
#include "base/time/time.h"
#include "build/build_config.h"
#include "koan/browser/updates/koan_update_install_logic.h"

// The mechanics of installing an update, behind a small interface so that
// what Koan decides stays in koan_update_install_logic.h. Only the Mac has
// them (koan_update_platform_mac.mm): hdiutil, ditto, the quarantine
// attribute, codesign, and the helper that swaps the apps with
// renamex_np(RENAME_SWAP) once Koan has exited (koan_update_helper_main.mm).
// Elsewhere CreateInstallPlatform() returns null and Koan only tells the user
// about a release (#549).
namespace koan::updates {

// The helper's switches (kHelperName, HelperPathIn()). It also takes
// kCodeRequirementSwitch (koan_update_source.h) in a test hook build.
inline constexpr char kHelperPidSwitch[] = "pid";
inline constexpr char kHelperMarkerSwitch[] = "marker";
inline constexpr char kHelperRelaunchSwitch[] = "relaunch";
inline constexpr char kHelperRollBackSwitch[] = "roll-back";

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
  // What the helper requires of the copy's signature right before it swaps
  // (UpdateSource's code_requirement; CodeRequirementSwitchValue()).
  std::string code_requirement;
};

class InstallPlatform {
 public:
  virtual ~InstallPlatform() = default;

  // Where the running app is, and whether Koan can replace it. Blocking.
  virtual InstallLocation Inspect(const base::FilePath& updates_dir) = 0;

  // Detaches the disk images in `updates_dir` that a prepare left attached
  // (IsUpdateImage()). Blocking.
  virtual void DetachLeftoverImages(const base::FilePath& updates_dir) = 0;

  // Whether another Koan (another profile's) runs from the running app.
  // Blocking, briefly.
  virtual bool OtherInstanceRunning() = 0;

  // The running app's bundle ID.
  virtual std::string BundleId() = 0;

  // Mounts `request.disk_image` read-only, copies the app in it into
  // `request.staging_dir`, unmounts it, removes the quarantine attribute and
  // checks the copy: its signature against `request.code_requirement`, then
  // CheckStagedBundle(). Blocking (a few seconds to a minute; each tool it
  // runs is stopped after a few minutes: PrepareError::kTimeout).
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

#if BUILDFLAG(IS_MAC)
// What the helper shares with Koan.

enum class ToolResult {
  kOk,
  // It ran and failed (exit status not 0), or didn't start.
  kFailed,
  // It didn't finish within the timeout, and was killed.
  kTimedOut,
};

// Runs `argv` (no shell), its output and errors in `output`; kills it after
// `timeout`. Blocking (a thread that may wait on a process).
ToolResult RunTool(const std::vector<std::string>& argv,
                   base::TimeDelta timeout,
                   std::string* output);

// codesign --verify --deep --strict -R `requirement` `app`.
ToolResult VerifyCodeSignature(const base::FilePath& app,
                               const std::string& requirement,
                               std::string* output);

// The processes this user can see, and the executable each runs (resolved,
// as proc_pidpath() gives it).
struct RunningProcess {
  int pid = 0;
  std::string path;
};
std::vector<RunningProcess> ListProcesses();
#endif

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UPDATES_KOAN_UPDATE_PLATFORM_H_
