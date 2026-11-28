// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_IMPORT_PLATFORM_H_
#define KOAN_BROWSER_IMPORTER_KOAN_IMPORT_PLATFORM_H_

#include <optional>
#include <string>
#include <string_view>

#include "build/build_config.h"

// What the import from other browsers needs from the system (#348, #493):
// koan_import_platform_mac.mm on macOS, koan_import_platform_linux.cc on
// Linux.
namespace koan::importer {

#if BUILDFLAG(IS_MAC)
// The secret of the keychain item `service` / `account` (another browser's
// "<Browser> Safe Storage"), or nullopt if there is none or the user denies
// it. Blocks: macOS asks the user whether Koan may read another app's item.
std::optional<std::string> ReadKeychainSecret(std::string_view service,
                                              std::string_view account);
#else
// The secret another Chromium-based browser keeps its Safe Storage key in:
// its Secret Service item (os_crypt's schema, "application" `application`),
// else its KWallet entry `key` in folder `kwallet_folder`; nullopt if there
// is neither, or it is locked or refused. Reads only that item, and never
// unlocks a locked keyring. Blocks on D-Bus: KWallet may ask the user
// whether Koan may open the wallet.
std::optional<std::string> ReadKeyringSecret(std::string_view application,
                                             std::string_view kwallet_folder,
                                             std::string_view key);
#endif

// The icon of the app with `bundle_id`, as a data: URL for the welcome's
// import step; empty if the app isn't installed. macOS: its app's icon,
// 64 px. Linux: the icon found when the sources were detected
// (LoadAppIcons(), koan_import_sources.h), so no file is read here.
std::string AppIconDataUrl(std::string_view bundle_id);

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_IMPORT_PLATFORM_H_
