// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Import from Zen, Arc and Chrome (#348), end to end through Chromium's
// importer: Koan's sources in its ImporterList, then an import through
// ExternalProcessImporterHost and ProfileWriter, as the import dialog runs
// it. Zen goes through Chromium's Firefox importer (the utility process),
// Arc and Chrome through Koan's reader in the browser process. Fixture
// profiles only, under --koan-import-test-root: never the user's own.

#include <algorithm>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/password_store_results_observer.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/importer/koan_import_sources.h"
#include "koan/browser/koan_switches.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace koan {

namespace {

using user_data_importer::SourceProfile;

constexpr char kZenHistory[] = "https://zen-history.example/";
constexpr char kZenBookmark[] = "https://zen-bookmark.example/";
constexpr char kChromeHistory[] = "https://chrome-history.example/";
constexpr char kChromeBookmark[] = "https://chrome-bookmark.example/";
constexpr char kChromeLogin[] = "https://chrome-login.example/";
constexpr char kArcPinned[] = "https://arc-pinned.example/";
// "hunter2" under the key of the Safe Storage secret "peanuts" (as in
// koan_chromium_profile_reader_unittest.cc).
constexpr char kHunter2[] = "7218b8ec8774bcaad0e8305f7a6cf965";

class ImportWaiter : public ::importer::ImporterProgressObserver {
 public:
  explicit ImportWaiter(base::OnceClosure done) : done_(std::move(done)) {}

  std::vector<user_data_importer::ImportItem> items_ended;

 private:
  // ::importer::ImporterProgressObserver:
  void ImportStarted() override {}
  void ImportItemStarted(user_data_importer::ImportItem item) override {}
  void ImportItemEnded(user_data_importer::ImportItem item) override {
    items_ended.push_back(item);
  }
  void ImportEnded() override { std::move(done_).Run(); }

  base::OnceClosure done_;
};

void Write(const base::FilePath& file, std::string_view contents) {
  ASSERT_TRUE(base::CreateDirectory(file.DirName()));
  ASSERT_TRUE(base::WriteFile(file, contents));
}

void Exec(sql::Database& db, base::cstring_view statement) {
  ASSERT_TRUE(db.Execute(statement)) << statement.c_str();
}

// Microseconds since 1970 (Firefox) and since 1601 (Chromium), now.
std::string FirefoxNow() {
  return base::NumberToString(
      (base::Time::Now() - base::Time::UnixEpoch()).InMicroseconds());
}
std::string ChromiumNow() {
  return base::NumberToString(
      base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
}

class KoanImportBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(root_.CreateUniqueTempDir());
    command_line->AppendSwitchPath(switches::kImportTestRoot, root_.GetPath());
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    WriteZen();
    WriteChrome();
    WriteArc();
  }

  // A Zen profile as Zen 1.24 writes one (its places.sqlite schema, the
  // tables Chromium's Firefox importer reads).
  void WriteZen() {
    const base::FilePath zen = root_.GetPath().AppendASCII("zen");
    Write(zen.AppendASCII("profiles.ini"),
          "[Profile0]\nName=Default (release)\nIsRelative=1\n"
          "Path=Profiles/k0an.Default (release)\nDefault=1\n\n"
          "[Install6F193CCC56814779]\n"
          "Default=Profiles/k0an.Default (release)\nLocked=1\n");
    const base::FilePath profile =
        zen.AppendASCII("Profiles/k0an.Default (release)");
    ASSERT_TRUE(base::CreateDirectory(profile));
    sql::Database db(sql::Database::Tag("Test"));
    ASSERT_TRUE(db.Open(profile.AppendASCII("places.sqlite")));
    Exec(db,
         "CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url LONGVARCHAR, "
         "title LONGVARCHAR, rev_host LONGVARCHAR, visit_count INTEGER "
         "DEFAULT 0, hidden INTEGER DEFAULT 0 NOT NULL, typed INTEGER "
         "DEFAULT 0 NOT NULL, frecency INTEGER DEFAULT -1 NOT NULL, "
         "last_visit_date INTEGER, guid TEXT, foreign_count INTEGER DEFAULT "
         "0 NOT NULL, url_hash INTEGER DEFAULT 0 NOT NULL)");
    Exec(db,
         "CREATE TABLE moz_historyvisits (id INTEGER PRIMARY KEY, from_visit "
         "INTEGER, place_id INTEGER, visit_date INTEGER, visit_type INTEGER, "
         "session INTEGER, source INTEGER DEFAULT 0 NOT NULL, "
         "triggeringPlaceId INTEGER)");
    Exec(db,
         "CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, "
         "fk INTEGER DEFAULT NULL, parent INTEGER, position INTEGER, title "
         "LONGVARCHAR, keyword_id INTEGER, folder_type TEXT, dateAdded "
         "INTEGER, lastModified INTEGER, guid TEXT, syncStatus INTEGER NOT "
         "NULL DEFAULT 0, syncChangeCounter INTEGER NOT NULL DEFAULT 1)");
    Exec(db,
         "CREATE TABLE moz_keywords (id INTEGER PRIMARY KEY AUTOINCREMENT, "
         "keyword TEXT UNIQUE, place_id INTEGER, post_data TEXT)");
    Exec(db,
         "CREATE TABLE moz_anno_attributes (id INTEGER PRIMARY KEY, name "
         "VARCHAR(32) UNIQUE NOT NULL)");
    Exec(db,
         "CREATE TABLE moz_items_annos (id INTEGER PRIMARY KEY, item_id "
         "INTEGER NOT NULL, anno_attribute_id INTEGER, content LONGVARCHAR, "
         "flags INTEGER DEFAULT 0, expiration INTEGER DEFAULT 0, type INTEGER "
         "DEFAULT 0, dateAdded INTEGER DEFAULT 0, lastModified INTEGER "
         "DEFAULT 0)");
    const std::string now = FirefoxNow();
    sql::Statement places(db.GetUniqueStatement(
        "INSERT INTO moz_places (id, url, title, visit_count, typed, guid) "
        "VALUES (1, ?, 'Zen history', 2, 1, 'p1'), "
        "(2, ?, 'Zen bookmark', 1, 0, 'p2')"));
    places.BindString(0, kZenHistory);
    places.BindString(1, kZenBookmark);
    ASSERT_TRUE(places.Run());
    sql::Statement visits(db.GetUniqueStatement(
        "INSERT INTO moz_historyvisits (place_id, visit_date, visit_type) "
        "VALUES (1, ?, 1), (2, ?, 1)"));
    visits.BindString(0, now);
    visits.BindString(1, now);
    ASSERT_TRUE(visits.Run());
    sql::Statement bookmarks(db.GetUniqueStatement(
        "INSERT INTO moz_bookmarks (id, type, fk, parent, position, title, "
        "dateAdded, guid) VALUES "
        "(1, 2, NULL, 0, 0, '', ?, 'root________'), "
        "(2, 2, NULL, 1, 0, 'menu', ?, 'menu________'), "
        "(3, 2, NULL, 1, 1, 'toolbar', ?, 'toolbar_____'), "
        "(5, 2, NULL, 1, 3, 'unfiled', ?, 'unfiled_____'), "
        "(10, 1, 2, 3, 0, 'Zen bookmark', ?, 'b10')"));
    for (int i = 0; i < 5; ++i) {
      bookmarks.BindString(i, now);
    }
    ASSERT_TRUE(bookmarks.Run());
  }

  // A Chrome profile: Local State, Bookmarks, History and Login Data, its
  // Safe Storage secret in the test root's Keychain folder.
  void WriteChrome() {
    const base::FilePath chrome = root_.GetPath().AppendASCII("Google/Chrome");
    Write(chrome.AppendASCII("Local State"),
          R"({"profile": {"last_used": "Default",
              "info_cache": {"Default": {"name": "Person 1"}}}})");
    const base::FilePath profile = chrome.AppendASCII("Default");
    Write(profile.AppendASCII("Bookmarks"),
          std::string(R"({"roots": {"bookmark_bar": {"type": "folder",
              "name": "Bookmarks bar", "children": [{"type": "url",
              "name": "Chrome bookmark", "url": ")") +
              kChromeBookmark + R"("}]}}, "version": 1})");
    {
      sql::Database db(sql::Database::Tag("Test"));
      ASSERT_TRUE(db.Open(profile.AppendASCII("History")));
      Exec(db,
           "CREATE TABLE urls(id INTEGER PRIMARY KEY AUTOINCREMENT, url "
           "LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0 "
           "NOT NULL, typed_count INTEGER DEFAULT 0 NOT NULL, "
           "last_visit_time INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT "
           "NULL)");
      sql::Statement row(db.GetUniqueStatement(
          "INSERT INTO urls (url, title, visit_count, typed_count, "
          "last_visit_time) VALUES (?, 'Chrome history', 3, 1, ?)"));
      row.BindString(0, kChromeHistory);
      row.BindString(1, ChromiumNow());
      ASSERT_TRUE(row.Run());
    }
    {
      sql::Database db(sql::Database::Tag("Test"));
      ASSERT_TRUE(db.Open(profile.AppendASCII("Login Data")));
      Exec(db,
           "CREATE TABLE logins (origin_url VARCHAR NOT NULL, action_url "
           "VARCHAR, username_element VARCHAR, username_value VARCHAR, "
           "password_element VARCHAR, password_value BLOB, signon_realm "
           "VARCHAR NOT NULL, blacklisted_by_user INTEGER NOT NULL, scheme "
           "INTEGER NOT NULL)");
      std::vector<uint8_t> password = {'v', '1', '0'};
      std::vector<uint8_t> body;
      ASSERT_TRUE(base::HexStringToBytes(kHunter2, &body));
      password.insert(password.end(), body.begin(), body.end());
      sql::Statement row(db.GetUniqueStatement(
          "INSERT INTO logins VALUES (?, ?, 'user', 'ana', 'pass', ?, ?, 0, "
          "0)"));
      row.BindString(0, kChromeLogin);
      row.BindString(1, kChromeLogin);
      row.BindBlob(2, password);
      row.BindString(3, kChromeLogin);
      ASSERT_TRUE(row.Run());
    }
    Write(root_.GetPath().AppendASCII("Keychain/Chrome Safe Storage"),
          "peanuts\n");
  }

  // Arc: a profile with a History, its pinned tabs in StorableSidebar.json.
  void WriteArc() {
    const base::FilePath arc = root_.GetPath().AppendASCII("Arc");
    Write(arc.AppendASCII("User Data/Default/Bookmarks"),
          R"({"roots": {}, "version": 1})");
    Write(arc.AppendASCII("StorableSidebar.json"),
          std::string(R"({"sidebar": {"containers": [{"global": {}}, {
            "spaces": ["s", {"id": "s", "title": "Reading",
                             "profile": {"default": true},
                             "containerIDs": ["pinned", "p"]}],
            "items": ["p", {"id": "p", "childrenIds": ["t"]},
                      "t", {"id": "t", "title": "Pinned", "data": {"tab": {
                            "savedURL": ")") +
              kArcPinned + R"("}}}]}]}})");
  }

  std::vector<SourceProfile> Detect() {
    ImporterList list;
    base::RunLoop run_loop;
    list.DetectSourceProfiles("en-US", /*include_interactive_profiles=*/true,
                              run_loop.QuitClosure());
    run_loop.Run();
    std::vector<SourceProfile> sources;
    for (size_t i = 0; i < list.count(); ++i) {
      sources.push_back(list.GetSourceProfileAt(i));
    }
    return sources;
  }

  SourceProfile Find(std::u16string_view name) {
    std::vector<SourceProfile> sources = Detect();
    const auto it =
        std::ranges::find(sources, name, &SourceProfile::importer_name);
    EXPECT_NE(it, sources.end()) << name;
    return it == sources.end() ? SourceProfile() : *it;
  }

  // Imports `items` from `source` as the import dialog does
  // (ImportDataHandler::StartImport), and waits for the end.
  std::vector<user_data_importer::ImportItem> Import(
      const SourceProfile& source,
      uint16_t items) {
    Profile* profile = browser()->GetProfile();
    bookmarks::test::WaitForBookmarkModelToLoad(
        BookmarkModelFactory::GetForBrowserContext(profile));
    base::RunLoop run_loop;
    ImportWaiter waiter(run_loop.QuitClosure());
    // Deletes itself at the end.
    auto* host = new ExternalProcessImporterHost();
    host->set_headless();
    host->set_observer(&waiter);
    host->StartImportSettings(source, profile, items,
                              new ProfileWriter(profile));
    run_loop.Run();
    return waiter.items_ended;
  }

  bool IsBookmarked(const char* url) {
    return BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile())
        ->IsBookmarked(GURL(url));
  }

  bool InHistory(const char* url) {
    ui_test_utils::HistoryEnumerator history(browser()->GetProfile());
    return std::ranges::contains(history.urls(), GURL(url));
  }

  base::ScopedTempDir root_;
};

}  // namespace

// Koan's browsers come first in Chromium's list (the import dialog's and the
// welcome's), before upstream's; the bookmarks file stays last.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ListsZenArcAndChrome) {
  const std::vector<SourceProfile> sources = Detect();
  ASSERT_GE(sources.size(), 4u);
  std::vector<std::u16string> first;
  for (size_t i = 0; i < 3; ++i) {
    first.push_back(sources[i].importer_name);
  }
  std::ranges::sort(first);
  EXPECT_EQ((std::vector<std::u16string>{u"Arc", u"Google Chrome", u"Zen"}),
            first);
  EXPECT_EQ(user_data_importer::TYPE_BOOKMARKS_FILE,
            sources.back().importer_type);
  for (size_t i = 0; i < 3; ++i) {
    // Each profile is the only one of its browser: no profile name.
    EXPECT_TRUE(sources[i].profile.empty()) << sources[i].importer_name;
    EXPECT_TRUE(root_.GetPath().IsParent(sources[i].source_path));
  }
}

// The welcome's pick is listed first, so the import dialog opens on it.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, WelcomePickFirst) {
  importer::SetPreferredSource(Find(u"Arc"));
  EXPECT_EQ(u"Arc", Detect()[0].importer_name);
  importer::SetPreferredSource(Find(u"Zen"));
  EXPECT_EQ(u"Zen", Detect()[0].importer_name);
}

// Zen through Chromium's Firefox importer, given Zen's profile folder.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsZen) {
  const SourceProfile zen = Find(u"Zen");
  EXPECT_EQ(user_data_importer::TYPE_FIREFOX, zen.importer_type);
  EXPECT_FALSE(zen.services_supported & user_data_importer::PASSWORDS);
  const std::vector<user_data_importer::ImportItem> ended =
      Import(zen, user_data_importer::HISTORY | user_data_importer::FAVORITES);
  EXPECT_TRUE(std::ranges::contains(ended, user_data_importer::HISTORY));
  EXPECT_TRUE(std::ranges::contains(ended, user_data_importer::FAVORITES));
  EXPECT_TRUE(IsBookmarked(kZenBookmark));
  EXPECT_TRUE(InHistory(kZenHistory));
}

// Chrome: bookmarks, history and the saved password (its key from the test
// root, never the keychain).
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsChrome) {
  const SourceProfile chrome = Find(u"Google Chrome");
  EXPECT_TRUE(importer::IsChromiumProfileSource(chrome));
  ASSERT_TRUE(chrome.services_supported & user_data_importer::PASSWORDS);
  const std::vector<user_data_importer::ImportItem> ended = Import(
      chrome, user_data_importer::HISTORY | user_data_importer::FAVORITES |
                  user_data_importer::PASSWORDS);
  EXPECT_EQ((std::vector<user_data_importer::ImportItem>{
                user_data_importer::HISTORY, user_data_importer::FAVORITES,
                user_data_importer::PASSWORDS}),
            ended);
  EXPECT_TRUE(IsBookmarked(kChromeBookmark));
  EXPECT_TRUE(InHistory(kChromeHistory));

  password_manager::PasswordStoreInterface* store =
      ProfilePasswordStoreFactory::GetForProfile(
          browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS)
          .get();
  ASSERT_TRUE(store);
  password_manager::PasswordStoreResultsObserver results;
  store->GetAutofillableLogins(results.GetWeakPtr());
  const auto logins = results.WaitForResults();
  ASSERT_EQ(1u, logins.size());
  EXPECT_EQ(kChromeLogin, logins[0].signon_realm);
  EXPECT_EQ(u"ana", logins[0].username_value);
}

// Arc: its pinned tabs arrive as bookmarks, in a folder named after the
// Space.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsArcPinnedTabs) {
  Import(Find(u"Arc"), user_data_importer::FAVORITES);
  EXPECT_TRUE(IsBookmarked(kArcPinned));
}

// Only what was asked for.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsOnlyTheItemsAsked) {
  Import(Find(u"Google Chrome"), user_data_importer::FAVORITES);
  EXPECT_TRUE(IsBookmarked(kChromeBookmark));
  EXPECT_FALSE(InHistory(kChromeHistory));
}

}  // namespace koan
