// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_import_sources.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_command_line.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "koan/browser/koan_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::importer {

namespace {

using user_data_importer::SourceProfile;

void Write(const base::FilePath& file, std::string_view contents) {
  ASSERT_TRUE(base::CreateDirectory(file.DirName()));
  ASSERT_TRUE(base::WriteFile(file, contents));
}

// Makes `file` last modified `days` ago.
void Age(const base::FilePath& file, int days) {
  const base::Time time = base::Time::Now() - base::Days(days);
  ASSERT_TRUE(base::TouchFile(file, time, time));
}

std::vector<std::u16string> Names(const std::vector<SourceProfile>& sources) {
  std::vector<std::u16string> names;
  for (const SourceProfile& source : sources) {
    names.push_back(source.importer_name +
                    (source.profile.empty() ? u"" : u"/" + source.profile));
  }
  return names;
}

}  // namespace

TEST(KoanImportSourcesTest, ProfilesIni) {
  const base::FilePath dir(FILE_PATH_LITERAL("/zen"));
  // An install's Default wins over Default=1; IsRelative=0 is absolute.
  std::vector<ProfileDir> profiles = ParseProfilesIni(
      "[Profile1]\nName=Default (release)\nIsRelative=1\n"
      "Path=Profiles/b.Default (release)\n\n"
      "[Profile0]\nName=default\nIsRelative=1\nPath=Profiles/a.default\n"
      "Default=1\n\n"
      "[Profile2]\nName=Elsewhere\nIsRelative=0\nPath=/Volumes/x/p\n\n"
      "[Install6F193CCC56814779]\nDefault=Profiles/b.Default (release)\n"
      "Locked=1\n\n[General]\nStartWithLastProfile=1\nVersion=2\n",
      dir);
  ASSERT_EQ(3u, profiles.size());
  EXPECT_EQ(dir.Append(FILE_PATH_LITERAL("Profiles/b.Default (release)")),
            profiles[0].path);
  EXPECT_EQ(u"Default (release)", profiles[0].name);
  EXPECT_EQ(dir.Append(FILE_PATH_LITERAL("Profiles/a.default")),
            profiles[1].path);
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("/Volumes/x/p")),
            profiles[2].path);

  // Without an install section, Default=1.
  profiles = ParseProfilesIni(
      "[Profile0]\nName=one\nIsRelative=1\nPath=Profiles/one\n"
      "[Profile1]\nName=two\nIsRelative=1\nPath=Profiles/two\nDefault=1\n",
      dir);
  ASSERT_EQ(2u, profiles.size());
  EXPECT_EQ(u"two", profiles[0].name);
  EXPECT_EQ(u"one", profiles[1].name);

  EXPECT_TRUE(ParseProfilesIni("", dir).empty());
  EXPECT_TRUE(ParseProfilesIni("[Profile0]\nName=no path\n", dir).empty());
}

TEST(KoanImportSourcesTest, LocalState) {
  const base::FilePath dir(FILE_PATH_LITERAL("/User Data"));
  std::vector<ProfileDir> profiles = ParseLocalState(
      R"({"profile": {"last_used": "Profile 2",
          "profiles_order": ["Default", "Profile 2", "../evil"],
          "info_cache": {"Default": {"name": "Personal"},
                         "Profile 2": {"name": "Work"},
                         "Profile 3": {"name": "Old"}}}})",
      dir);
  ASSERT_EQ(3u, profiles.size());
  EXPECT_EQ(dir.AppendASCII("Profile 2"), profiles[0].path);
  EXPECT_EQ(u"Work", profiles[0].name);
  EXPECT_EQ(dir.AppendASCII("Default"), profiles[1].path);
  EXPECT_EQ(u"Personal", profiles[1].name);
  EXPECT_EQ(dir.AppendASCII("Profile 3"), profiles[2].path);

  // No Local State: the default profile.
  profiles = ParseLocalState("", dir);
  ASSERT_EQ(1u, profiles.size());
  EXPECT_EQ(dir.AppendASCII("Default"), profiles[0].path);
  EXPECT_TRUE(profiles[0].name.empty());
}

#if BUILDFLAG(IS_MAC)
TEST(KoanImportSourcesTest, Detect) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath root = temp.GetPath();
  // Zen: two profiles, only one used (has places.sqlite).
  Write(root.AppendASCII("zen/profiles.ini"),
        "[Profile0]\nName=Default (release)\nIsRelative=1\n"
        "Path=Profiles/r.Default (release)\nDefault=1\n"
        "[Profile1]\nName=default\nIsRelative=1\nPath=Profiles/d.default\n");
  const base::FilePath zen =
      root.AppendASCII("zen/Profiles/r.Default (release)");
  Write(zen.AppendASCII("places.sqlite"), "");
  ASSERT_TRUE(
      base::CreateDirectory(root.AppendASCII("zen/Profiles/d.default")));
  Age(zen.AppendASCII("places.sqlite"), 1);
  // Chrome: two profiles, used longest ago.
  const base::FilePath chrome = root.AppendASCII("Google/Chrome");
  Write(chrome.AppendASCII("Local State"),
        R"({"profile": {"last_used": "Profile 1", "info_cache": {
            "Default": {"name": "Person 1"}, "Profile 1": {"name": "Work"}}}})");
  Write(chrome.AppendASCII("Default/History"), "");
  Write(chrome.AppendASCII("Profile 1/Bookmarks"), "{}");
  Age(chrome.AppendASCII("Default/History"), 5);
  Age(chrome.AppendASCII("Profile 1/Bookmarks"), 4);
  // Arc: no Local State, its sidebar touched today.
  Write(root.AppendASCII("Arc/User Data/Default/Bookmarks"), "{}");
  Age(root.AppendASCII("Arc/User Data/Default/Bookmarks"), 30);
  Write(root.AppendASCII("Arc/StorableSidebar.json"), "{}");
  // Brave: installed but never used (no profile data).
  ASSERT_TRUE(
      base::CreateDirectory(root.AppendASCII("BraveSoftware/Brave-Browser")));

  const std::vector<SourceProfile> sources =
      DetectSourceProfiles(root, "en-US");
  // The most recently used browser first; a browser's only profile unnamed.
  EXPECT_EQ((std::vector<std::u16string>{u"Arc", u"Zen", u"Google Chrome/Work",
                                         u"Google Chrome/Person 1"}),
            Names(sources));
  ASSERT_EQ(4u, sources.size());
  EXPECT_EQ(kChromiumImporterType, sources[0].importer_type);
  EXPECT_EQ(root.AppendASCII("Arc/User Data/Default"), sources[0].source_path);
  EXPECT_EQ(user_data_importer::TYPE_FIREFOX, sources[1].importer_type);
  EXPECT_EQ(zen, sources[1].source_path);
  EXPECT_EQ("en-US", sources[1].locale);
  // Chromium's Firefox importer on macOS: no passwords.
  EXPECT_EQ(user_data_importer::HISTORY | user_data_importer::FAVORITES |
                user_data_importer::AUTOFILL_FORM_DATA,
            sources[1].services_supported);
  EXPECT_EQ(chrome.AppendASCII("Profile 1"), sources[2].source_path);
  // Passwords need the keychain: macOS only (or a test root).
  EXPECT_EQ(user_data_importer::HISTORY | user_data_importer::FAVORITES |
                (BUILDFLAG(IS_MAC) ? user_data_importer::PASSWORDS : 0),
            sources[2].services_supported);

  // Spaces and pinned tabs: Arc's sidebar is there, Zen has no session file
  // yet, Chrome has no Spaces.
  EXPECT_TRUE(sources[0].services_supported & kSpacesImportItem);
  EXPECT_FALSE(sources[1].services_supported & kSpacesImportItem);
  EXPECT_FALSE(sources[2].services_supported & kSpacesImportItem);
  Write(zen.AppendASCII("zen-sessions.jsonlz4"), "");
  EXPECT_TRUE(DetectSourceProfiles(root, "en-US")[1].services_supported &
              kSpacesImportItem);

  const KnownBrowser* browser = FindBrowser(root, sources[2].source_path);
  ASSERT_TRUE(browser);
  EXPECT_EQ("Chrome Safe Storage", browser->keychain_service);
  EXPECT_EQ(u"Zen", FindBrowser(root, zen)->name);
  EXPECT_FALSE(FindBrowser(root, root.AppendASCII("Elsewhere/Default")));

  EXPECT_TRUE(DetectSourceProfiles(root.AppendASCII("none"), "").empty());
  EXPECT_TRUE(DetectSourceProfiles(base::FilePath(), "").empty());
}
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_LINUX)
// Linux (#493): under the home folder, native installs and Flatpak ones
// (~/.var/app/<app id>), Firefox-based ones in their XDG folder too; a
// browser in several folders listed once; no Arc or Dia.
TEST(KoanImportSourcesTest, DetectLinux) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath home = temp.GetPath();
  // Zen from its Flatpak, used yesterday.
  const base::FilePath zen_dir =
      home.AppendASCII(".var/app/app.zen_browser.zen/.zen");
  Write(zen_dir.AppendASCII("profiles.ini"),
        "[Profile0]\nName=Default (release)\nIsRelative=1\n"
        "Path=abc.Default (release)\nDefault=1\n");
  const base::FilePath zen = zen_dir.AppendASCII("abc.Default (release)");
  Write(zen.AppendASCII("places.sqlite"), "");
  Write(zen.AppendASCII("zen-sessions.jsonlz4"), "");
  Age(zen.AppendASCII("places.sqlite"), 1);
  // Chrome natively and from its Flatpak: one browser, two profiles.
  const base::FilePath chrome = home.AppendASCII(".config/google-chrome");
  Write(chrome.AppendASCII("Default/History"), "");
  Age(chrome.AppendASCII("Default/History"), 6);
  const base::FilePath chrome_flatpak =
      home.AppendASCII(".var/app/com.google.Chrome/config/google-chrome");
  Write(chrome_flatpak.AppendASCII("Local State"),
        R"({"profile": {"info_cache": {"Default": {"name": "Flatpak"}}}})");
  Write(chrome_flatpak.AppendASCII("Default/Bookmarks"), "{}");
  Age(chrome_flatpak.AppendASCII("Default/Bookmarks"), 3);
  // Firefox in its XDG folder (Firefox 147 and later), used longest ago.
  const base::FilePath firefox_dir =
      home.AppendASCII(".config/mozilla/firefox");
  Write(firefox_dir.AppendASCII("profiles.ini"),
        "[Profile0]\nName=default-release\nIsRelative=1\n"
        "Path=x.default-release\n");
  const base::FilePath firefox = firefox_dir.AppendASCII("x.default-release");
  Write(firefox.AppendASCII("places.sqlite"), "");
  Age(firefox.AppendASCII("places.sqlite"), 10);
  // Brave from its Flatpak, never used; Arc's macOS folder means nothing.
  ASSERT_TRUE(base::CreateDirectory(home.AppendASCII(
      ".var/app/com.brave.Browser/config/BraveSoftware/Brave-Browser")));
  Write(home.AppendASCII("Arc/User Data/Default/Bookmarks"), "{}");

  const std::vector<SourceProfile> sources =
      DetectSourceProfiles(home, "en-US");
  // The most recently used first; Chrome's two installs together, each
  // profile named (the native one has no Local State: unnamed).
  EXPECT_EQ((std::vector<std::u16string>{u"Zen", u"Google Chrome",
                                         u"Google Chrome/Flatpak", u"Firefox"}),
            Names(sources));
  ASSERT_EQ(4u, sources.size());
  EXPECT_EQ(zen, sources[0].source_path);
  EXPECT_EQ(user_data_importer::TYPE_FIREFOX, sources[0].importer_type);
  // Chromium's Firefox importer on Linux: passwords too (NSS).
  EXPECT_EQ(user_data_importer::HISTORY | user_data_importer::FAVORITES |
                user_data_importer::AUTOFILL_FORM_DATA |
                user_data_importer::PASSWORDS | kSpacesImportItem,
            sources[0].services_supported);
  EXPECT_EQ(chrome.AppendASCII("Default"), sources[1].source_path);
  EXPECT_EQ(chrome_flatpak.AppendASCII("Default"), sources[2].source_path);
  for (size_t i = 1; i < 3; ++i) {
    EXPECT_EQ(kChromiumImporterType, sources[i].importer_type);
    EXPECT_EQ(user_data_importer::HISTORY | user_data_importer::FAVORITES |
                  user_data_importer::PASSWORDS,
              sources[i].services_supported);
  }
  EXPECT_EQ(firefox, sources[3].source_path);
  EXPECT_FALSE(sources[3].services_supported & kSpacesImportItem);

  // Each install's keyring names (and the icon's app id) are the browser's.
  for (const base::FilePath& profile :
       {sources[1].source_path, sources[2].source_path}) {
    const KnownBrowser* browser = FindBrowser(home, profile);
    ASSERT_TRUE(browser);
    EXPECT_EQ("Chrome Safe Storage", browser->keychain_service);
    EXPECT_EQ("chrome", browser->secret_application);
    EXPECT_EQ("Chrome Keys", browser->kwallet_folder);
    EXPECT_EQ("com.google.Chrome", browser->bundle_id);
  }
  EXPECT_EQ(u"Zen", FindBrowser(home, zen)->name);
  EXPECT_EQ("app.zen_browser.zen", FindBrowser(home, zen)->bundle_id);
  EXPECT_EQ(u"Firefox", FindBrowser(home, firefox)->name);
  EXPECT_FALSE(FindBrowser(home, home.AppendASCII("Arc/User Data/Default")));
  for (const KnownBrowser& browser : KnownBrowsers()) {
    EXPECT_NE(u"Arc", browser.name);
    EXPECT_NE(u"Dia", browser.name);
  }
}

// Every Linux browser has a native folder and a Flatpak one, and every
// Chromium-based one its keyring names.
TEST(KoanImportSourcesTest, LinuxFolders) {
  for (const std::u16string_view name :
       {u"Zen", u"Google Chrome", u"Brave", u"Microsoft Edge", u"Vivaldi",
        u"Chromium", u"Floorp", u"LibreWolf", u"Waterfox", u"Firefox"}) {
    bool native = false;
    bool flatpak = false;
    for (const KnownBrowser& browser : KnownBrowsers()) {
      if (browser.name != name) {
        continue;
      }
      const std::string flatpak_dir =
          base::StrCat({".var/app/", browser.bundle_id, "/"});
      (browser.data_dir.starts_with(flatpak_dir) ? flatpak : native) = true;
      EXPECT_FALSE(browser.data_dir.starts_with("/")) << browser.data_dir;
      if (browser.engine == Engine::kChromium) {
        EXPECT_FALSE(browser.keychain_service.empty()) << browser.data_dir;
        EXPECT_FALSE(browser.secret_application.empty()) << browser.data_dir;
        EXPECT_FALSE(browser.kwallet_folder.empty()) << browser.data_dir;
      }
    }
    EXPECT_TRUE(native && flatpak) << base::UTF16ToUTF8(name);
  }
}

// The icon an app installs in the hicolor theme, nearest 64 px, else its SVG
// or a pixmap; its Flatpak app id's first.
TEST(KoanImportSourcesTest, AppIcons) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath user = temp.GetPath().AppendASCII("user");
  const base::FilePath system = temp.GetPath().AppendASCII("system");
  const std::vector<base::FilePath> dirs = {user, system};
  Write(system.AppendASCII("icons/hicolor/256x256/apps/google-chrome.png"),
        "big");
  Write(system.AppendASCII("icons/hicolor/48x48/apps/google-chrome.png"),
        "small");
  EXPECT_EQ(system.AppendASCII("icons/hicolor/48x48/apps/google-chrome.png"),
            FindAppIcon(dirs, AppIconNames("com.google.Chrome")));
  Write(user.AppendASCII("icons/hicolor/scalable/apps/com.google.Chrome.svg"),
        "<svg/>");
  EXPECT_EQ(
      user.AppendASCII("icons/hicolor/scalable/apps/com.google.Chrome.svg"),
      FindAppIcon(dirs, AppIconNames("com.google.Chrome")));
  Write(system.AppendASCII("pixmaps/zen.png"), "zen");
  EXPECT_EQ(system.AppendASCII("pixmaps/zen.png"),
            FindAppIcon(dirs, AppIconNames("app.zen_browser.zen")));
  EXPECT_TRUE(FindAppIcon(dirs, AppIconNames("com.brave.Browser")).empty());
  // Never a path out of the icon folders.
  EXPECT_TRUE(
      FindAppIcon(dirs, std::vector<std::string>{"../pixmaps/zen"}).empty());

  EXPECT_EQ((std::vector<std::string>{"org.mozilla.firefox", "firefox"}),
            AppIconNames("org.mozilla.firefox"));
  EXPECT_EQ(
      (std::vector<std::string>{"app.zen_browser.zen", "zen-browser", "zen"}),
      AppIconNames("app.zen_browser.zen"));
  EXPECT_EQ(std::vector<std::string>{"other.app"}, AppIconNames("other.app"));
}
#endif  // BUILDFLAG(IS_LINUX)

// The hook in ImporterList: Koan's sources before Chromium's, the welcome's
// pick first; found only under the test root, never the user's folders.
TEST(KoanImportSourcesTest, AddSourceProfiles) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->AppendSwitchPath(
      switches::kImportTestRoot, temp.GetPath());
  EXPECT_TRUE(HasTestRoot());
  EXPECT_EQ(temp.GetPath(), SearchRoot());
#if BUILDFLAG(IS_MAC)
  constexpr char kChromeHistory[] = "Google/Chrome/Default/History";
  constexpr char kVivaldiHistory[] = "Vivaldi/Default/History";
#else
  constexpr char kChromeHistory[] = ".config/google-chrome/Default/History";
  constexpr char kVivaldiHistory[] =
      ".var/app/com.vivaldi.Vivaldi/config/vivaldi/Default/History";
#endif
  Write(temp.GetPath().AppendASCII(kChromeHistory), "");
  Write(temp.GetPath().AppendASCII(kVivaldiHistory), "");
  Age(temp.GetPath().AppendASCII(kVivaldiHistory), 2);

  SourceProfile firefox;
  firefox.importer_name = u"Firefox";
  firefox.importer_type = user_data_importer::TYPE_FIREFOX;
  firefox.source_path = base::FilePath(FILE_PATH_LITERAL("/ff/p"));
  std::vector<SourceProfile> profiles = {firefox};
  AddSourceProfiles("en-US", profiles);
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Google Chrome", u"Vivaldi", u"Firefox"}),
      Names(profiles));
  // With the test root, passwords come from its key files.
  EXPECT_TRUE(profiles[0].services_supported & user_data_importer::PASSWORDS);
  EXPECT_EQ("com.google.Chrome", BundleIdOf(profiles[0]));
  EXPECT_EQ("org.mozilla.firefox", BundleIdOf(profiles[2]));
#if BUILDFLAG(IS_LINUX)
  // The icons, looked for under the test root only: found as the sources
  // were, for the welcome to show without reading a file.
  EXPECT_TRUE(LoadedAppIconDataUrl("com.google.Chrome").empty());
  Write(temp.GetPath().AppendASCII(
            ".local/share/icons/hicolor/64x64/apps/google-chrome.png"),
        "png");
  profiles = {firefox};
  AddSourceProfiles("en-US", profiles);
  EXPECT_EQ("data:image/png;base64,cG5n",
            LoadedAppIconDataUrl("com.google.Chrome"));
  EXPECT_TRUE(LoadedAppIconDataUrl("com.vivaldi.Vivaldi").empty());
#endif

  // The welcome picked Firefox: first, the others in their order.
  SetPreferredSource(firefox);
  profiles = {firefox};
  AddSourceProfiles("en-US", profiles);
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Firefox", u"Google Chrome", u"Vivaldi"}),
      Names(profiles));
  SetPreferredSource(profiles[2]);
  MovePreferredSourceFirst(profiles);
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Vivaldi", u"Firefox", u"Google Chrome"}),
      Names(profiles));
  // Not in the list (gone since): nothing moves.
  SetPreferredSource(SourceProfile());
  MovePreferredSourceFirst(profiles);
  EXPECT_EQ(u"Vivaldi", profiles[0].importer_name);
}

// A profile upstream lists too (Linux: Firefox's, which Koan lists with its
// Flatpak and XDG folders) is listed once, as Koan's.
TEST(KoanImportSourcesTest, AddSourceProfilesWithoutUpstreamCopies) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->AppendSwitchPath(
      switches::kImportTestRoot, temp.GetPath());
#if BUILDFLAG(IS_MAC)
  const base::FilePath dir = temp.GetPath().AppendASCII("zen");
#else
  const base::FilePath dir = temp.GetPath().AppendASCII(".mozilla/firefox");
#endif
  Write(dir.AppendASCII("profiles.ini"),
        "[Profile0]\nName=p\nIsRelative=1\nPath=p\n");
  Write(dir.AppendASCII("p/places.sqlite"), "");

  SourceProfile upstream;
  upstream.importer_name = u"Mozilla Firefox";
  upstream.importer_type = user_data_importer::TYPE_FIREFOX;
  upstream.source_path = dir.AppendASCII("p");
  SourceProfile other = upstream;
  other.source_path = temp.GetPath().AppendASCII("elsewhere");
  std::vector<SourceProfile> profiles = {upstream, other};
  SetPreferredSource(SourceProfile());
  AddSourceProfiles("en-US", profiles);
  ASSERT_EQ(2u, profiles.size());
  EXPECT_EQ(std::u16string(BUILDFLAG(IS_MAC) ? u"Zen" : u"Firefox"),
            profiles[0].importer_name);
  EXPECT_EQ(upstream.source_path, profiles[0].source_path);
  EXPECT_EQ(other.source_path, profiles[1].source_path);
}

}  // namespace koan::importer
