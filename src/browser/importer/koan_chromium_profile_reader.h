// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_READER_H_
#define KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_READER_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/time/time.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/user_data_importer/common/importer_url_row.h"

// Reads another Chromium-based browser's profile folder (Arc, Chrome, Brave,
// Edge...; #348) into the importer's types, for ProfileWriter: Bookmarks
// (and AccountBookmarks) JSON, Arc's StorableSidebar.json, the History and
// Login Data SQLite files (read from a copy, as the browser may hold them).
// Blocking file IO, on a thread pool sequence. Pure: unit-tested in
// //koan/test:koan_unittests.
namespace koan::importer {

// The key that encrypts a Chromium profile's saved passwords on macOS:
// PBKDF2-HMAC-SHA1 of the "<Browser> Safe Storage" keychain secret, salt
// "saltysalt", 1003 rounds, 16 bytes (as os_crypt's keychain key provider).
using SafeStorageKey = std::array<uint8_t, 16>;
SafeStorageKey DeriveSafeStorageKey(std::string_view keychain_secret);

// A saved password's value ("v10" + AES-128-CBC with a 16-space IV, as
// os_crypt on macOS), or nullopt if it isn't one or doesn't decrypt.
std::optional<std::u16string> DecryptPassword(
    base::span<const uint8_t> encrypted,
    const SafeStorageKey& key);

// Chromium's times: microseconds since 1601 (base::Time's internal value).
base::Time FromChromiumTime(int64_t microseconds);

// The bookmarks of a Bookmarks JSON file: the bookmarks bar's in the
// toolbar, the others under their root's name ("Other bookmarks", "Mobile
// bookmarks"), each with its folders as its path; empty folders too.
std::vector<user_data_importer::ImportedBookmarkEntry> ParseBookmarksJson(
    std::string_view json);

// Arc's pinned tabs of profile `profile_dir_name` (its folder in "User
// Data", "Default" for the main one) from StorableSidebar.json, as
// bookmarks: each Space's pinned tabs in a folder named after the Space
// (Arc's folders inside it), the Favorites (top apps) in "Favorites"
// (ParseArcSpaces(), koan_imported_spaces.h). What the import brings when
// "Spaces and pinned tabs" isn't asked for.
std::vector<user_data_importer::ImportedBookmarkEntry> ParseArcSidebar(
    std::string_view json,
    std::string_view profile_dir_name);

// The pages of a History file, for the history (http, https, ftp and file
// URLs only).
std::vector<user_data_importer::ImporterURLRow> ReadHistory(
    const base::FilePath& history_file);

// The saved passwords (and sites never to save for) of a Login Data file,
// decrypted with `key`. Forms of a scheme the importer can't take (digest,
// federated) and passwords that don't decrypt are left out.
std::vector<user_data_importer::ImportedPasswordForm> ReadLogins(
    const base::FilePath& login_data_file,
    const SafeStorageKey& key);

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_READER_H_
