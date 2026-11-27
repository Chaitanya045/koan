// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_IMPORT_SOURCES_H_
#define KOAN_BROWSER_IMPORTER_KOAN_IMPORT_SOURCES_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "build/build_config.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_type.h"

// The browsers Koan imports from besides Chromium's own list (Safari, Firefox
// and a bookmarks file on macOS; #348): the Firefox-based ones (Zen first)
// through Chromium's Firefox importer, and the Chromium-based ones (Arc,
// Chrome...), which Chromium 158 has no importer for on macOS, through
// koan_chromium_profile_importer.h. Found under ~/Library/Application
// Support on macOS, under the home folder on Linux (#493: native and Flatpak
// installs, and Firefox too), or the --koan-import-test-root folder in
// tests. Listed first in
// Chromium's ImporterList (the import dialog, the welcome's import step),
// the most recently used browser first, as Zen's welcome lists them.
namespace koan::importer {

// SourceProfile::importer_type of a Chromium-format profile. Not one of
// upstream's ImporterType values on any platform (7: the enum's range, -1 to
// 7, holds it); such a source never reaches the utility process, whose IPC
// traits allow only upstream's values (external_process_importer_host.cc
// hook). docs/REBASE.md: re-check that importer_type.h has no 7.
inline constexpr auto kChromiumImporterType =
    static_cast<user_data_importer::ImporterType>(7);

// Koan's "Spaces and pinned tabs" item (#363; Zen's ZEN_SPACES migration
// resource): a bit of SourceProfile::services_supported and of the items an
// import asks for, past upstream's ImportItem bits. Never an ImportItem (the
// enum's range ends at ALL) and never sent to the utility process
// (koan_chromium_profile_importer.h takes it out).
inline constexpr uint16_t kSpacesImportItem = 1 << 7;

enum class Engine {
  // A Firefox profile layout (profiles.ini, places.sqlite).
  kGecko,
  // A Chromium "User Data" folder (Local State, <profile>/Bookmarks,
  // History, Login Data).
  kChromium,
};

struct KnownBrowser {
  // The name listed. A browser in several folders (on Linux, its native and
  // its Flatpak install) has a row for each, listed as one.
  std::u16string_view name;
  Engine engine;
  // Where its data is, under the search root (SearchRoot()): profiles.ini's
  // folder (Gecko) or the "User Data" folder (Chromium).
  std::string_view data_dir;
  // Its app, for the icon listed: its bundle id on macOS, its Flatpak app id
  // on Linux (also the icon name its Flatpak installs).
  std::string_view bundle_id;
  // Linux: the other names its icon has in the icon theme (its native
  // packages'), space-separated.
  std::string_view icon_names;
  // Chromium: the "<Browser> Safe Storage" item whose secret encrypts its
  // saved passwords: the keychain item (macOS), its KWallet entry (Linux).
  std::string_view keychain_service;
  std::string_view keychain_account;
  // Linux, Chromium: its Secret Service item's "application" attribute and
  // its KWallet folder (freedesktop_secret_key_provider.h in the browser).
  std::string_view secret_application;
  std::string_view kwallet_folder;
  // Arc: its Spaces and pinned tabs are in StorableSidebar.json, beside
  // "User Data".
  bool arc_sidebar = false;
  // Zen: its Spaces and pinned tabs are in the profile's session file
  // (koan_imported_spaces.h).
  bool zen_sessions = false;
};

// Every browser Koan looks for, in the order listed when none was used.
base::span<const KnownBrowser> KnownBrowsers();

// The folder the browsers' data folders are in: --koan-import-test-root, or
// ~/Library/Application Support on macOS, the home folder on Linux (in
// Koan's Flatpak too: its finish-args let it read exactly those folders).
base::FilePath SearchRoot();
// Whether --koan-import-test-root is set.
bool HasTestRoot();

// Reads `path` into `contents` if it is a regular file of at most
// `max_size` bytes. Another browser's folder may hold anything: a FIFO or a
// device would block the detection (and the welcome waiting on it, #493).
bool ReadRegularFile(const base::FilePath& path,
                     size_t max_size,
                     std::string& contents);

// The known browser whose data folder under `root` holds `profile_dir`, or
// null.
const KnownBrowser* FindBrowser(const base::FilePath& root,
                                const base::FilePath& profile_dir);

// Whether `source` is a Chromium-format profile Koan imports itself.
bool IsChromiumProfileSource(const user_data_importer::SourceProfile& source);

// The bundle id (Linux: Flatpak app id) of the app `source` is from (Koan's
// browsers, Safari, Firefox), for its icon; empty for a bookmarks file.
std::string_view BundleIdOf(const user_data_importer::SourceProfile& source);

// A profile folder and its name.
struct ProfileDir {
  base::FilePath path;
  std::u16string name;
};

// The profiles a Firefox-style profiles.ini (`ini`, in folder `ini_dir`)
// lists, the default first: the one an [Install...] section names, else the
// one with Default=1; then in the file's order.
std::vector<ProfileDir> ParseProfilesIni(const std::string& ini,
                                         const base::FilePath& ini_dir);

// The profiles a Chromium "Local State" file (`json`, in `user_data_dir`)
// knows, with their names: the last used first, then in profiles_order.
// Without a profile list, "Default".
std::vector<ProfileDir> ParseLocalState(std::string_view json,
                                        const base::FilePath& user_data_dir);

// The sources found under `root`: one per profile with data (Gecko: a
// places.sqlite; Chromium: Bookmarks or History), each browser's in its
// profile order, the browsers most recently used first. A browser's only
// profile has no name (as Chromium lists Firefox's). Zen's profiles with a
// session file and Arc's with a StorableSidebar.json offer
// kSpacesImportItem.
std::vector<user_data_importer::SourceProfile> DetectSourceProfiles(
    const base::FilePath& root,
    const std::string& locale);

// The hook in Chromium's ImporterList (importer_list.cc), on its detection
// thread: puts Koan's sources before upstream's (without upstream's copy of
// a profile Koan lists too: Firefox's on Linux), then the source the
// welcome's import step picked (SetPreferredSource) first of all, so the
// import dialog opens on it. On Linux, loads the sources' icons.
void AddSourceProfiles(
    const std::string& locale,
    std::vector<user_data_importer::SourceProfile>& profiles);

// The source the welcome's import step picked, until another is picked.
void SetPreferredSource(const user_data_importer::SourceProfile& source);
// Moves the preferred source, if `profiles` has it, to the front.
void MovePreferredSourceFirst(
    std::vector<user_data_importer::SourceProfile>& profiles);

#if BUILDFLAG(IS_LINUX)
// Linux (koan_import_icons_linux.cc): the folders apps' icons are under
// (the XDG data folders, the Flatpak exports, Flatpak's view of the host's
// under /run/host), the user's first; with --koan-import-test-root, its
// .local/share only.
std::vector<base::FilePath> AppIconDataDirs();

// The icon file of an app with icon theme names `names` (the first
// preferred) under the data folders `data_dirs`: hicolor's PNG nearest
// 64 px, else its SVG, else a pixmap; empty if none.
base::FilePath FindAppIcon(base::span<const base::FilePath> data_dirs,
                           base::span<const std::string> names);

// The icon names of the app with `bundle_id`: the id, then a known
// browser's other names.
std::vector<std::string> AppIconNames(std::string_view bundle_id);

// Finds the icons of `sources`' apps (blocking file IO, on the detection
// thread), for LoadedAppIconDataUrl().
void LoadAppIcons(base::span<const user_data_importer::SourceProfile> sources);
// The icon LoadAppIcons() found for `bundle_id`, as a data: URL; empty if
// none.
std::string LoadedAppIconDataUrl(std::string_view bundle_id);
#endif

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_IMPORT_SOURCES_H_
