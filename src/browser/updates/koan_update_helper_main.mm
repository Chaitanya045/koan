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
//    process running from the app (helpers, another profile's Koan) to go:
//    the app is never replaced while it runs;
// 2. swaps the installed app and the staged copy with one
//    renamex_np(RENAME_SWAP), so the old app is at the staged path
//    afterwards, and records that in the marker (koan_update_install_logic.h);
// 3. with --relaunch, opens the app again with Koan's arguments (Chromium's
//    relaunch command line, so the session comes back), and watches the new
//    version: if it exits before it has confirmed itself in the marker,
//    the old copy is swapped back and opened instead.
// With --roll-back, step 2 swaps the old copy back.
//
// It logs to helper.log next to the marker. It runs from the old app's
// bundle, which stays whole at the staged path after the swap.

#import <AppKit/AppKit.h>
#include <errno.h>
#include <libproc.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/mac/launch_application.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "koan/browser/updates/koan_update_install_logic.h"
#include "koan/browser/updates/koan_update_platform.h"

namespace koan::updates {
namespace {

// How long the app's other processes get to exit after Koan did.
constexpr base::TimeDelta kIdleTimeout = base::Seconds(30);
// How long a relaunched new version is watched for a crash before it has
// confirmed itself (kConfirmDelay after its first window).
constexpr base::TimeDelta kWatchTimeout = base::Minutes(2);
constexpr base::TimeDelta kPollInterval = base::Milliseconds(250);

std::optional<InstallMarker> ReadMarker(const base::FilePath& path) {
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return std::nullopt;
  }
  return ParseMarker(json);
}

bool WriteMarker(const base::FilePath& path, const InstallMarker& marker) {
  const base::FilePath temp = path.AddExtension(FILE_PATH_LITERAL("tmp"));
  return base::WriteFile(temp, SerializeMarker(marker)) &&
         base::ReplaceFile(temp, path, nullptr);
}

// Waits for `pid`, this process's parent, to exit. Its parent having changed
// means it exited already (launchd took the helper over).
void WaitForParentExit(pid_t pid) {
  if (getppid() != pid) {
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

// Whether a process other than this one runs an executable inside `app`
// (proc_pidpath() gives resolved paths).
bool AnyProcessRunsFrom(const base::FilePath& app) {
  const base::FilePath resolved = base::MakeAbsoluteFilePath(app);
  const std::string prefix = (resolved.empty() ? app : resolved).value() + "/";
  const int count = proc_listallpids(nullptr, 0);
  if (count <= 0) {
    return false;
  }
  std::vector<pid_t> pids(static_cast<size_t>(count) + 64);
  const int listed = proc_listallpids(
      pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
  if (listed <= 0) {
    return false;
  }
  pids.resize(std::min(pids.size(), static_cast<size_t>(listed)));
  std::array<char, PROC_PIDPATHINFO_MAXSIZE> path;
  for (pid_t pid : pids) {
    if (pid == getpid()) {
      continue;
    }
    const int length =
        proc_pidpath(pid, path.data(), static_cast<uint32_t>(path.size()));
    const std::string_view running(path.data(),
                                   static_cast<size_t>(std::max(length, 0)));
    if (base::StartsWith(running, prefix)) {
      LOG(INFO) << "Still running from the app: " << pid << " " << running;
      return true;
    }
  }
  return false;
}

bool WaitUntilNothingRunsFrom(const base::FilePath& app) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + kIdleTimeout;
  while (AnyProcessRunsFrom(app)) {
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

// Opens `app` as the Dock would, with `args`; its pid, or 0.
pid_t Open(const base::FilePath& app, const std::vector<std::string>& args) {
  pid_t pid = 0;
  base::mac::LaunchApplication(
      app, args, /*url_specs=*/{},
      {.activate = true, .create_new_instance = true},
      base::BindOnce(
          [](pid_t* out, NSRunningApplication* running, NSError* error) {
            if (error) {
              LOG(ERROR) << "Couldn't open the app: "
                         << base::SysNSStringToUTF8(error.description);
            } else {
              *out = running.processIdentifier;
            }
            CFRunLoopStop(CFRunLoopGetMain());
          },
          base::Unretained(&pid)));
  CFRunLoopRun();
  return pid;
}

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

int HelperMain() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  const base::FilePath marker_path =
      command_line.GetSwitchValuePath(kHelperMarkerSwitch);
  if (marker_path.empty() || !marker_path.IsAbsolute()) {
    fprintf(stderr, "usage: %s --pid=<pid> --marker=<path> ...\n", kHelperName);
    return 2;
  }
  logging::LoggingSettings logging;
  logging.logging_dest = logging::LOG_TO_FILE | logging::LOG_TO_STDERR;
  const base::FilePath log_path = marker_path.DirName().Append(kHelperLogName);
  logging.log_file_path = log_path.value().c_str();
  logging.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
  logging::InitLogging(logging);

  int pid = 0;
  if (!base::StringToInt(command_line.GetSwitchValueASCII(kHelperPidSwitch),
                         &pid) ||
      pid <= 1) {
    LOG(ERROR) << "No --" << kHelperPidSwitch;
    return 2;
  }
  const bool relaunch = command_line.HasSwitch(kHelperRelaunchSwitch);
  const bool roll_back = command_line.HasSwitch(kHelperRollBackSwitch);
  const std::vector<std::string> args = command_line.GetArgs();
  LOG(INFO) << "Waiting for " << pid << " to exit"
            << (roll_back ? " to roll back" : " to install")
            << (relaunch ? ", then relaunching" : "");
  WaitForParentExit(pid);

  std::optional<InstallMarker> marker = ReadMarker(marker_path);
  if (!marker ||
      !IsStagedPathFor(marker->app, marker->staged, marker_path.DirName()) ||
      marker->state !=
          (roll_back ? MarkerState::kSwapped : MarkerState::kReady)) {
    LOG(ERROR) << "No install to do in " << marker_path;
    return 1;
  }
  const base::FilePath app = marker->app;
  if (!WaitUntilNothingRunsFrom(app)) {
    // Another Koan still runs from it: try again at the next quit.
    LOG(ERROR) << app << " still runs; nothing swapped";
    if (relaunch) {
      Open(app, args);
    }
    return 1;
  }
  if (const int error = Swap(*marker)) {
    marker->state = MarkerState::kFailed;
    marker->error = base::StrCat({"renamex_np: ", strerror(error)});
    WriteMarker(marker_path, *marker);
    if (relaunch) {
      Open(app, args);
    }
    return 1;
  }
  marker->state = roll_back ? MarkerState::kRolledBack : MarkerState::kSwapped;
  marker->launches = 0;
  if (!WriteMarker(marker_path, *marker)) {
    PLOG(ERROR) << "Couldn't write " << marker_path;
  }
  LOG(INFO) << (roll_back ? "Rolled back to " : "Installed ")
            << (roll_back ? marker->from_version : marker->version);
  if (!relaunch) {
    return 0;
  }
  const pid_t new_pid = Open(app, args);
  if (roll_back || new_pid == 0 || WatchNewVersion(new_pid, marker_path)) {
    return 0;
  }

  // The new version died before it confirmed itself: put the old one back.
  LOG(ERROR) << marker->version << " exited before it started; rolling back";
  marker = ReadMarker(marker_path);
  if (!marker || marker->state != MarkerState::kSwapped ||
      !WaitUntilNothingRunsFrom(app) || Swap(*marker) != 0) {
    return 1;
  }
  marker->state = MarkerState::kRolledBack;
  WriteMarker(marker_path, *marker);
  Open(app, args);
  return 0;
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
