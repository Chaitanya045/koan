// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_IMPORT_PLATFORM_H_
#define KOAN_BROWSER_IMPORTER_KOAN_IMPORT_PLATFORM_H_

#include <optional>
#include <string>
#include <string_view>

// What the import from other browsers needs from macOS (#348):
// koan_import_platform_mac.mm, and nothing elsewhere.
namespace koan::importer {

// The secret of the keychain item `service` / `account` (another browser's
// "<Browser> Safe Storage"), or nullopt if there is none or the user denies
// it. Blocks: macOS asks the user whether Koan may read another app's item.
std::optional<std::string> ReadKeychainSecret(std::string_view service,
                                              std::string_view account);

// The icon of the app with `bundle_id`, 64 px, as a data: URL for the
// welcome's import step; empty if the app isn't installed.
std::string AppIconDataUrl(std::string_view bundle_id);

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_IMPORT_PLATFORM_H_
