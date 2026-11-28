// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// koan-update-helper: swaps a staged update in once Koan has exited (#550).
//
//   koan-update-helper --pid=<Koan's pid> --marker=<Updates/install.json>
//                      [--relaunch] [--roll-back] -- <Koan's arguments>
//
// Koan starts it while it terminates with an update ready (or, to roll back,
// when the new version failed to start too often). It
// 1. waits for that process (its parent) to exit, then for every other
//    process running from the app (another profile's Koan, its helpers; not
//    another koan-update-helper) to go: the app is never replaced while it
//    runs;
// 2. checks the marker (koan_update_install_logic.h's HelperRefusal(): it
//    must name the app this helper is part of) and, right before the swap,
//    the staged copy's signature against the release certificate again
//    (the marker and the copy are in folders the user can write);
// 3. swaps the installed app and the staged copy with one
//    renamex_np(RENAME_SWAP), so the old app is at the staged path
//    afterwards, and records that in the marker, with the old app's own
//    designated requirement, read just before the swap;
// 4. runs the new version's `Koan --version`: one that can't start at all
//    (dyld, the wrong architecture) would never get as far as counting its
//    launches, so it is swapped back at once;
//    a swap back checks the old copy against that requirement rather than
//    the release certificate, so it also works for an old app signed
//    otherwise (a development build), and is refused if none was read;
// 5. with --relaunch, opens the app again with Koan's arguments (Chromium's
//    relaunch command line, so the session comes back), and watches the new
//    version: if it exits before it has confirmed itself in the marker,
//    the old copy is swapped back and opened instead.
// With --roll-back, step 3 swaps the old copy back.
//
// With --relaunch Koan has left its relaunch to the helper, so the helper
// opens the app on every way out, whatever failed.
//
// It logs to helper.log next to the marker (started again past 1 MB). It
// runs from the old app's bundle, which stays whole at the staged path after
// the swap.

#import <AppKit/AppKit.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/mac/launch_application.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "koan/browser/updates/koan_update_install_logic.h"
#include "koan/browser/updates/koan_update_platform.h"
#include "koan/browser/updates/koan_update_source.h"

namespace koan::updates {
namespace {

// How long the app's other processes get to exit after Koan did.
constexpr base::TimeDelta kIdleTimeout = base::Seconds(30);
// How long a relaunched new version is watched for a crash before it has
// confirmed itself (kConfirmDelay after its first window).
constexpr base::TimeDelta kWatchTimeout = base::Minutes(2);
constexpr base::TimeDelta kPollInterval = base::Milliseconds(250);
// How long LaunchServices gets to open the app.
constexpr base::TimeDelta kOpenTimeout = base::Seconds(30);
// How long the new version gets to print its version.
constexpr base::TimeDelta kSmokeTestTimeout = base::Minutes(1);

std::optional<InstallMarker> ReadMarker(const base::FilePath& path) {
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return std::nullopt;
  }
  return ParseMarker(json);
}

bool WriteMarker(const base::FilePath& path, const InstallMarker& marker) {
  const base::FilePath temp = path.AddExtension(FILE_PATH_LITERAL("tmp"));
  const bool ok = base::WriteFile(temp, SerializeMarker(marker)) &&
                  base::ReplaceFile(temp, path, nullptr);
  if (!ok) {
    PLOG(ERROR) << "Couldn't write " << path;
  }
  return ok;
}

// Keeps helper.log and the one before it, at about kMaxHelperLogSize each.
void RotateLog(const base::FilePath& log_path) {
  const std::optional<int64_t> size = base::GetFileSize(log_path);
  if (size && *size > kMaxHelperLogSize) {
    base::ReplaceFile(log_path, log_path.DirName().Append(kHelperOldLogName),
                      nullptr);
  }
}

// Waits for `pid`, this process's parent, to exit. Its parent having changed
// means it exited already (launchd took the helper over).
void WaitForParentExit(pid_t pid) {
  if (pid <= 1 || getppid() != pid) {
    return;
  }
  base::ScopedFD queue(kqueue());
  if (!queue.is_valid()) {
    PLOG(ERROR) << "kqueue";
    return;
  }
  struct kevent change;
  EV_SET(&change, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, nullptr);
  if (kevent(queue.get(), &change, 1, nullptr, 0, nullptr) == -1) {
    // ESRCH: gone already.
    return;
  }
  struct kevent event;
  HANDLE_EINTR(kevent(queue.get(), nullptr, 0, &event, 1, nullptr));
}

// Whether a process other than this one keeps the helper from replacing
// `app` (BlocksSwap(); proc_pidpath() gives resolved paths).
bool AnyProcessBlocksSwap(const base::FilePath& app) {
  const base::FilePath resolved = base::MakeAbsoluteFilePath(app);
  const base::FilePath& running_from = resolved.empty() ? app : resolved;
  for (const RunningProcess& process : ListProcesses()) {
    if (process.pid != getpid() && BlocksSwap(running_from, process.path)) {
      LOG(INFO) << "Still running from the app: " << process.pid << " "
                << process.path;
      return true;
    }
  }
  return false;
}

bool WaitUntilNothingRunsFrom(const base::FilePath& app) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + kIdleTimeout;
  while (AnyProcessBlocksSwap(app)) {
    if (base::TimeTicks::Now() > deadline) {
      return false;
    }
    base::PlatformThread::Sleep(kPollInterval);
  }
  return true;
}

// Swaps the installed app and the staged copy in one rename. 0, or the
// error.
int Swap(const InstallMarker& marker) {
  if (renamex_np(marker.staged.value().c_str(), marker.app.value().c_str(),
                 RENAME_SWAP) == 0) {
    return 0;
  }
  const int error = errno;
  LOG(ERROR) << "renamex_np " << marker.staged << " <-> " << marker.app << ": "
             << strerror(error);
  return error;
}

// Checks the copy at `marker.staged` against `requirement` again (it may
// have changed since Koan checked it), then swaps. Empty, or why not.
std::string CheckAndSwap(const InstallMarker& marker,
                         const std::string& requirement) {
  std::string output;
  if (VerifyCodeSignature(marker.staged, requirement, &output) !=
      ToolResult::kOk) {
    return base::StrCat({"codesign: ", output});
  }
  if (const int error = Swap(marker)) {
    return base::StrCat({"renamex_np: ", strerror(error)});
  }
  return std::string();
}

// The installed app's own designated requirement, or empty if it has none
// (unsigned) or codesign couldn't say.
std::string ReadOwnRequirement(const base::FilePath& app) {
  std::string output;
  if (DisplayCodeRequirement(app, &output) != ToolResult::kOk) {
    LOG(ERROR) << "No designated requirement for " << app << ": " << output;
    return std::string();
  }
  const std::optional<std::string> requirement =
      DesignatedRequirementFromCodesign(output);
  if (!requirement) {
    LOG(ERROR) << "No designated requirement for " << app << " in: " << output;
    return std::string();
  }
  LOG(INFO) << "Old app's requirement: " << *requirement;
  return *requirement;
}

// Whether the app at `app` starts as far as printing its version. A build
// that dyld can't load (a missing framework, the wrong architecture) exits
// before any of Koan's code could count its launches.
bool SmokeTest(const base::FilePath& app) {
  // Info.plist itself: NSBundle would cache what it read of the old app.
  NSDictionary* plist =
      [NSDictionary dictionaryWithContentsOfURL:base::apple::FilePathToNSURL(
                                                    app.Append("Contents")
                                                        .Append("Info.plist"))
                                          error:nil];
  const std::string name = base::SysNSStringToUTF8(
      base::apple::ObjCCast<NSString>(plist[@"CFBundleExecutable"]));
  if (name.empty() || base::FilePath(name).BaseName().value() != name) {
    LOG(ERROR) << app << " has no executable";
    return false;
  }
  std::string output;
  if (RunTool({app.Append("Contents").Append("MacOS").Append(name).value(),
               "--version"},
              kSmokeTestTimeout, &output) != ToolResult::kOk) {
    return false;
  }
  LOG(INFO) << "Starts: " << output;
  return true;
}

// What LaunchApplication() reports, which may come after Open() has given
// up waiting for it.
struct OpenResult : public base::RefCountedThreadSafe<OpenResult> {
  pid_t pid = 0;
  bool done = false;

 private:
  friend class base::RefCountedThreadSafe<OpenResult>;
  ~OpenResult() = default;
};

// Opens `app` as the Dock would, with `args`; its pid, or 0.
pid_t Open(const base::FilePath& app, const std::vector<std::string>& args) {
  if (app.empty()) {
    LOG(ERROR) << "No app to open";
    return 0;
  }
  LOG(INFO) << "Opening " << app;
  auto result = base::MakeRefCounted<OpenResult>();
  base::mac::LaunchApplication(
      app, args, /*url_specs=*/{},
      {.activate = true, .create_new_instance = true},
      base::BindOnce(
          [](scoped_refptr<OpenResult> result, NSRunningApplication* running,
             NSError* error) {
            if (error) {
              LOG(ERROR) << "Couldn't open the app: "
                         << base::SysNSStringToUTF8(error.description);
            } else {
              result->pid = running.processIdentifier;
            }
            result->done = true;
          },
          result));
  // The callback comes on the main queue, which the main run loop serves.
  const base::TimeTicks deadline = base::TimeTicks::Now() + kOpenTimeout;
  while (!result->done && base::TimeTicks::Now() < deadline) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, kPollInterval.InSecondsF(),
                       /*returnAfterSourceHandled=*/true);
  }
  if (!result->done) {
    LOG(ERROR) << "LaunchServices didn't open " << app << " in time";
  }
  return result->pid;
}

// With --relaunch, opens the app once the helper is done, whichever way it
// ends, unless it was opened already (Relaunch()).
class Relauncher {
 public:
  Relauncher(bool enabled, base::FilePath app, std::vector<std::string> args)
      : enabled_(enabled), app_(std::move(app)), args_(std::move(args)) {}
  Relauncher(const Relauncher&) = delete;
  Relauncher& operator=(const Relauncher&) = delete;
  ~Relauncher() {
    if (enabled_ && !opened_) {
      Open(app_, args_);
    }
  }

  // Opens it now; its pid, or 0.
  pid_t Relaunch() {
    opened_ = true;
    return Open(app_, args_);
  }
  // What was opened didn't last: open the app again on the way out.
  void OpenAgainOnExit() { opened_ = false; }

 private:
  const bool enabled_;
  const base::FilePath app_;
  const std::vector<std::string> args_;
  bool opened_ = false;
};

// Whether the new version has confirmed itself: the marker says so, or is
// gone (Koan removes it once it has cleaned up).
bool Confirmed(const base::FilePath& marker_path) {
  if (!base::PathExists(marker_path)) {
    return true;
  }
  const std::optional<InstallMarker> marker = ReadMarker(marker_path);
  return marker && marker->state == MarkerState::kGood;
}

// Watches the relaunched new version `pid`. False if it exited before it
// confirmed itself.
bool WatchNewVersion(pid_t pid, const base::FilePath& marker_path) {
  base::ScopedFD queue(kqueue());
  if (!queue.is_valid()) {
    return true;
  }
  struct kevent change;
  EV_SET(&change, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, nullptr);
  bool exited = kevent(queue.get(), &change, 1, nullptr, 0, nullptr) == -1;
  const base::TimeTicks deadline = base::TimeTicks::Now() + kWatchTimeout;
  while (!exited && base::TimeTicks::Now() < deadline) {
    if (Confirmed(marker_path)) {
      return true;
    }
    const timespec wait = kPollInterval.ToTimeSpec();
    struct kevent event;
    exited =
        HANDLE_EINTR(kevent(queue.get(), nullptr, 0, &event, 1, &wait)) == 1;
  }
  // Exited, or still running unconfirmed after the timeout (the launch count
  // in the marker takes over then).
  return !exited || Confirmed(marker_path);
}

// Puts the old copy back after the new version was swapped in (`marker` is
// kSwapped), checking it against `old_requirement`, the old app's own
// designated requirement as this helper read it before the swap (not the
// marker's copy, which anyone who can write the marker could change).
// Whether it did.
bool SwapBack(InstallMarker marker,
              const std::string& old_requirement,
              const base::FilePath& marker_path) {
  if (old_requirement.empty()) {
    LOG(ERROR) << "Can't put " << marker.from_version
               << " back: no designated requirement was read for it before "
                  "the swap (unsigned, or codesign -d failed), so it can't "
                  "be checked";
    return false;
  }
  if (!WaitUntilNothingRunsFrom(marker.app)) {
    LOG(ERROR) << marker.app << " still runs; can't put " << marker.from_version
               << " back";
    return false;
  }
  const std::string error = CheckAndSwap(marker, old_requirement);
  if (!error.empty()) {
    LOG(ERROR) << "Couldn't put " << marker.from_version << " back: " << error;
    return false;
  }
  marker.state = MarkerState::kRolledBack;
  WriteMarker(marker_path, marker);
  LOG(INFO) << "Put " << marker.from_version << " back";
  return true;
}

int HelperMain() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  const bool relaunch = command_line.HasSwitch(kHelperRelaunchSwitch);
  const bool roll_back = command_line.HasSwitch(kHelperRollBackSwitch);
  // Koan's own bundle: the only app this helper replaces, and the one it
  // opens again.
  base::FilePath executable;
  base::PathService::Get(base::FILE_EXE, &executable);
  const base::FilePath own_app = HelperBundle(executable);
  Relauncher relauncher(relaunch, own_app, command_line.GetArgs());

  int pid = 0;
  if (!base::StringToInt(command_line.GetSwitchValueASCII(kHelperPidSwitch),
                         &pid) ||
      pid <= 1) {
    pid = getppid();
  }
  const base::FilePath marker_path =
      command_line.GetSwitchValuePath(kHelperMarkerSwitch);
  if (marker_path.empty() || !marker_path.IsAbsolute()) {
    fprintf(stderr, "usage: %s --pid=<pid> --marker=<path> ...\n", kHelperName);
    WaitForParentExit(pid);
    return 2;
  }
  const base::FilePath log_path = marker_path.DirName().Append(kHelperLogName);
  RotateLog(log_path);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE | logging::LOG_TO_STDERR;
  settings.log_file_path = log_path.value().c_str();
  settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
  logging::InitLogging(settings);

  LOG(INFO) << "Waiting for " << pid << " to exit"
            << (roll_back ? " to roll back" : " to install")
            << (relaunch ? ", then relaunching " : " ") << own_app;
  WaitForParentExit(pid);

  std::optional<InstallMarker> marker = ReadMarker(marker_path);
  if (const std::optional<std::string> refusal = HelperRefusal(
          marker, roll_back ? HelperTask::kRollBack : HelperTask::kInstall,
          own_app, marker_path.DirName())) {
    LOG(ERROR) << "Nothing swapped for " << marker_path << ": " << *refusal;
    // A roll-back that can't happen mustn't be asked for at every launch.
    if (roll_back && marker && marker->state == MarkerState::kSwapped) {
      marker->state = MarkerState::kFailed;
      marker->error = *refusal;
      WriteMarker(marker_path, *marker);
    }
    return 1;
  }
  const base::FilePath app = marker->app;
  const std::string requirement =
      GetUpdateSource(command_line).code_requirement;
  if (!WaitUntilNothingRunsFrom(app)) {
    // Another Koan still runs from it: an install is tried again at the
    // next quit; a roll-back is given up (the new version does start).
    LOG(ERROR) << app << " still runs; nothing swapped";
    if (roll_back) {
      marker->state = MarkerState::kFailed;
      marker->error = "the app still ran";
      WriteMarker(marker_path, *marker);
    }
    return 1;
  }
  // Read before the swap, while the old app is still the installed one; a
  // roll-back doesn't need it (the release certificate checks the old copy,
  // as it checked it when it was installed).
  if (!roll_back) {
    marker->old_requirement = ReadOwnRequirement(app);
  }
  const std::string old_requirement = marker->old_requirement;
  if (const std::string error = CheckAndSwap(*marker, requirement);
      !error.empty()) {
    marker->state = MarkerState::kFailed;
    marker->error = error;
    WriteMarker(marker_path, *marker);
    return 1;
  }
  marker->state = roll_back ? MarkerState::kRolledBack : MarkerState::kSwapped;
  marker->launches = 0;
  WriteMarker(marker_path, *marker);
  LOG(INFO) << (roll_back ? "Rolled back to " : "Installed ")
            << (roll_back ? marker->from_version : marker->version);
  if (roll_back) {
    return 0;
  }
  if (!SmokeTest(app)) {
    LOG(ERROR) << marker->version << " doesn't start; putting "
               << marker->from_version << " back";
    return SwapBack(*marker, old_requirement, marker_path) ? 0 : 1;
  }
  if (!relaunch) {
    return 0;
  }

  const pid_t new_pid = relauncher.Relaunch();
  if (new_pid != 0 && WatchNewVersion(new_pid, marker_path)) {
    return 0;
  }
  // The new version didn't open, or died before it confirmed itself: put the
  // old one back, and open whichever is installed then.
  LOG(ERROR) << marker->version
             << (new_pid ? " exited before it started" : " didn't open")
             << "; rolling back";
  relauncher.OpenAgainOnExit();
  marker = ReadMarker(marker_path);
  if (!marker || marker->state != MarkerState::kSwapped) {
    return 1;
  }
  return SwapBack(*marker, old_requirement, marker_path) ? 0 : 1;
}

}  // namespace
}  // namespace koan::updates

int main(int argc, char* argv[]) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  @autoreleasepool {
    return koan::updates::HelperMain();
  }
}
