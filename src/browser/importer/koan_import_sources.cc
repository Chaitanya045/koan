// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_import_sources.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <optional>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/common/ini_parser.h"
#include "koan/browser/importer/koan_imported_spaces.h"
#include "koan/browser/koan_switches.h"

namespace koan::importer {

namespace {

using user_data_importer::SourceProfile;

// The enum's range must still hold kChromiumImporterType (its largest value
// sets the range), and no upstream value may be it.
static_assert(user_data_importer::TYPE_BOOKMARKS_FILE >= 4);
static_assert(user_data_importer::TYPE_FIREFOX != kChromiumImporterType &&
              user_data_importer::TYPE_BOOKMARKS_FILE != kChromiumImporterType);
// Koan's Spaces item is none of upstream's.
static_assert(user_data_importer::ALL < kSpacesImportItem);

// Zen first: Koan is for people leaving Zen and Arc (#348). Data folders and
// keychain items as the browsers' own (Zen's ChromeMigrationUtils patch for
// Arc and Dia).
constexpr KnownBrowser kBrowsers[] = {
    {.name = u"Zen",
     .engine = Engine::kGecko,
     .data_dir = "zen",
     .bundle_id = "app.zen-browser.zen",
     .zen_sessions = true},
    {.name = u"Arc",
     .engine = Engine::kChromium,
     .data_dir = "Arc/User Data",
     .bundle_id = "company.thebrowser.Browser",
     .keychain_service = "Arc Safe Storage",
     .keychain_account = "Arc",
     .arc_sidebar = true},
    {.name = u"Dia",
     .engine = Engine::kChromium,
     .data_dir = "Dia/User Data",
     .bundle_id = "company.thebrowser.dia",
     .keychain_service = "Dia Safe Storage",
     .keychain_account = "Dia"},
    {.name = u"Google Chrome",
     .engine = Engine::kChromium,
     .data_dir = "Google/Chrome",
     .bundle_id = "com.google.Chrome",
     .keychain_service = "Chrome Safe Storage",
     .keychain_account = "Chrome"},
    {.name = u"Brave",
     .engine = Engine::kChromium,
     .data_dir = "BraveSoftware/Brave-Browser",
     .bundle_id = "com.brave.Browser",
     .keychain_service = "Brave Safe Storage",
     .keychain_account = "Brave"},
    {.name = u"Microsoft Edge",
     .engine = Engine::kChromium,
     .data_dir = "Microsoft Edge",
     .bundle_id = "com.microsoft.edgemac",
     .keychain_service = "Microsoft Edge Safe Storage",
     .keychain_account = "Microsoft Edge"},
    {.name = u"Vivaldi",
     .engine = Engine::kChromium,
     .data_dir = "Vivaldi",
     .bundle_id = "com.vivaldi.Vivaldi",
     .keychain_service = "Vivaldi Safe Storage",
     .keychain_account = "Vivaldi"},
    {.name = u"Chromium",
     .engine = Engine::kChromium,
     .data_dir = "Chromium",
     .bundle_id = "org.chromium.Chromium",
     .keychain_service = "Chromium Safe Storage",
     .keychain_account = "Chromium"},
    {.name = u"Floorp",
     .engine = Engine::kGecko,
     .data_dir = "Floorp",
     .bundle_id = "one.ablaze.floorp"},
    {.name = u"LibreWolf",
     .engine = Engine::kGecko,
     .data_dir = "librewolf",
     .bundle_id = "org.librewolf.librewolf"},
    {.name = u"Waterfox",
     .engine = Engine::kGecko,
     .data_dir = "Waterfox",
     .bundle_id = "net.waterfox.waterfox"},
};

// What Chromium's Firefox importer brings on macOS (importer_list.cc: no
// passwords there, NSS can't be loaded).
constexpr uint16_t kGeckoServices = user_data_importer::HISTORY |
                                    user_data_importer::FAVORITES |
                                    user_data_importer::AUTOFILL_FORM_DATA;

uint16_t ChromiumServices() {
  uint16_t services =
      user_data_importer::HISTORY | user_data_importer::FAVORITES;
  // Saved passwords need the browser's Safe Storage key from the keychain
  // (macOS), or a test's key file.
#if BUILDFLAG(IS_MAC)
  services |= user_data_importer::PASSWORDS;
#else
  if (HasTestRoot()) {
    services |= user_data_importer::PASSWORDS;
  }
#endif
  return services;
}

base::Time LastModified(const base::FilePath& path) {
  base::File::Info info;
  return base::GetFileInfo(path, &info) ? info.last_modified : base::Time();
}

// A profile folder's data file whose time says when it was last used, or
// empty if it has no data to import.
base::FilePath DataFileOf(Engine engine, const base::FilePath& profile_dir) {
  static constexpr const char* kGeckoFiles[] = {"places.sqlite"};
  static constexpr const char* kChromiumFiles[] = {"History", "Bookmarks"};
  const base::span<const char* const> files =
      engine == Engine::kGecko ? base::span<const char* const>(kGeckoFiles)
                               : base::span<const char* const>(kChromiumFiles);
  for (const char* file : files) {
    base::FilePath path = profile_dir.AppendASCII(file);
    if (base::PathExists(path)) {
      return path;
    }
  }
  return base::FilePath();
}

struct Preferred {
  base::Lock lock;
  std::optional<std::pair<user_data_importer::ImporterType, base::FilePath>>
      source GUARDED_BY(lock);
};

Preferred& GetPreferred() {
  static base::NoDestructor<Preferred> preferred;
  return *preferred;
}

}  // namespace

base::span<const KnownBrowser> KnownBrowsers() {
  return kBrowsers;
}

bool HasTestRoot() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kImportTestRoot);
}

base::FilePath SearchRoot() {
  if (HasTestRoot()) {
    return base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
        switches::kImportTestRoot);
  }
#if BUILDFLAG(IS_MAC)
  // ~/Library/Application Support (as firefox_importer_utils_mac.mm).
  base::FilePath app_data;
  if (base::PathService::Get(base::DIR_APP_DATA, &app_data)) {
    return app_data;
  }
#endif
  return base::FilePath();
}

const KnownBrowser* FindBrowser(const base::FilePath& root,
                                const base::FilePath& profile_dir) {
  if (root.empty()) {
    return nullptr;
  }
  for (const KnownBrowser& browser : kBrowsers) {
    const base::FilePath data_dir = root.AppendASCII(browser.data_dir);
    if (browser.engine == Engine::kGecko ? data_dir.IsParent(profile_dir)
                                         : profile_dir.DirName() == data_dir) {
      return &browser;
    }
  }
  return nullptr;
}

bool IsChromiumProfileSource(const SourceProfile& source) {
  return source.importer_type == kChromiumImporterType;
}

std::string_view BundleIdOf(const SourceProfile& source) {
#if BUILDFLAG(IS_MAC)
  if (source.importer_type == user_data_importer::TYPE_SAFARI) {
    return "com.apple.Safari";
  }
#endif
  if (source.importer_type == user_data_importer::TYPE_BOOKMARKS_FILE) {
    return std::string_view();
  }
  if (const KnownBrowser* browser =
          FindBrowser(SearchRoot(), source.source_path)) {
    return browser->bundle_id;
  }
  return source.importer_type == user_data_importer::TYPE_FIREFOX
             ? "org.mozilla.firefox"
             : std::string_view();
}

std::vector<ProfileDir> ParseProfilesIni(const std::string& ini,
                                         const base::FilePath& ini_dir) {
  DictionaryValueINIParser parser;
  parser.Parse(ini);

  // The default: the profile an [Install<hash>] section names (the one that
  // install uses), else the one marked Default=1.
  std::optional<std::string> install_default;
  std::optional<std::string> marked_default;
  std::vector<std::pair<int, const base::DictValue*>> sections;
  for (const auto [name, value] : parser.root()) {
    const base::DictValue* section = value.GetIfDict();
    if (!section) {
      continue;
    }
    int number = 0;
    if (name.starts_with("Install")) {
      if (const std::string* path = section->FindString("Default");
          path && !install_default) {
        install_default = *path;
      }
    } else if (name.starts_with("Profile") &&
               base::StringToInt(std::string_view(name).substr(7), &number) &&
               section->FindString("Path")) {
      sections.emplace_back(number, section);
    }
  }
  std::ranges::stable_sort(sections, {}, [](const auto& s) { return s.first; });

  std::vector<ProfileDir> profiles;
  for (const auto& [number, section] : sections) {
    const std::string& path = *section->FindString("Path");
    const std::string* relative = section->FindString("IsRelative");
    const std::string* name = section->FindString("Name");
    const std::string* is_default = section->FindString("Default");
    if (!marked_default && is_default && *is_default == "1") {
      marked_default = path;
    }
    ProfileDir profile;
    // IsRelative=1: under profiles.ini's folder; else an absolute path.
    profile.path = relative && *relative == "1"
                       ? ini_dir.Append(base::FilePath::FromUTF8Unsafe(path))
                       : base::FilePath::FromUTF8Unsafe(path);
    if (name) {
      profile.name = base::UTF8ToUTF16(*name);
    }
    profiles.push_back(std::move(profile));
  }

  const std::optional<std::string>& default_path =
      install_default ? install_default : marked_default;
  if (default_path) {
    const auto matches = [&](const auto& s) {
      return *s.second->FindString("Path") == *default_path;
    };
    if (const auto it = std::ranges::find_if(sections, matches);
        it != sections.end()) {
      const size_t index = static_cast<size_t>(it - sections.begin());
      std::rotate(profiles.begin(), profiles.begin() + index,
                  profiles.begin() + index + 1);
    }
  }
  return profiles;
}

std::vector<ProfileDir> ParseLocalState(std::string_view json,
                                        const base::FilePath& user_data_dir) {
  std::vector<std::string> order;
  // Kept for the whole function: `info_cache` points into it.
  const std::optional<base::DictValue> local_state =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::DictValue* info_cache = nullptr;
  if (local_state) {
    if (const base::DictValue* profile = local_state->FindDict("profile")) {
      info_cache = profile->FindDict("info_cache");
      if (const std::string* last_used = profile->FindString("last_used")) {
        order.push_back(*last_used);
      }
      if (const base::ListValue* profiles_order =
              profile->FindList("profiles_order")) {
        for (const base::Value& dir : *profiles_order) {
          if (dir.is_string()) {
            order.push_back(dir.GetString());
          }
        }
      }
    }
  }
  if (info_cache) {
    for (const auto [dir, value] : *info_cache) {
      order.push_back(dir);
    }
  }
  if (order.empty()) {
    order.push_back("Default");
  }

  std::vector<ProfileDir> profiles;
  for (const std::string& dir : order) {
    // A folder name only, never a path out of the user data folder.
    if (dir.empty() || dir.find('/') != std::string::npos || dir == "." ||
        dir == ".." || std::ranges::any_of(profiles, [&](const ProfileDir& p) {
          return p.path.BaseName().AsUTF8Unsafe() == dir;
        })) {
      continue;
    }
    ProfileDir profile;
    profile.path = user_data_dir.Append(base::FilePath::FromUTF8Unsafe(dir));
    if (const base::DictValue* info =
            info_cache ? info_cache->FindDict(dir) : nullptr) {
      if (const std::string* name = info->FindString("name")) {
        profile.name = base::UTF8ToUTF16(*name);
      }
    }
    profiles.push_back(std::move(profile));
  }
  return profiles;
}

std::vector<SourceProfile> DetectSourceProfiles(const base::FilePath& root,
                                                const std::string& locale) {
  if (root.empty()) {
    return {};
  }
  struct Found {
    base::Time last_used;
    std::vector<SourceProfile> sources;
  };
  std::vector<Found> found;
  for (const KnownBrowser& browser : kBrowsers) {
    const base::FilePath data_dir = root.AppendASCII(browser.data_dir);
    std::vector<ProfileDir> profiles;
    std::string contents;
    if (browser.engine == Engine::kGecko) {
      if (base::ReadFileToString(data_dir.AppendASCII("profiles.ini"),
                                 &contents)) {
        profiles = ParseProfilesIni(contents, data_dir);
      }
    } else if (base::DirectoryExists(data_dir)) {
      // Without a Local State (or unreadable), "Default".
      base::ReadFileToString(data_dir.AppendASCII("Local State"), &contents);
      profiles = ParseLocalState(contents, data_dir);
    }

    Found entry;
    for (ProfileDir& profile : profiles) {
      const base::FilePath data_file = DataFileOf(browser.engine, profile.path);
      if (data_file.empty()) {
        continue;
      }
      entry.last_used = std::max(entry.last_used, LastModified(data_file));
      SourceProfile source;
      source.importer_name = std::u16string(browser.name);
      source.profile = std::move(profile.name);
      source.source_path = profile.path;
      source.locale = locale;
      if (browser.engine == Engine::kGecko) {
        source.importer_type = user_data_importer::TYPE_FIREFOX;
        source.services_supported = kGeckoServices;
        if (browser.zen_sessions &&
            base::PathExists(
                source.source_path.AppendASCII(kZenSessionsFile))) {
          source.services_supported |= kSpacesImportItem;
        }
      } else {
        source.importer_type = kChromiumImporterType;
        source.services_supported = ChromiumServices();
        if (browser.arc_sidebar &&
            base::PathExists(data_dir.DirName().AppendASCII(kArcSidebarFile))) {
          source.services_supported |= kSpacesImportItem;
        }
      }
      entry.sources.push_back(std::move(source));
    }
    if (entry.sources.empty()) {
      continue;
    }
    if (entry.sources.size() == 1) {
      // One profile: no name to tell it from others.
      entry.sources[0].profile.clear();
    }
    if (browser.arc_sidebar) {
      // Arc keeps its Spaces and pinned tabs beside "User Data".
      entry.last_used = std::max(
          entry.last_used,
          LastModified(data_dir.DirName().AppendASCII(kArcSidebarFile)));
    }
    found.push_back(std::move(entry));
  }
  std::ranges::stable_sort(found, std::ranges::greater(), &Found::last_used);

  std::vector<SourceProfile> sources;
  for (Found& entry : found) {
    std::ranges::move(entry.sources, std::back_inserter(sources));
  }
  return sources;
}

void AddSourceProfiles(const std::string& locale,
                       std::vector<SourceProfile>& profiles) {
  std::vector<SourceProfile> koan = DetectSourceProfiles(SearchRoot(), locale);
  profiles.insert(profiles.begin(), std::make_move_iterator(koan.begin()),
                  std::make_move_iterator(koan.end()));
  MovePreferredSourceFirst(profiles);
}

void SetPreferredSource(const SourceProfile& source) {
  Preferred& preferred = GetPreferred();
  base::AutoLock lock(preferred.lock);
  preferred.source.emplace(source.importer_type, source.source_path);
}

void MovePreferredSourceFirst(std::vector<SourceProfile>& profiles) {
  std::optional<std::pair<user_data_importer::ImporterType, base::FilePath>>
      source;
  {
    Preferred& preferred = GetPreferred();
    base::AutoLock lock(preferred.lock);
    source = preferred.source;
  }
  if (!source) {
    return;
  }
  const auto it = std::ranges::find_if(profiles, [&](const SourceProfile& p) {
    return p.importer_type == source->first && p.source_path == source->second;
  });
  if (it != profiles.end()) {
    std::rotate(profiles.begin(), it, it + 1);
  }
}

}  // namespace koan::importer
