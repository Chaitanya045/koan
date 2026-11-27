// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_platform.h"

#import <Foundation/Foundation.h>
#include <errno.h>
#include <libproc.h>
#include <stdio.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/scoped_blocking_call.h"
#include "koan/browser/updates/koan_update_source.h"

namespace koan::updates {

namespace {

constexpr char kHdiutil[] = "/usr/bin/hdiutil";
constexpr char kDitto[] = "/usr/bin/ditto";
constexpr char kCodesign[] = "/usr/bin/codesign";
// LSFileQuarantineEnabled (Chromium's Info.plist) quarantines every file
// Koan writes, the download and what is copied from it included.
constexpr char kQuarantineAttribute[] = "com.apple.quarantine";

// Generous: a 200 MB app on a slow disk, a busy Mac. A tool that takes
// longer is stuck, and is killed (the prepare is tried again later).
constexpr base::TimeDelta kAttachTimeout = base::Minutes(3);
constexpr base::TimeDelta kCopyTimeout = base::Minutes(10);
constexpr base::TimeDelta kDetachTimeout = base::Minutes(1);
constexpr base::TimeDelta kInfoTimeout = base::Seconds(30);
constexpr base::TimeDelta kCodesignTimeout = base::Minutes(5);
// What is kept of a tool's output, for the log.
constexpr size_t kMaxToolOutput = 64 * 1024;

std::optional<PrepareError> ToolError(ToolResult result, PrepareError error) {
  switch (result) {
    case ToolResult::kOk:
      return std::nullopt;
    case ToolResult::kFailed:
      return error;
    case ToolResult::kTimedOut:
      return PrepareError::kTimeout;
  }
}

// Whether the volume `path` is on can swap two names in one rename
// (RENAME_SWAP). True when it doesn't say.
bool SupportsRenameSwap(const base::FilePath& path) {
  struct statfs volume;
  if (statfs(path.value().c_str(), &volume) != 0) {
    return true;
  }
  struct attrlist request = {};
  request.bitmapcount = ATTR_BIT_MAP_COUNT;
  request.volattr = ATTR_VOL_INFO | ATTR_VOL_CAPABILITIES;
  struct {
    uint32_t length;
    vol_capabilities_attr_t capabilities;
  } reply = {};
  // Volume attributes are asked of the volume's root.
  if (getattrlist(volume.f_mntonname, &request, &reply, sizeof(reply), 0) !=
      0) {
    PLOG(WARNING) << "getattrlist " << volume.f_mntonname;
    return true;
  }
  const vol_capabilities_attr_t capabilities = reply.capabilities;
  const uint32_t valid = capabilities.valid[VOL_CAPABILITIES_INTERFACES];
  const uint32_t supported =
      capabilities.capabilities[VOL_CAPABILITIES_INTERFACES];
  return !(valid & VOL_CAP_INT_RENAME_SWAP) ||
         (supported & VOL_CAP_INT_RENAME_SWAP);
}

// The first app at the top of the mounted image (mac_package.sh puts Koan.app
// and a link to /Applications there).
base::FilePath FindApp(const base::FilePath& volume) {
  base::FileEnumerator apps(volume, /*recursive=*/false,
                            base::FileEnumerator::DIRECTORIES,
                            FILE_PATH_LITERAL("*.app"));
  return apps.Next();
}

// Removes the quarantine attribute from `root` and everything in it,
// symbolic links themselves included (not their targets).
bool RemoveQuarantine(const base::FilePath& root) {
  auto remove = [](const base::FilePath& path) {
    if (removexattr(path.value().c_str(), kQuarantineAttribute,
                    XATTR_NOFOLLOW) == 0 ||
        errno == ENOATTR) {
      return true;
    }
    PLOG(ERROR) << "removexattr " << path;
    return false;
  };
  bool ok = remove(root);
  base::FileEnumerator files(root, /*recursive=*/true,
                             base::FileEnumerator::FILES |
                                 base::FileEnumerator::DIRECTORIES |
                                 base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    ok = remove(path) && ok;
  }
  return ok;
}

// The mount points of the images `hdiutil info` lists that IsUpdateImage()
// says are Koan's downloads.
std::vector<std::string> LeftoverMountPoints(
    const base::FilePath& updates_dir) {
  std::vector<std::string> mount_points;
  std::string output;
  if (RunTool({kHdiutil, "info", "-plist"}, kInfoTimeout, &output) !=
      ToolResult::kOk) {
    return mount_points;
  }
  // Anything hdiutil wrote to stderr comes first.
  const size_t start = output.find("<?xml");
  if (start == std::string::npos) {
    return mount_points;
  }
  const base::span<const uint8_t> plist =
      base::as_byte_span(output).subspan(start);
  NSData* data = [NSData dataWithBytes:plist.data() length:plist.size()];
  NSDictionary* info = base::apple::ObjCCast<NSDictionary>(
      [NSPropertyListSerialization propertyListWithData:data
                                                options:NSPropertyListImmutable
                                                 format:nil
                                                  error:nil]);
  const base::FilePath resolved = base::MakeAbsoluteFilePath(updates_dir);
  for (id image in base::apple::ObjCCast<NSArray>(info[@"images"])) {
    NSDictionary* image_info = base::apple::ObjCCast<NSDictionary>(image);
    NSString* image_path =
        base::apple::ObjCCast<NSString>(image_info[@"image-path"]);
    if (!image_path) {
      continue;
    }
    const base::FilePath path(base::SysNSStringToUTF8(image_path));
    if (!IsUpdateImage(path, updates_dir) &&
        (resolved.empty() || !IsUpdateImage(path, resolved))) {
      continue;
    }
    for (id entity in base::apple::ObjCCast<NSArray>(
             image_info[@"system-entities"])) {
      NSString* mount_point = base::apple::ObjCCast<NSString>(
          base::apple::ObjCCast<NSDictionary>(entity)[@"mount-point"]);
      if (mount_point.length) {
        mount_points.push_back(base::SysNSStringToUTF8(mount_point));
      }
    }
  }
  return mount_points;
}

class MacInstallPlatform : public InstallPlatform {
 public:
  MacInstallPlatform() = default;
  MacInstallPlatform(const MacInstallPlatform&) = delete;
  MacInstallPlatform& operator=(const MacInstallPlatform&) = delete;
  ~MacInstallPlatform() override = default;

  InstallLocation Inspect(const base::FilePath& updates_dir) override {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    InstallLocation location;
    location.app = base::apple::OuterBundlePath();
    const std::string& app = location.app.value();
    // Gatekeeper's App Translocation runs a quarantined app that wasn't
    // moved from a read-only, randomised path.
    location.translocated = app.find("/AppTranslocation/") != std::string::npos;
    struct statfs volume;
    location.read_only =
        statfs(app.c_str(), &volume) == 0 && (volume.f_flags & MNT_RDONLY);
    // The swap renames the app's folder entry and, moving it to another
    // folder, the app's own "..": both must be writable.
    const base::FilePath parent = location.app.DirName();
    location.writable = access(parent.value().c_str(), W_OK) == 0 &&
                        access(app.c_str(), W_OK) == 0;
    struct stat parent_stat;
    struct stat updates_stat;
    location.updates_on_app_volume =
        base::CreateDirectory(updates_dir) &&
        stat(parent.value().c_str(), &parent_stat) == 0 &&
        stat(updates_dir.value().c_str(), &updates_stat) == 0 &&
        parent_stat.st_dev == updates_stat.st_dev;
    // The copy is always staged on the app's volume (StagingDir()).
    location.rename_swap = SupportsRenameSwap(parent);
    return location;
  }

  void DetachLeftoverImages(const base::FilePath& updates_dir) override {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    for (const std::string& mount_point : LeftoverMountPoints(updates_dir)) {
      LOG(WARNING) << "Detaching " << mount_point
                   << ", left attached by an update";
      std::string output;
      RunTool({kHdiutil, "detach", "-force", mount_point}, kDetachTimeout,
              &output);
    }
  }

  bool OtherInstanceRunning() override {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    NSString* executable = base::apple::OuterBundle().executablePath;
    if (!executable) {
      return false;
    }
    const base::FilePath resolved = base::MakeAbsoluteFilePath(
        base::FilePath(base::SysNSStringToUTF8(executable)));
    for (const RunningProcess& process : ListProcesses()) {
      if (process.pid != getpid() && process.path == resolved.value()) {
        LOG(WARNING) << "Another Koan runs from the app: " << process.pid;
        return true;
      }
    }
    return false;
  }

  std::string BundleId() override {
    return base::SysNSStringToUTF8(base::apple::OuterBundle().bundleIdentifier);
  }

  PrepareResult Prepare(const PrepareRequest& request) override {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    PrepareResult result;
    base::ScopedTempDir mount_point;
    std::string output;
    if (!mount_point.CreateUniqueTempDir()) {
      result.error = PrepareError::kMount;
      return result;
    }
    result.error = ToolError(
        RunTool({kHdiutil, "attach", "-nobrowse", "-readonly", "-noautoopen",
                 "-mountpoint", mount_point.GetPath().value(),
                 request.disk_image.value()},
                kAttachTimeout, &output),
        PrepareError::kMount);
    if (result.error) {
      // A stuck attach may still have mounted it: the next start detaches
      // it (DetachLeftoverImages()).
      result.detail = output;
      return result;
    }
    result.staged_app = request.staging_dir.Append(request.app.BaseName());
    const base::FilePath source = FindApp(mount_point.GetPath());
    if (source.empty()) {
      result.error = PrepareError::kNoApp;
    } else {
      // --noqtn: don't carry the image's quarantine over (the attribute is
      // removed below anyway, as Koan's own writes get it too).
      result.error = ToolError(RunTool({kDitto, "--noqtn", source.value(),
                                        result.staged_app.value()},
                                       kCopyTimeout, &output),
                               PrepareError::kCopy);
    }
    std::string detach_output;
    if (RunTool({kHdiutil, "detach", mount_point.GetPath().value()},
                kDetachTimeout, &detach_output) != ToolResult::kOk) {
      RunTool({kHdiutil, "detach", "-force", mount_point.GetPath().value()},
              kDetachTimeout, &detach_output);
    }
    if (result.error) {
      result.detail = output;
      return result;
    }
    if (!RemoveQuarantine(result.staged_app)) {
      result.error = PrepareError::kQuarantine;
      return result;
    }
    PrepareResult checked = Verify(result.staged_app, request);
    checked.staged_app = result.staged_app;
    return checked;
  }

  PrepareResult Verify(const base::FilePath& staged_app,
                       const PrepareRequest& request) override {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    PrepareResult result;
    result.staged_app = staged_app;
    // The signature first: it seals the Info.plist read next. -R pins the
    // certificate; the designated requirement's text isn't compared (the
    // release certificate's reads `certificate root`, #458).
    std::string output;
    result.error = ToolError(
        VerifyCodeSignature(staged_app, request.code_requirement, &output),
        PrepareError::kSignature);
    if (result.error) {
      result.detail = output;
      return result;
    }
    NSDictionary* plist = [NSDictionary
        dictionaryWithContentsOfURL:base::apple::FilePathToNSURL(
                                        staged_app.Append("Contents")
                                            .Append("Info.plist"))
                              error:nil];
    StagedBundle staged;
    staged.bundle_id = base::SysNSStringToUTF8(
        base::apple::ObjCCast<NSString>(plist[@"CFBundleIdentifier"]));
    staged.koan_version = base::SysNSStringToUTF8(
        base::apple::ObjCCast<NSString>(plist[@"KoanVersion"]));
    result.error = CheckStagedBundle(staged, BundleId(), request.version,
                                     request.running_version);
    if (result.error) {
      result.detail = staged.bundle_id + " " + staged.koan_version;
    }
    return result;
  }

  bool StartHelper(const HelperRequest& request) override {
    std::vector<std::string> argv = {
        HelperPathIn(base::apple::OuterBundlePath()).value(),
        std::string("--") + kHelperPidSwitch + "=" +
            base::NumberToString(getpid()),
        std::string("--") + kHelperMarkerSwitch + "=" + request.marker.value(),
    };
    if (request.relaunch) {
      argv.push_back(std::string("--") + kHelperRelaunchSwitch);
    }
    if (request.roll_back) {
      argv.push_back(std::string("--") + kHelperRollBackSwitch);
    }
    // A test hook build's requirement (the helper takes it only in one).
    if (const std::optional<std::string> requirement =
            CodeRequirementSwitchValue(request.code_requirement)) {
      argv.push_back(std::string("--") + kCodeRequirementSwitch + "=" +
                     *requirement);
    }
    // The new app's arguments, after the end of the helper's switches.
    argv.push_back("--");
    argv.insert(argv.end(), request.relaunch_args.begin(),
                request.relaunch_args.end());
    base::LaunchOptions options;
    // Its own process group: nothing sent to Koan's reaches it.
    options.new_process_group = true;
    if (!base::LaunchProcess(argv, options).IsValid()) {
      LOG(ERROR) << "Couldn't start " << argv[0];
      return false;
    }
    return true;
  }
};

}  // namespace

std::unique_ptr<InstallPlatform> CreateInstallPlatform() {
  return std::make_unique<MacInstallPlatform>();
}

ToolResult RunTool(const std::vector<std::string>& argv,
                   base::TimeDelta timeout,
                   std::string* output) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  output->clear();
  // The output goes to an unlinked file: a pipe nobody reads while waiting
  // could fill up and stall the tool.
  base::FilePath temp_path;
  base::ScopedFILE temp = base::CreateAndOpenTemporaryStream(&temp_path);
  if (!temp) {
    PLOG(ERROR) << "No temporary file for " << argv[0];
    return ToolResult::kFailed;
  }
  base::DeleteFile(temp_path);
  const int fd = fileno(temp.get());
  base::LaunchOptions options;
  options.fds_to_remap = {{fd, STDOUT_FILENO}, {fd, STDERR_FILENO}};
  const base::Process process = base::LaunchProcess(argv, options);
  if (!process.IsValid()) {
    LOG(ERROR) << "Couldn't run " << argv[0];
    return ToolResult::kFailed;
  }
  int exit_code = -1;
  ToolResult result = ToolResult::kFailed;
  if (process.WaitForExitWithTimeout(timeout, &exit_code)) {
    result = exit_code == 0 ? ToolResult::kOk : ToolResult::kFailed;
  } else {
    process.Terminate(/*exit_code=*/1, /*wait=*/true);
    result = ToolResult::kTimedOut;
  }
  rewind(temp.get());
  base::ReadStreamToStringWithMaxSize(temp.get(), kMaxToolOutput, output);
  if (result != ToolResult::kOk) {
    LOG(ERROR) << argv[0] << " "
               << (result == ToolResult::kTimedOut ? "timed out" : "failed")
               << " (" << exit_code << "): " << *output;
  }
  return result;
}

ToolResult VerifyCodeSignature(const base::FilePath& app,
                               const std::string& requirement,
                               std::string* output) {
  // -R pins the certificate; the designated requirement's text isn't
  // compared (the release certificate's reads `certificate root`, #458).
  return RunTool({kCodesign, "--verify", "--deep", "--strict", "-R",
                  requirement, app.value()},
                 kCodesignTimeout, output);
}

ToolResult DisplayCodeRequirement(const base::FilePath& app,
                                  std::string* output) {
  return RunTool({kCodesign, "-d", "-r-", app.value()}, kCodesignTimeout,
                 output);
}

std::vector<RunningProcess> ListProcesses() {
  std::vector<RunningProcess> processes;
  const int count = proc_listallpids(nullptr, 0);
  if (count <= 0) {
    return processes;
  }
  std::vector<pid_t> pids(static_cast<size_t>(count) + 64);
  const int listed = proc_listallpids(
      pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
  if (listed <= 0) {
    return processes;
  }
  pids.resize(std::min(pids.size(), static_cast<size_t>(listed)));
  std::array<char, PROC_PIDPATHINFO_MAXSIZE> path;
  for (pid_t pid : pids) {
    const int length =
        proc_pidpath(pid, path.data(), static_cast<uint32_t>(path.size()));
    if (length > 0) {
      processes.push_back(
          {pid, std::string(path.data(), static_cast<size_t>(length))});
    }
  }
  return processes;
}

}  // namespace koan::updates
