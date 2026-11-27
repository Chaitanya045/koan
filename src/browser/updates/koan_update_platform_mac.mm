// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_platform.h"

#import <Foundation/Foundation.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/scoped_blocking_call.h"

namespace koan::updates {

namespace {

constexpr char kHdiutil[] = "/usr/bin/hdiutil";
constexpr char kDitto[] = "/usr/bin/ditto";
constexpr char kCodesign[] = "/usr/bin/codesign";
// LSFileQuarantineEnabled (Chromium's Info.plist) quarantines every file
// Koan writes, the download and what is copied from it included.
constexpr char kQuarantineAttribute[] = "com.apple.quarantine";

bool Run(const std::vector<std::string>& argv, std::string* output) {
  const bool ok = base::GetAppOutputAndError(argv, output);
  if (!ok) {
    LOG(ERROR) << argv[0] << " failed: " << *output;
  }
  return ok;
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
    return location;
  }

  std::string BundleId() override {
    return base::SysNSStringToUTF8(base::apple::OuterBundle().bundleIdentifier);
  }

  PrepareResult Prepare(const PrepareRequest& request) override {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    PrepareResult result;
    base::ScopedTempDir mount_point;
    std::string output;
    if (!mount_point.CreateUniqueTempDir() ||
        !Run({kHdiutil, "attach", "-nobrowse", "-readonly", "-noautoopen",
              "-mountpoint", mount_point.GetPath().value(),
              request.disk_image.value()},
             &output)) {
      result.error = PrepareError::kMount;
      result.detail = output;
      return result;
    }
    result.staged_app = request.staging_dir.Append(request.app.BaseName());
    const base::FilePath source = FindApp(mount_point.GetPath());
    bool copied = false;
    if (!source.empty()) {
      // --noqtn: don't carry the image's quarantine over (the attribute is
      // removed below anyway, as Koan's own writes get it too).
      copied =
          Run({kDitto, "--noqtn", source.value(), result.staged_app.value()},
              &output);
    }
    std::string detach_output;
    if (!Run({kHdiutil, "detach", mount_point.GetPath().value()},
             &detach_output)) {
      Run({kHdiutil, "detach", "-force", mount_point.GetPath().value()},
          &detach_output);
    }
    if (source.empty() || !copied) {
      result.error =
          source.empty() ? PrepareError::kNoApp : PrepareError::kCopy;
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
    if (!Run({kCodesign, "--verify", "--deep", "--strict", "-R",
              request.code_requirement, staged_app.value()},
             &output)) {
      result.error = PrepareError::kSignature;
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
        base::apple::OuterBundlePath()
            .Append("Contents")
            .Append("Resources")
            .Append("bin")
            .Append(kHelperName)
            .value(),
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

}  // namespace koan::updates
