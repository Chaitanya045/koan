// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_chromium_profile_reader.h"

#include <array>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/aes_cbc.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::importer {

namespace {

using user_data_importer::ImportedBookmarkEntry;
using user_data_importer::ImportedPasswordForm;

// PBKDF2-HMAC-SHA1("peanuts", "saltysalt", 1003, 16) and "hunter2" encrypted
// with it (AES-128-CBC, 16-space IV), computed with Python's hashlib and
// `openssl enc`, so the parameters are checked against other code than
// Chromium's.
constexpr char kSecret[] = "peanuts";
constexpr char kKeyHex[] = "d9a09d499b4e1b7461f28e67972c6dbd";
constexpr char kHunter2Hex[] = "7218b8ec8774bcaad0e8305f7a6cf965";

std::vector<uint8_t> V10(std::string_view hex) {
  std::vector<uint8_t> bytes = {'v', '1', '0'};
  std::vector<uint8_t> body;
  EXPECT_TRUE(base::HexStringToBytes(hex, &body));
  bytes.insert(bytes.end(), body.begin(), body.end());
  return bytes;
}

std::vector<uint8_t> Encrypt(const SafeStorageKey& key, std::string_view text) {
  static constexpr std::array<uint8_t, 16> kIv = {' ', ' ', ' ', ' ', ' ', ' ',
                                                  ' ', ' ', ' ', ' ', ' ', ' ',
                                                  ' ', ' ', ' ', ' '};
  std::vector<uint8_t> value = {'v', '1', '0'};
  std::vector<uint8_t> body =
      crypto::aes_cbc::Encrypt(key, kIv, base::as_byte_span(text));
  value.insert(value.end(), body.begin(), body.end());
  return value;
}

std::vector<std::u16string> Path(const ImportedBookmarkEntry& entry) {
  return entry.path;
}

}  // namespace

TEST(KoanChromiumProfileReaderTest, SafeStorageKeyAndPassword) {
  const SafeStorageKey key = DeriveSafeStorageKey(kSecret);
  EXPECT_EQ(kKeyHex, base::HexEncodeLower(key));
  EXPECT_EQ(u"hunter2", DecryptPassword(V10(kHunter2Hex), key));
  EXPECT_EQ(u"pässwörd", DecryptPassword(Encrypt(key, "pässwörd"), key));
  // No "v10", the wrong key, a cut-off value: not a password.
  std::vector<uint8_t> raw;
  ASSERT_TRUE(base::HexStringToBytes(kHunter2Hex, &raw));
  EXPECT_FALSE(DecryptPassword(raw, key));
  EXPECT_FALSE(DecryptPassword(V10(kHunter2Hex), DeriveSafeStorageKey("x")));
  EXPECT_FALSE(DecryptPassword(V10("7218b8ec"), key));
}

TEST(KoanChromiumProfileReaderTest, Bookmarks) {
  const std::vector<ImportedBookmarkEntry> entries = ParseBookmarksJson(R"({
    "roots": {
      "bookmark_bar": {"type": "folder", "name": "Bookmarks bar", "children": [
        {"type": "url", "name": "Example", "url": "https://example.com/",
         "date_added": "13300000000000000"},
        {"type": "folder", "name": "Work", "children": [
          {"type": "url", "name": "Docs", "url": "https://docs.example/"},
          {"type": "folder", "name": "Empty", "children": []}
        ]},
        {"type": "url", "name": "Broken", "url": "not a url"}
      ]},
      "other": {"type": "folder", "name": "Other bookmarks", "children": [
        {"type": "url", "name": "Other", "url": "https://other.example/"}
      ]},
      "synced": {"type": "folder", "name": "", "children": [
        {"type": "url", "name": "Phone", "url": "https://phone.example/"}
      ]}
    },
    "version": 1
  })");
  ASSERT_EQ(5u, entries.size());
  EXPECT_EQ(GURL("https://example.com/"), entries[0].url);
  EXPECT_EQ(u"Example", entries[0].title);
  EXPECT_TRUE(entries[0].in_toolbar);
  EXPECT_FALSE(entries[0].is_folder);
  EXPECT_EQ(std::vector<std::u16string>{u"Bookmarks bar"}, Path(entries[0]));
  EXPECT_EQ(FromChromiumTime(13300000000000000), entries[0].creation_time);
  EXPECT_EQ(GURL("https://docs.example/"), entries[1].url);
  EXPECT_EQ((std::vector<std::u16string>{u"Bookmarks bar", u"Work"}),
            Path(entries[1]));
  // An empty folder is kept, as a folder.
  EXPECT_TRUE(entries[2].is_folder);
  EXPECT_EQ(u"Empty", entries[2].title);
  EXPECT_EQ((std::vector<std::u16string>{u"Bookmarks bar", u"Work"}),
            Path(entries[2]));
  EXPECT_FALSE(entries[3].in_toolbar);
  EXPECT_EQ(std::vector<std::u16string>{u"Other bookmarks"}, Path(entries[3]));
  // A root without a name gets its usual one.
  EXPECT_EQ(std::vector<std::u16string>{u"Mobile bookmarks"}, Path(entries[4]));

  EXPECT_TRUE(ParseBookmarksJson("").empty());
  EXPECT_TRUE(ParseBookmarksJson(R"({"roots": 3})").empty());
}

TEST(KoanChromiumProfileReaderTest, ArcSidebar) {
  // StorableSidebar.json's shape: lists of key, value pairs; the "global"
  // container is the window's sidebar.
  constexpr char kSidebar[] = R"({"sidebar": {"containers": [
    {"littleBrowser": {}}, {"items": [], "spaces": []},
    {"global": {}}, {
      "topAppsContainerIDs": [{"default": true}, "top",
                              {"custom": {"_0": {"directoryBasename":
                                                 "Profile 1"}}}, "top1"],
      "spaces": [
        "s1", {"id": "thebrowser.company.defaultPersonalSpaceID",
               "title": "  ", "profile": {"default": true},
               "containerIDs": ["unpinned", "u1", "pinned", "p1"]},
        "s2", {"id": "s2", "title": "Work",
               "profile": {"custom": {"_0": {"directoryBasename":
                                             "Profile 1"}}},
               "containerIDs": ["pinned", "p2"]},
        "s3", {"id": "s3", "containerIDs": ["pinned", "p3"]}
      ],
      "items": [
        "top", {"id": "top", "childrenIds": ["mail"], "data": {"itemContainer": {}}},
        "mail", {"id": "mail", "parentID": "top", "title": "Mail",
                 "data": {"tab": {"savedURL": "https://mail.example/",
                                  "savedTitle": ""}}},
        "top1", {"id": "top1", "data": {"itemContainer": {}}},
        "p1", {"id": "p1", "childrenIds": ["news", "folder", "split", "bad"],
               "data": {"itemContainer": {}}},
        "news", {"id": "news", "title": "News", "data": {"tab": {
                 "savedURL": "https://news.example/", "savedTitle": "Today"}}},
        "folder", {"id": "folder", "title": "Reading", "data": {"list": {}},
                   "childrenIds": ["blog"]},
        "blog", {"id": "blog", "title": "Blog", "data": {"tab": {
                 "savedURL": "https://blog.example/"}}},
        "split", {"id": "split", "data": {"splitView": {}}},
        "left", {"id": "left", "parentID": "split", "title": "Left",
                 "data": {"tab": {"savedURL": "https://left.example/"}}},
        "bad", {"id": "bad", "title": "Settings",
                "data": {"tab": {"savedURL": "arc://settings"}}},
        "u1", {"id": "u1", "childrenIds": ["temp"]},
        "temp", {"id": "temp", "data": {"tab": {
                 "savedURL": "https://temporary.example/"}}},
        "p2", {"id": "p2", "childrenIds": ["work"]},
        "work", {"id": "work", "data": {"tab": {
                 "savedURL": "https://work.example/"}}},
        "p3", {"id": "p3", "childrenIds": ["third"]},
        "third", {"id": "third", "data": {"tab": {
                  "savedURL": "https://third.example/"}}}
      ]
    }
  ]}})";

  const std::vector<ImportedBookmarkEntry> main =
      ParseArcSidebar(kSidebar, "Default");
  ASSERT_EQ(5u, main.size());
  // The Favorites first, then each Space's pinned tabs; never its unpinned
  // ones, nor a page Koan can't open.
  EXPECT_EQ(GURL("https://mail.example/"), main[0].url);
  EXPECT_EQ(u"Mail", main[0].title);
  EXPECT_EQ(std::vector<std::u16string>{u"Favorites"}, Path(main[0]));
  EXPECT_EQ(GURL("https://news.example/"), main[1].url);
  EXPECT_EQ(u"Today", main[1].title);
  // Arc's default Space without a title is "Personal".
  EXPECT_EQ(std::vector<std::u16string>{u"Personal"}, Path(main[1]));
  EXPECT_EQ(GURL("https://blog.example/"), main[2].url);
  EXPECT_EQ((std::vector<std::u16string>{u"Personal", u"Reading"}),
            Path(main[2]));
  // A split view's tabs (found by parentID), in the Space's folder.
  EXPECT_EQ(GURL("https://left.example/"), main[3].url);
  EXPECT_EQ(std::vector<std::u16string>{u"Personal"}, Path(main[3]));
  EXPECT_FALSE(main[3].in_toolbar);
  // A Space without a profile is the main profile's; without a title, it is
  // numbered.
  EXPECT_EQ(GURL("https://third.example/"), main[4].url);
  EXPECT_EQ(std::vector<std::u16string>{u"Space 3"}, Path(main[4]));

  const std::vector<ImportedBookmarkEntry> other =
      ParseArcSidebar(kSidebar, "Profile 1");
  ASSERT_EQ(1u, other.size());
  EXPECT_EQ(GURL("https://work.example/"), other[0].url);
  EXPECT_EQ(std::vector<std::u16string>{u"Work"}, Path(other[0]));

  EXPECT_TRUE(ParseArcSidebar("{}", "Default").empty());
  EXPECT_TRUE(
      ParseArcSidebar(R"({"sidebar": {"containers": [1, 2, 3]}})", "Default")
          .empty());
}

TEST(KoanChromiumProfileReaderTest, History) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath file = dir.GetPath().AppendASCII("History");
  {
    sql::Database db(sql::Database::Tag("Test"));
    ASSERT_TRUE(db.Open(file));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE urls(id INTEGER PRIMARY KEY AUTOINCREMENT, url "
        "LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0 NOT "
        "NULL, typed_count INTEGER DEFAULT 0 NOT NULL, last_visit_time "
        "INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT NULL)"));
    ASSERT_TRUE(db.Execute(
        "INSERT INTO urls(url, title, visit_count, typed_count, "
        "last_visit_time, hidden) VALUES "
        "('https://example.com/', 'Example', 3, 1, 13300000000000000, 0), "
        "('chrome://settings/', 'Settings', 9, 0, 13300000000000000, 0), "
        "('https://frame.example/', 'Frame', 1, 0, 13300000000000001, 1)"));
  }
  const std::vector<user_data_importer::ImporterURLRow> rows =
      ReadHistory(file);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://example.com/"), rows[0].url);
  EXPECT_EQ(u"Example", rows[0].title);
  EXPECT_EQ(3, rows[0].visit_count);
  EXPECT_EQ(1, rows[0].typed_count);
  EXPECT_EQ(FromChromiumTime(13300000000000000), rows[0].last_visit);
  EXPECT_FALSE(rows[0].hidden);
  EXPECT_TRUE(rows[1].hidden);

  EXPECT_TRUE(ReadHistory(dir.GetPath().AppendASCII("missing")).empty());
}

TEST(KoanChromiumProfileReaderTest, Logins) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath file = dir.GetPath().AppendASCII("Login Data");
  const SafeStorageKey key = DeriveSafeStorageKey(kSecret);
  {
    sql::Database db(sql::Database::Tag("Test"));
    ASSERT_TRUE(db.Open(file));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE logins (origin_url VARCHAR NOT NULL, action_url "
        "VARCHAR, username_element VARCHAR, username_value VARCHAR, "
        "password_element VARCHAR, password_value BLOB, signon_realm "
        "VARCHAR NOT NULL, blacklisted_by_user INTEGER NOT NULL, scheme "
        "INTEGER NOT NULL)"));
    const struct {
      const char* origin;
      const char* user;
      std::vector<uint8_t> password;
      int blocked;
      int scheme;
    } kRows[] = {
        {"https://site.example/login", "ana", V10(kHunter2Hex), 0, 0},
        {"https://never.example/", "", {}, 1, 0},
        {"https://basic.example/", "bo", Encrypt(key, "b4sic"), 0, 1},
        {"https://digest.example/", "di", Encrypt(key, "x"), 0, 2},
        {"https://other-key.example/", "ok",
         Encrypt(DeriveSafeStorageKey("other"), "x"), 0, 0},
        {"https://federated.example/", "fe", {}, 0, 0},
    };
    for (const auto& row : kRows) {
      sql::Statement insert(db.GetUniqueStatement(
          "INSERT INTO logins VALUES (?, ?, 'user', ?, 'pass', ?, ?, ?, ?)"));
      const GURL origin(row.origin);
      insert.BindString(0, row.origin);
      insert.BindString(1, origin.GetWithEmptyPath().spec() + "submit");
      insert.BindString(2, row.user);
      insert.BindBlob(3, row.password);
      insert.BindString(4, origin.GetWithEmptyPath().spec());
      insert.BindInt(5, row.blocked);
      insert.BindInt(6, row.scheme);
      ASSERT_TRUE(insert.Run());
    }
  }
  const std::vector<ImportedPasswordForm> forms = ReadLogins(file, key);
  ASSERT_EQ(3u, forms.size());
  EXPECT_EQ(GURL("https://site.example/login"), forms[0].url);
  EXPECT_EQ(GURL("https://site.example/submit"), forms[0].action);
  EXPECT_EQ("https://site.example/", forms[0].signon_realm);
  EXPECT_EQ(u"user", forms[0].username_element);
  EXPECT_EQ(u"ana", forms[0].username_value);
  EXPECT_EQ(u"pass", forms[0].password_element);
  EXPECT_EQ(u"hunter2", forms[0].password_value);
  EXPECT_EQ(ImportedPasswordForm::Scheme::kHtml, forms[0].scheme);
  EXPECT_FALSE(forms[0].blocked_by_user);
  // A site never to save for: no password to decrypt.
  EXPECT_TRUE(forms[1].blocked_by_user);
  EXPECT_TRUE(forms[1].password_value.empty());
  EXPECT_EQ(ImportedPasswordForm::Scheme::kBasic, forms[2].scheme);
  EXPECT_EQ(u"b4sic", forms[2].password_value);

  EXPECT_TRUE(ReadLogins(dir.GetPath().AppendASCII("missing"), key).empty());
}

}  // namespace koan::importer
