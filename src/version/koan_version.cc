// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/version/koan_version.h"

#include "base/strings/strcat.h"
#include "components/version_info/version_info.h"
#include "koan/version/koan_version_values.h"

namespace koan::version {

std::string_view Full() {
  return KOAN_VERSION_FULL;
}

std::string DisplayVersion() {
  return FormatDisplayVersion(KOAN_VERSION_SHORT, KOAN_VERSION_LABEL,
                              version_info::GetVersionNumber());
}

std::string FormatDisplayVersion(std::string_view short_version,
                                 std::string_view label,
                                 std::string_view chromium_version) {
  // U+00B7 MIDDLE DOT, as in the release titles.
  constexpr std::string_view kSeparator = " · Chromium ";
  if (label.empty()) {
    return base::StrCat({short_version, kSeparator, chromium_version});
  }
  return base::StrCat(
      {short_version, " (", label, ")", kSeparator, chromium_version});
}

}  // namespace koan::version
