// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_chromium_profile_reader.h"

#include <array>
#include <utility>

#include "base/check.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ref.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "crypto/aes_cbc.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace koan::importer {

namespace {

using user_data_importer::ImportedBookmarkEntry;
using user_data_importer::ImportedPasswordForm;
using user_data_importer::ImporterURLRow;

// The tag of the importers' reads of other browsers' files (sql/
// histograms.xml has no other importer tag; Koan reports no metrics).
inline constexpr sql::Database::Tag kDatabaseTag{"FirefoxImporter"};

// os_crypt's macOS parameters (keychain_key_provider.mm, os_crypt_mac.mm).
constexpr std::string_view kEncryptionPrefix = "v10";
constexpr std::string_view kSalt = "saltysalt";
constexpr uint32_t kIterations = 1003;

// Folders deeper than this are cut (a cycle in Arc's sidebar, a hostile
// file).
constexpr int kMaxDepth = 32;

// The pages and pinned tabs worth importing (as Zen's Arc import).
bool IsImportableUrl(const GURL& url) {
  return url.is_valid() &&
         (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs(url::kFtpScheme) ||
          url.SchemeIsFile());
}

// Reads `file` from a copy in `temp_dir` (with its journal or WAL): the
// browser may be running and hold it locked.
bool OpenCopy(const base::FilePath& file,
              base::ScopedTempDir& temp_dir,
              sql::Database& db) {
  if (!base::PathExists(file) || !temp_dir.CreateUniqueTempDir()) {
    return false;
  }
  const base::FilePath copy = temp_dir.GetPath().Append(file.BaseName());
  if (!base::CopyFile(file, copy)) {
    return false;
  }
  for (const char* suffix : {"-journal", "-wal"}) {
    const base::FilePath side(file.value() + suffix);
    if (base::PathExists(side)) {
      base::CopyFile(side, base::FilePath(copy.value() + suffix));
    }
  }
  return db.Open(copy);
}

// Chromium's Bookmarks JSON: a node and, for a folder, its children.
void AddBookmarkNode(const base::DictValue& node,
                     bool in_toolbar,
                     std::vector<std::u16string>& path,
                     int depth,
                     std::vector<ImportedBookmarkEntry>& entries) {
  const std::string* type = node.FindString("type");
  const std::string* name = node.FindString("name");
  if (!type || depth > kMaxDepth) {
    return;
  }
  ImportedBookmarkEntry entry;
  entry.in_toolbar = in_toolbar;
  entry.path = path;
  entry.title = name ? base::UTF8ToUTF16(*name) : std::u16string();
  int64_t added = 0;
  const std::string* date_added = node.FindString("date_added");
  entry.creation_time = date_added && base::StringToInt64(*date_added, &added)
                            ? FromChromiumTime(added)
                            : base::Time::Now();
  if (*type == "url") {
    const std::string* url = node.FindString("url");
    entry.url = url ? GURL(*url) : GURL();
    if (entry.url.is_valid()) {
      entries.push_back(std::move(entry));
    }
    return;
  }
  if (*type != "folder") {
    return;
  }
  const base::ListValue* children = node.FindList("children");
  if (!children || children->empty()) {
    entry.is_folder = true;
    entries.push_back(std::move(entry));
    return;
  }
  path.push_back(entry.title);
  for (const base::Value& child : *children) {
    if (child.is_dict()) {
      AddBookmarkNode(child.GetDict(), in_toolbar, path, depth + 1, entries);
    }
  }
  path.pop_back();
}

// Arc stores Swift dictionaries as flat lists: key, value, key, value.
template <typename Fn>
void ForEachPair(const base::ListValue* list, Fn fn) {
  if (!list) {
    return;
  }
  for (size_t i = 0; i + 1 < list->size(); i += 2) {
    fn((*list)[i], (*list)[i + 1]);
  }
}

// The profile folder an Arc profile marker names: {"default": {}} for the
// main one, {"custom": {"_0": {"directoryBasename": ...}}} for the others.
std::string ArcProfileOf(const base::Value& marker) {
  if (const base::DictValue* dict = marker.GetIfDict()) {
    if (const std::string* dir =
            dict->FindStringByDottedPath("custom._0.directoryBasename");
        dir && !dir->empty()) {
      return *dir;
    }
  }
  return "Default";
}

class ArcSidebarReader {
 public:
  ArcSidebarReader(const base::DictValue& container,
                   std::vector<ImportedBookmarkEntry>& entries)
      : entries_(entries) {
    ForEachPair(container.FindList("items"),
                [this](const base::Value&, const base::Value& item) {
                  if (const base::DictValue* dict = item.GetIfDict()) {
                    if (const std::string* id = dict->FindString("id")) {
                      items_.emplace(*id, dict);
                      order_.push_back(dict);
                    }
                  }
                });
  }

  // The tabs and folders under item `id`, as bookmarks in `path`.
  void Add(const std::string& id,
           std::vector<std::u16string>& path,
           int depth) {
    if (depth > kMaxDepth) {
      return;
    }
    for (const base::DictValue* item : ChildrenOf(id)) {
      const base::DictValue* data = item->FindDict("data");
      const std::string* item_id = item->FindString("id");
      const std::string* title = item->FindString("title");
      if (!data || !item_id) {
        continue;
      }
      if (const base::DictValue* tab = data->FindDict("tab")) {
        const std::string* url = tab->FindString("savedURL");
        const std::string* saved_title = tab->FindString("savedTitle");
        ImportedBookmarkEntry entry;
        entry.url = url ? GURL(*url) : GURL();
        if (!IsImportableUrl(entry.url)) {
          continue;
        }
        entry.path = path;
        entry.title = base::UTF8ToUTF16(saved_title && !saved_title->empty()
                                            ? *saved_title
                                        : title ? *title
                                                : std::string());
        entry.creation_time = base::Time::Now();
        entries_->push_back(std::move(entry));
      } else if (data->FindDict("list")) {
        // A folder; an empty one is left out.
        path.push_back(title && !title->empty() ? base::UTF8ToUTF16(*title)
                                                : u"Folder");
        Add(*item_id, path, depth + 1);
        path.pop_back();
      } else if (data->FindDict("splitView")) {
        // A split view's tabs, side by side in the folder.
        Add(*item_id, path, depth + 1);
      }
    }
  }

 private:
  // An item's children in its childrenIds order, else the items whose
  // parentID it is, in the file's order.
  std::vector<const base::DictValue*> ChildrenOf(const std::string& id) {
    std::vector<const base::DictValue*> children;
    if (const auto it = items_.find(id); it != items_.end()) {
      if (const base::ListValue* ids = it->second->FindList("childrenIds")) {
        for (const base::Value& child_id : *ids) {
          if (const std::string* child = child_id.GetIfString()) {
            if (const auto child_it = items_.find(*child);
                child_it != items_.end()) {
              children.push_back(child_it->second);
            }
          }
        }
      }
    }
    if (children.empty()) {
      for (const base::DictValue* item : order_) {
        if (const std::string* parent = item->FindString("parentID");
            parent && *parent == id) {
          children.push_back(item);
        }
      }
    }
    return children;
  }

  const raw_ref<std::vector<ImportedBookmarkEntry>> entries_;
  base::flat_map<std::string, const base::DictValue*> items_;
  std::vector<const base::DictValue*> order_;
};

}  // namespace

SafeStorageKey DeriveSafeStorageKey(std::string_view keychain_secret) {
  SafeStorageKey key;
  const base::span<const uint8_t> salt = base::as_byte_span(kSalt);
  CHECK(PKCS5_PBKDF2_HMAC_SHA1(keychain_secret.data(), keychain_secret.size(),
                               salt.data(), salt.size(), kIterations,
                               key.size(), key.data()));
  return key;
}

std::optional<std::u16string> DecryptPassword(
    base::span<const uint8_t> encrypted,
    const SafeStorageKey& key) {
  if (!base::as_string_view(encrypted).starts_with(kEncryptionPrefix)) {
    return std::nullopt;
  }
  static constexpr std::array<uint8_t, crypto::aes_cbc::kBlockSize> kIv = {
      ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
      ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  std::optional<std::vector<uint8_t>> plain = crypto::aes_cbc::Decrypt(
      key, kIv, encrypted.subspan(kEncryptionPrefix.size()));
  if (!plain || !base::IsStringUTF8(base::as_string_view(*plain))) {
    return std::nullopt;
  }
  return base::UTF8ToUTF16(base::as_string_view(*plain));
}

base::Time FromChromiumTime(int64_t microseconds) {
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

std::vector<ImportedBookmarkEntry> ParseBookmarksJson(std::string_view json) {
  std::vector<ImportedBookmarkEntry> entries;
  std::optional<base::DictValue> file =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::DictValue* roots = file ? file->FindDict("roots") : nullptr;
  if (!roots) {
    return entries;
  }
  static constexpr struct {
    const char* key;
    const char16_t* fallback_name;
    bool in_toolbar;
  } kRoots[] = {
      {"bookmark_bar", u"Bookmarks bar", true},
      {"other", u"Other bookmarks", false},
      {"synced", u"Mobile bookmarks", false},
  };
  for (const auto& root : kRoots) {
    const base::DictValue* node = roots->FindDict(root.key);
    const base::ListValue* children =
        node ? node->FindList("children") : nullptr;
    if (!children) {
      continue;
    }
    const std::string* name = node->FindString("name");
    std::vector<std::u16string> path = {
        name && !name->empty() ? base::UTF8ToUTF16(*name)
                               : std::u16string(root.fallback_name)};
    for (const base::Value& child : *children) {
      if (child.is_dict()) {
        AddBookmarkNode(child.GetDict(), root.in_toolbar, path, 1, entries);
      }
    }
  }
  return entries;
}

std::vector<ImportedBookmarkEntry> ParseArcSidebar(
    std::string_view json,
    std::string_view profile_dir_name) {
  std::vector<ImportedBookmarkEntry> entries;
  std::optional<base::DictValue> file =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* containers =
      file ? file->FindListByDottedPath("sidebar.containers") : nullptr;
  // The window's sidebar is the "global" container (the others are Little
  // Arc windows' unpinned tabs).
  ForEachPair(containers, [&](const base::Value& kind,
                              const base::Value& value) {
    const base::DictValue* container = value.GetIfDict();
    if (!kind.is_dict() || !kind.GetDict().Find("global") || !container) {
      return;
    }
    ArcSidebarReader reader(*container, entries);
    // The Favorites (top apps, Zen's Essentials).
    ForEachPair(
        container->FindList("topAppsContainerIDs"),
        [&](const base::Value& marker, const base::Value& id) {
          if (id.is_string() && ArcProfileOf(marker) == profile_dir_name) {
            std::vector<std::u16string> path = {u"Favorites"};
            reader.Add(id.GetString(), path, 0);
          }
        });
    // Each Space's pinned tabs, in a folder named after it.
    int index = 0;
    ForEachPair(container->FindList("spaces"), [&](const base::Value&,
                                                   const base::Value& value) {
      const base::DictValue* space = value.GetIfDict();
      if (!space) {
        return;
      }
      ++index;
      const base::Value* profile = space->Find("profile");
      if ((profile ? ArcProfileOf(*profile) : "Default") != profile_dir_name) {
        return;
      }
      std::optional<std::string> pinned;
      ForEachPair(space->FindList("containerIDs"),
                  [&](const base::Value& key, const base::Value& id) {
                    if (!pinned && key.is_string() &&
                        key.GetString() == "pinned" && id.is_string()) {
                      pinned = id.GetString();
                    }
                  });
      if (!pinned) {
        return;
      }
      const std::string* id = space->FindString("id");
      const std::string* title = space->FindString("title");
      std::u16string name;
      if (title) {
        name = base::UTF8ToUTF16(
            base::TrimWhitespaceASCII(*title, base::TRIM_ALL));
      }
      if (name.empty()) {
        name = id && *id == "thebrowser.company."
                            "defaultPersonalSpaceID"
                   ? u"Personal"
                   : u"Space " + base::NumberToString16(index);
      }
      std::vector<std::u16string> path = {name};
      reader.Add(*pinned, path, 0);
    });
  });
  return entries;
}

std::vector<ImporterURLRow> ReadHistory(const base::FilePath& history_file) {
  std::vector<ImporterURLRow> rows;
  base::ScopedTempDir temp_dir;
  sql::Database db(kDatabaseTag);
  static constexpr char kQuery[] =
      "SELECT url, title, visit_count, typed_count, last_visit_time, hidden "
      "FROM urls";
  if (!OpenCopy(history_file, temp_dir, db) || !db.IsSQLValid(kQuery)) {
    return rows;
  }
  sql::Statement s(db.GetUniqueStatement(kQuery));
  while (s.Step()) {
    GURL url(s.ColumnStringView(0));
    if (!IsImportableUrl(url)) {
      continue;
    }
    ImporterURLRow row(url);
    row.title = s.ColumnString16(1);
    row.visit_count = s.ColumnInt(2);
    row.typed_count = s.ColumnInt(3);
    row.last_visit = s.ColumnTime(4);
    row.hidden = s.ColumnBool(5);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<ImportedPasswordForm> ReadLogins(
    const base::FilePath& login_data_file,
    const SafeStorageKey& key) {
  std::vector<ImportedPasswordForm> forms;
  base::ScopedTempDir temp_dir;
  sql::Database db(kDatabaseTag);
  static constexpr char kQuery[] =
      "SELECT origin_url, action_url, username_element, username_value, "
      "password_element, password_value, signon_realm, blacklisted_by_user, "
      "scheme FROM logins";
  if (!OpenCopy(login_data_file, temp_dir, db) || !db.IsSQLValid(kQuery)) {
    return forms;
  }
  sql::Statement s(db.GetUniqueStatement(kQuery));
  while (s.Step()) {
    ImportedPasswordForm form;
    // Chromium's PasswordForm::Scheme: 0 HTML, 1 Basic (2 Digest, 3 Other,
    // 4 Username only have no importer form).
    switch (s.ColumnInt(8)) {
      case 0:
        form.scheme = ImportedPasswordForm::Scheme::kHtml;
        break;
      case 1:
        form.scheme = ImportedPasswordForm::Scheme::kBasic;
        break;
      default:
        continue;
    }
    form.url = GURL(s.ColumnStringView(0));
    form.action = GURL(s.ColumnStringView(1));
    form.username_element = s.ColumnString16(2);
    form.username_value = s.ColumnString16(3);
    form.password_element = s.ColumnString16(4);
    form.signon_realm = s.ColumnString(6);
    form.blocked_by_user = s.ColumnBool(7);
    if (!form.url.is_valid() || form.signon_realm.empty()) {
      continue;
    }
    if (!form.blocked_by_user) {
      // A federated sign-in has no password: nothing to import.
      std::optional<std::u16string> password =
          DecryptPassword(s.ColumnBlob(5), key);
      if (!password || password->empty()) {
        continue;
      }
      form.password_value = std::move(*password);
    }
    forms.push_back(std::move(form));
  }
  return forms;
}

}  // namespace koan::importer
