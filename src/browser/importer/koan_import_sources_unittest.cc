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

  const KnownBrowser* browser = FindBrowser(root, sources[2].source_path);
  ASSERT_TRUE(browser);
  EXPECT_EQ("Chrome Safe Storage", browser->keychain_service);
  EXPECT_EQ(u"Zen", FindBrowser(root, zen)->name);
  EXPECT_FALSE(FindBrowser(root, root.AppendASCII("Elsewhere/Default")));

  EXPECT_TRUE(DetectSourceProfiles(root.AppendASCII("none"), "").empty());
  EXPECT_TRUE(DetectSourceProfiles(base::FilePath(), "").empty());
}

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
  Write(temp.GetPath().AppendASCII("Google/Chrome/Default/History"), "");
  Write(temp.GetPath().AppendASCII("Vivaldi/Default/History"), "");
  Age(temp.GetPath().AppendASCII("Vivaldi/Default/History"), 2);

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

}  // namespace koan::importer
