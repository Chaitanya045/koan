// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_import_platform.h"

namespace koan::importer {

// Koan imports other browsers' passwords and shows their icons on macOS only.

std::optional<std::string> ReadKeychainSecret(std::string_view service,
                                              std::string_view account) {
  return std::nullopt;
}

std::string AppIconDataUrl(std::string_view bundle_id) {
  return std::string();
}

}  // namespace koan::importer
