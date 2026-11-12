// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Import from Zen, Arc and Chrome (#348), end to end through Chromium's
// importer: Koan's sources in its ImporterList, then an import through
// ExternalProcessImporterHost and ProfileWriter, as the import dialog runs
// it. Zen goes through Chromium's Firefox importer (the utility process),
// Arc and Chrome through Koan's reader in the browser process; Arc's and
// Zen's Spaces and pinned tabs (#363) through Koan's Spaces. Fixture
// profiles only, under --koan-import-test-root: never the user's own.

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/strings/cstring_view.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
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
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/password_store_results_observer.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/importer/koan_import_sources.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_switches.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gmock/include/gmock/gmock.h"
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
constexpr char kArcFavorite[] = "https://arc-favorite.example/";
constexpr char kArcFolderTab[] = "https://arc-folder.example/";
constexpr char kArcWork[] = "https://arc-work.example/";
constexpr char kZenEssential[] = "https://zen-essential.example/";
constexpr char kZenPinned[] = "https://zen-pinned.example/";
constexpr char kZenFolderTab[] = "https://zen-folder.example/";
constexpr char kZenPlay[] = "https://zen-play.example/";
constexpr char kZenUnpinned[] = "https://zen-unpinned.example/";
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

// `json` as Firefox's mozLz4 (.jsonlz4): the magic, the size and an LZ4
// block of literals only, which any LZ4 decoder reads.
std::string MozLz4(std::string_view json) {
  std::string file("mozLz40\0", 8);
  const uint32_t size = static_cast<uint32_t>(json.size());
  for (int shift = 0; shift < 32; shift += 8) {
    file.push_back(static_cast<char>((size >> shift) & 0xff));
  }
  // The token: 15 literals or more, the rest in bytes of 255 and one less.
  file.push_back(static_cast<char>(0xf0));
  size_t rest = json.size() - 15;
  for (; rest >= 255; rest -= 255) {
    file.push_back(static_cast<char>(0xff));
  }
  file.push_back(static_cast<char>(rest));
  file += json;
  return file;
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
    // Its Spaces, as Zen 1.24's session file has them (ZenSessionManager):
    // two Spaces, an Essential, a pinned tab with its own label, a folder
    // with a tab, and an unpinned tab.
    Write(profile.AppendASCII("zen-sessions.jsonlz4"),
          MozLz4(base::StrCat({R"({"spaces": [
            {"uuid": "{z1}", "name": "Zen Work", "icon": "\ud83e\uddd8",
             "theme": {"type": "gradient", "gradientColors": [{"c": [200, 100,
               50], "isCustom": false, "algorithm": "", "isPrimary": true,
               "lightness": 50, "type": "explicit-lightness"}],
               "opacity": 0.5, "texture": 0}},
            {"uuid": "{z2}", "name": "Zen Play"}],
          "folders": [{"id": "zf", "name": "Zen folder", "collapsed": false,
                       "parentId": null, "workspaceId": "{z1}"}],
          "tabs": [
            {"pinned": true, "zenEssential": true, "zenWorkspace": "{z1}",
             "entries": [{"url": ")",
                               kZenEssential, R"(",
                          "title": "Zen essential"}], "index": 1},
            {"pinned": true, "zenWorkspace": "{z1}", "zenStaticLabel": "Label",
             "entries": [{"url": ")",
                               kZenPinned, R"(", "title": "Zen pinned"}],
             "index": 1},
            {"pinned": true, "zenWorkspace": "{z1}", "groupId": "zf",
             "entries": [{"url": ")",
                               kZenFolderTab, R"(",
                          "title": "Zen folder tab"}], "index": 1},
            {"pinned": true, "zenWorkspace": "{z2}",
             "entries": [{"url": ")",
                               kZenPlay, R"("}], "index": 1},
            {"pinned": false, "zenWorkspace": "{z1}",
             "entries": [{"url": ")",
                               kZenUnpinned, R"("}], "index": 1}]})"})));
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

  // Arc: a profile with a History; its Spaces, pinned tabs (one in a
  // folder) and Favorites in StorableSidebar.json.
  void WriteArc() {
    const base::FilePath arc = root_.GetPath().AppendASCII("Arc");
    Write(arc.AppendASCII("User Data/Default/Bookmarks"),
          R"({"roots": {}, "version": 1})");
    Write(arc.AppendASCII("StorableSidebar.json"),
          base::StrCat({R"({"sidebar": {"containers": [{"global": {}}, {
            "topAppsContainerIDs": [{"default": true}, "top"],
            "spaces": [
              "s", {"id": "s", "title": "Reading",
                    "profile": {"default": true},
                    "containerIDs": ["pinned", "p"],
                    "customInfo": {"iconType": {"emoji_v2": "\ud83d\udcda"}}},
              "s2", {"id": "s2", "title": "Work",
                     "containerIDs": ["pinned", "p2"]}],
            "items": [
              "top", {"id": "top", "childrenIds": ["fav"]},
              "fav", {"id": "fav", "title": "Favorite", "data": {"tab": {
                      "savedURL": ")",
                        kArcFavorite, R"("}}},
              "p", {"id": "p", "childrenIds": ["t", "f"]},
              "t", {"id": "t", "title": "Pinned", "data": {"tab": {
                    "savedURL": ")",
                        kArcPinned, R"("}}},
              "f", {"id": "f", "title": "Arc folder", "data": {"list": {}},
                    "childrenIds": ["ft"]},
              "ft", {"id": "ft", "data": {"tab": {
                     "savedURL": ")",
                        kArcFolderTab, R"(",
                     "savedTitle": "In a folder"}}},
              "p2", {"id": "p2", "childrenIds": ["w"]},
              "w", {"id": "w", "data": {"tab": {
                    "savedURL": ")",
                        kArcWork, R"("}}}]}]}})"}));
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

  KoanBrowserViewController* Controller() {
    return KoanBrowserViewController::From(
        BrowserView::GetBrowserViewForBrowser(browser()));
  }

  // The window's tab pinned on `url` (an Essential or a Space's pinned
  // tab), or null.
  tabs::TabInterface* PinnedTab(const char* url) {
    TabStripModel* model = browser()->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      tabs::TabInterface* tab = model->GetTabAtIndex(i);
      if (const KoanTabState* state = GetTabState(tab);
          state && state->pinned_url == GURL(url)) {
        return tab;
      }
    }
    return nullptr;
  }

  std::optional<tab_groups::TabGroupId> SpaceOf(tabs::TabInterface* tab) {
    return tab ? tab->GetGroup() : std::nullopt;
  }

  // Whether `url` is a tab of the window, pinned or not.
  bool HasTab(const char* url) {
    TabStripModel* model = browser()->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      if (model->GetWebContentsAt(i)->GetVisibleURL() == GURL(url)) {
        return true;
      }
    }
    return false;
  }

  // The URLs of the tabs in the entries of the list pref `pref`, each
  // entry's under `key` (koan.workspaces.list's "pinned", koan.folders.list's
  // "tabs").
  std::set<std::string> StoredUrls(const char* pref, const char* key) {
    std::set<std::string> urls;
    for (const base::Value& entry :
         browser()->GetProfile()->GetPrefs()->GetList(pref)) {
      const base::ListValue* tabs =
          entry.is_dict() ? entry.GetDict().FindList(key) : nullptr;
      if (!tabs) {
        continue;
      }
      for (const base::Value& tab : *tabs) {
        if (const std::string* url =
                tab.is_dict() ? tab.GetDict().FindString("url") : nullptr) {
          urls.insert(*url);
        }
      }
    }
    return urls;
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
  // Without "Spaces and pinned tabs", no Space changes.
  EXPECT_THAT(SpaceNames(browser()), testing::ElementsAre(u"Space"));
  EXPECT_FALSE(PinnedTab(kZenPinned));
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

// Arc without "Spaces and pinned tabs": its pinned tabs arrive as
// bookmarks, in a folder named after the Space, and no Space changes.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsArcPinnedTabs) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  Import(Find(u"Arc"), user_data_importer::FAVORITES);
  EXPECT_TRUE(IsBookmarked(kArcPinned));
  EXPECT_TRUE(IsBookmarked(kArcFavorite));
  EXPECT_THAT(SpaceNames(browser()), testing::ElementsAre(u"Space"));
  EXPECT_FALSE(PinnedTab(kArcPinned));
}

// Zen and Arc offer their Spaces (a session file, a sidebar); Chrome doesn't.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, OffersSpaces) {
  EXPECT_TRUE(Find(u"Zen").services_supported & importer::kSpacesImportItem);
  EXPECT_TRUE(Find(u"Arc").services_supported & importer::kSpacesImportItem);
  EXPECT_FALSE(Find(u"Google Chrome").services_supported &
               importer::kSpacesImportItem);
}

// Arc's Spaces as Koan's: the first takes the window's untouched Space, the
// others come after it; their pinned tabs and folders, unloaded at their
// pinned URL; the Favorites as Essentials. The window stays on its tab.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsArcSpaces) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  tabs::TabInterface* active = browser()->GetTabStripModel()->GetActiveTab();
  // Reported as the bookmarks' item: the import dialog counts an import that
  // ended no item as failed.
  EXPECT_EQ(
      std::vector<user_data_importer::ImportItem>{
          user_data_importer::FAVORITES},
      Import(Find(u"Arc"), importer::kSpacesImportItem));
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  EXPECT_THAT(SpaceNames(browser()), testing::ElementsAre(u"Reading", u"Work"));
  const std::vector<KoanWorkspaceController::SpaceInfo> infos =
      spaces->GetSpaces();
  EXPECT_EQ(u"\U0001F4DA", infos[0].icon);
  EXPECT_TRUE(infos[0].active);
  EXPECT_EQ(active, browser()->GetTabStripModel()->GetActiveTab());

  tabs::TabInterface* favorite = PinnedTab(kArcFavorite);
  ASSERT_TRUE(favorite);
  EXPECT_TRUE(favorite->IsPinned());
  EXPECT_TRUE(IsPendingTab(favorite));

  tabs::TabInterface* pinned = PinnedTab(kArcPinned);
  ASSERT_TRUE(pinned);
  EXPECT_TRUE(IsPinnedInSpace(pinned));
  EXPECT_EQ(infos[0].id, SpaceOf(pinned));
  EXPECT_EQ(u"Pinned", pinned->GetContents()->GetTitle());
  EXPECT_TRUE(IsPendingTab(pinned));
  EXPECT_EQ(infos[1].id, SpaceOf(PinnedTab(kArcWork)));

  KoanFolderController* folders = Controller()->folders();
  ASSERT_TRUE(folders && folders->IsEnabled());
  const std::vector<KoanFolderController::Entry> entries =
      folders->GetFolders(infos[0].id);
  ASSERT_EQ(1u, entries.size());
  const KoanFolder* folder = folders->GetFolder(entries[0].id);
  ASSERT_TRUE(folder);
  EXPECT_EQ(u"Arc folder", folder->name);
  // Zen's import shows Arc's folders collapsed.
  EXPECT_TRUE(folder->collapsed);
  tabs::TabInterface* in_folder = PinnedTab(kArcFolderTab);
  ASSERT_TRUE(in_folder);
  EXPECT_EQ(entries[0].id, folders->GetFolderOf(in_folder));
  EXPECT_EQ(u"In a folder", in_folder->GetContents()->GetTitle());

  // Not as bookmarks too.
  EXPECT_FALSE(IsBookmarked(kArcPinned));

  // Kept for a start that restores no session, though they never loaded:
  // the Space's pinned tab with its Space, the folder's with its folder.
  EXPECT_TRUE(base::test::RunUntil([&] {
    return StoredUrls(prefs::kWorkspaceList, "pinned").contains(kArcPinned) &&
           StoredUrls(prefs::kFolderList, "tabs").contains(kArcFolderTab);
  }));
}

// Zen's Spaces alone (no utility process): its Spaces, Essentials, pinned
// tabs with their labels and folders; never its unpinned tabs.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsZenSpaces) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  EXPECT_EQ(
      std::vector<user_data_importer::ImportItem>{
          user_data_importer::FAVORITES},
      Import(Find(u"Zen"), importer::kSpacesImportItem));
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  EXPECT_THAT(SpaceNames(browser()),
              testing::ElementsAre(u"Zen Work", u"Zen Play"));
  const std::vector<KoanWorkspaceController::SpaceInfo> infos =
      spaces->GetSpaces();
  EXPECT_EQ(u"\U0001F9D8", infos[0].icon);
  // Zen's theme.
  EXPECT_FALSE(infos[0].theme.is_default());
  EXPECT_TRUE(infos[1].theme.is_default());

  tabs::TabInterface* essential = PinnedTab(kZenEssential);
  ASSERT_TRUE(essential);
  EXPECT_TRUE(essential->IsPinned());
  tabs::TabInterface* pinned = PinnedTab(kZenPinned);
  ASSERT_TRUE(pinned);
  EXPECT_TRUE(IsPinnedInSpace(pinned));
  EXPECT_EQ(infos[0].id, SpaceOf(pinned));
  EXPECT_EQ(u"Label", GetTabState(pinned)->custom_title);
  EXPECT_EQ(infos[1].id, SpaceOf(PinnedTab(kZenPlay)));

  KoanFolderController* folders = Controller()->folders();
  ASSERT_TRUE(folders);
  const std::vector<KoanFolderController::Entry> entries =
      folders->GetFolders(infos[0].id);
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ(u"Zen folder", folders->GetFolder(entries[0].id)->name);
  EXPECT_FALSE(folders->GetFolder(entries[0].id)->collapsed);
  EXPECT_EQ(entries[0].id, folders->GetFolderOf(PinnedTab(kZenFolderTab)));

  EXPECT_FALSE(HasTab(kZenUnpinned));
}

// Zen's Spaces next to its bookmarks (Chromium's Firefox importer brings
// those, Koan the Spaces).
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsZenSpacesAndBookmarks) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::vector<user_data_importer::ImportItem> ended =
      Import(Find(u"Zen"),
             user_data_importer::FAVORITES | importer::kSpacesImportItem);
  EXPECT_TRUE(std::ranges::contains(ended, user_data_importer::FAVORITES));
  EXPECT_TRUE(IsBookmarked(kZenBookmark));
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  EXPECT_THAT(SpaceNames(browser()),
              testing::ElementsAre(u"Zen Work", u"Zen Play"));
  EXPECT_TRUE(base::test::RunUntil([&] { return !!PinnedTab(kZenPinned); }));
}

// Only what was asked for.
IN_PROC_BROWSER_TEST_F(KoanImportBrowserTest, ImportsOnlyTheItemsAsked) {
  Import(Find(u"Google Chrome"), user_data_importer::FAVORITES);
  EXPECT_TRUE(IsBookmarked(kChromeBookmark));
  EXPECT_FALSE(InHistory(kChromeHistory));
}

}  // namespace koan
