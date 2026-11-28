// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The icons of the browsers Koan imports from, on Linux (#493): from the
// icon theme the apps install into (hicolor), as their desktop entries name
// them. In Koan's Flatpak the host's icons are under /run/host (Flatpak
// shows them there) and the Flatpak apps' in their exports folders
// (flatpak/dev.koan.Koan.yml lets Koan read those, read-only).

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/nix/xdg_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "koan/browser/importer/koan_import_sources.h"

namespace koan::importer {

namespace {

// hicolor's sizes, the nearest to the 64 px tile first.
constexpr const char* kIconSizes[] = {"64x64",   "128x128", "96x96", "48x48",
                                      "256x256", "512x512", "32x32"};

// An icon is small; anything bigger isn't one.
constexpr size_t kMaxIconBytes = 1024 * 1024;

struct LoadedIcons {
  base::Lock lock;
  std::map<std::string, std::string, std::less<>> data_urls GUARDED_BY(lock);
};

LoadedIcons& GetLoadedIcons() {
  static base::NoDestructor<LoadedIcons> icons;
  return *icons;
}

void AddDir(std::vector<base::FilePath>& dirs, const base::FilePath& dir) {
  if (!dir.empty() && !std::ranges::contains(dirs, dir)) {
    dirs.push_back(dir);
  }
}

// The icon at `file` as a data: URL, or empty.
std::string DataUrl(const base::FilePath& file) {
  std::string contents;
  if (file.empty() || !ReadRegularFile(file, kMaxIconBytes, contents)) {
    return std::string();
  }
  return base::StrCat(
      {file.Extension() == ".svg" ? "data:image/svg+xml" : "data:image/png",
       ";base64,", base::Base64Encode(contents)});
}

}  // namespace

std::vector<base::FilePath> AppIconDataDirs() {
  std::vector<base::FilePath> dirs;
  if (HasTestRoot()) {
    // Tests: the test root's only.
    dirs.push_back(SearchRoot().Append(".local/share"));
    return dirs;
  }
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  AddDir(dirs, base::nix::GetXDGDirectory(env.get(), "XDG_DATA_HOME",
                                          ".local/share"));
  if (!home.empty()) {
    AddDir(dirs, home.Append(".local/share"));
    AddDir(dirs, home.Append(".local/share/flatpak/exports/share"));
  }
  AddDir(dirs, base::FilePath("/var/lib/flatpak/exports/share"));
  // The host's, in Koan's Flatpak.
  AddDir(dirs, base::FilePath("/run/host/user-share"));
  AddDir(dirs, base::FilePath("/run/host/share"));
  const std::string data_dirs =
      env->GetVar("XDG_DATA_DIRS").value_or("/usr/local/share:/usr/share");
  for (std::string_view dir : base::SplitStringPiece(
           data_dirs, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    AddDir(dirs, base::FilePath(dir));
  }
  return dirs;
}

base::FilePath FindAppIcon(base::span<const base::FilePath> data_dirs,
                           base::span<const std::string> names) {
  for (const std::string& name : names) {
    // A name only, never a path.
    if (name.empty() || name.find('/') != std::string::npos ||
        name.starts_with(".")) {
      continue;
    }
    for (const base::FilePath& dir : data_dirs) {
      const base::FilePath hicolor = dir.Append("icons/hicolor");
      for (const char* size : kIconSizes) {
        const base::FilePath png =
            hicolor.Append(size).Append("apps").Append(name + ".png");
        if (base::PathExists(png)) {
          return png;
        }
      }
      const base::FilePath svg =
          hicolor.Append("scalable/apps").Append(name + ".svg");
      if (base::PathExists(svg)) {
        return svg;
      }
    }
    for (const base::FilePath& dir : data_dirs) {
      for (const char* extension : {".png", ".svg"}) {
        const base::FilePath pixmap =
            dir.Append("pixmaps").Append(name + extension);
        if (base::PathExists(pixmap)) {
          return pixmap;
        }
      }
    }
  }
  return base::FilePath();
}

std::vector<std::string> AppIconNames(std::string_view bundle_id) {
  std::vector<std::string> names = {std::string(bundle_id)};
  const auto it =
      std::ranges::find(KnownBrowsers(), bundle_id, &KnownBrowser::bundle_id);
  if (it != KnownBrowsers().end()) {
    for (std::string_view name :
         base::SplitStringPiece(it->icon_names, " ", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      names.emplace_back(name);
    }
  }
  return names;
}

void LoadAppIcons(base::span<const user_data_importer::SourceProfile> sources) {
  const std::vector<base::FilePath> dirs = AppIconDataDirs();
  std::map<std::string, std::string, std::less<>> data_urls;
  for (const user_data_importer::SourceProfile& source : sources) {
    const std::string_view bundle_id = BundleIdOf(source);
    if (bundle_id.empty() || data_urls.contains(bundle_id)) {
      continue;
    }
    data_urls.emplace(bundle_id,
                      DataUrl(FindAppIcon(dirs, AppIconNames(bundle_id))));
  }
  LoadedIcons& icons = GetLoadedIcons();
  base::AutoLock lock(icons.lock);
  icons.data_urls = std::move(data_urls);
}

std::string LoadedAppIconDataUrl(std::string_view bundle_id) {
  LoadedIcons& icons = GetLoadedIcons();
  base::AutoLock lock(icons.lock);
  const auto it = icons.data_urls.find(bundle_id);
  return it == icons.data_urls.end() ? std::string() : it->second;
}

}  // namespace koan::importer
