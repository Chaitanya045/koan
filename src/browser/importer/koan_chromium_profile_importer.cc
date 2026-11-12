// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_chromium_profile_importer.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/in_process_importer_bridge.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/common/importer/importer_bridge.h"
#include "koan/browser/importer/koan_chromium_profile_reader.h"
#include "koan/browser/importer/koan_import_platform.h"
#include "koan/browser/importer/koan_import_sources.h"

namespace koan::importer {

namespace {

using user_data_importer::ImportItem;

struct ProfileData {
  std::vector<user_data_importer::ImportedBookmarkEntry> bookmarks;
  std::vector<user_data_importer::ImporterURLRow> history;
  std::vector<user_data_importer::ImportedPasswordForm> passwords;
};

// The browser's Safe Storage secret: from the keychain (macOS asks the
// user), or with --koan-import-test-root from its Keychain/<item> file.
std::optional<std::string> SafeStorageSecret(const KnownBrowser& browser) {
  if (HasTestRoot()) {
    std::string secret;
    if (!base::ReadFileToString(SearchRoot()
                                    .AppendASCII("Keychain")
                                    .AppendASCII(browser.keychain_service),
                                &secret)) {
      return std::nullopt;
    }
    return std::string(base::TrimWhitespaceASCII(secret, base::TRIM_ALL));
  }
  return ReadKeychainSecret(browser.keychain_service, browser.keychain_account);
}

// On the thread pool: what `items` asks for from `profile_dir`.
ProfileData ReadProfile(const base::FilePath& profile_dir, uint16_t items) {
  ProfileData data;
  const KnownBrowser* browser = FindBrowser(SearchRoot(), profile_dir);
  if (items & user_data_importer::FAVORITES) {
    for (const char* file : {"Bookmarks", "AccountBookmarks"}) {
      std::string json;
      if (base::ReadFileToString(profile_dir.AppendASCII(file), &json)) {
        std::ranges::move(ParseBookmarksJson(json),
                          std::back_inserter(data.bookmarks));
      }
    }
    std::string sidebar;
    if (browser && browser->arc_sidebar &&
        base::ReadFileToString(
            profile_dir.DirName().DirName().AppendASCII("StorableSidebar.json"),
            &sidebar)) {
      std::ranges::move(
          ParseArcSidebar(sidebar, profile_dir.BaseName().AsUTF8Unsafe()),
          std::back_inserter(data.bookmarks));
    }
  }
  if (items & user_data_importer::HISTORY) {
    data.history = ReadHistory(profile_dir.AppendASCII("History"));
  }
  const base::FilePath login_files[] = {
      profile_dir.AppendASCII("Login Data"),
      profile_dir.AppendASCII("Login Data For Account")};
  // The keychain is asked only when there are passwords to decrypt.
  if ((items & user_data_importer::PASSWORDS) && browser &&
      !browser->keychain_service.empty() &&
      std::ranges::any_of(login_files, [](const base::FilePath& file) {
        return base::PathExists(file);
      })) {
    if (std::optional<std::string> secret = SafeStorageSecret(*browser)) {
      const SafeStorageKey key = DeriveSafeStorageKey(*secret);
      for (const base::FilePath& file : login_files) {
        std::ranges::move(ReadLogins(file, key),
                          std::back_inserter(data.passwords));
      }
    } else {
      LOG(WARNING) << "Koan import: no " << browser->keychain_service
                   << " key (denied or missing); passwords not imported";
    }
  }
  return data;
}

// On the UI thread: hands `data` to the writer through `bridge`, item by
// item, as Chromium's utility process importers report theirs.
void WriteProfile(scoped_refptr<ImporterBridge> bridge,
                  base::WeakPtr<ExternalProcessImporterHost> host,
                  scoped_refptr<ProfileWriter> writer,
                  uint16_t items,
                  std::u16string browser_name,
                  ProfileData data) {
  // (The bridge writes to `writer`, which this keeps alive, and tells
  // `host`, which deletes itself once the import ends or is cancelled.)
  const auto write = [&](ImportItem item, auto add) {
    if (!host || !(items & item)) {
      return;
    }
    bridge->NotifyItemStarted(item);
    add();
    if (host) {
      bridge->NotifyItemEnded(item);
    }
  };
  // History first, as Chromium's importers do (the bookmarks' favicons
  // are kept only for URLs in the history or the bookmarks).
  write(user_data_importer::HISTORY, [&] {
    if (!data.history.empty()) {
      // History treats every imported source alike (visit_database.cc);
      // there is none for a Chromium browser.
      bridge->SetHistoryItems(data.history,
                              user_data_importer::VISIT_SOURCE_SAFARI_IMPORTED);
    }
  });
  write(user_data_importer::FAVORITES, [&] {
    if (!data.bookmarks.empty()) {
      bridge->AddBookmarks(data.bookmarks, u"Imported from " + browser_name);
    }
  });
  write(user_data_importer::PASSWORDS, [&] {
    for (const user_data_importer::ImportedPasswordForm& form :
         data.passwords) {
      bridge->SetPasswordForm(form);
    }
  });
  if (host) {
    bridge->NotifyEnded();
  }
}

}  // namespace

bool MaybeStartChromiumProfileImport(
    const user_data_importer::SourceProfile& source,
    uint16_t items,
    ProfileWriter* writer,
    base::WeakPtr<ExternalProcessImporterHost> host) {
  if (!IsChromiumProfileSource(source)) {
    return false;
  }
  scoped_refptr<ImporterBridge> bridge =
      base::MakeRefCounted<InProcessImporterBridge>(writer, host);
  bridge->NotifyStarted();
  // A keychain prompt may wait on the user: never block shutdown on it.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&ReadProfile, source.source_path, items),
      base::BindOnce(&WriteProfile, std::move(bridge), std::move(host),
                     base::WrapRefCounted(writer), items,
                     source.importer_name));
  return true;
}

}  // namespace koan::importer
