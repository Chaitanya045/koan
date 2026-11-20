// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_VERSION_KOAN_VERSION_H_
#define KOAN_VERSION_KOAN_VERSION_H_

#include <string>
#include <string_view>

// Koan's own version (VERSION at the root of the repository), for display
// only. The User-Agent, client hints, the component updater and everything
// else a site or a server sees keep Chromium's version
// (version_info::GetVersionNumber()).
namespace koan::version {

// Koan's SemVer version, e.g. "0.0.2-alpha.1".
std::string_view Full();

// What About Koan and chrome://version show after the product name:
// "0.0.2 (Alpha) · Chromium 158.0.8127.18".
std::string DisplayVersion();

// DisplayVersion() from its parts; `label` may be empty ("1.0.0 · Chromium
// ..."). Exposed for the unit test.
std::string FormatDisplayVersion(std::string_view short_version,
                                 std::string_view label,
                                 std::string_view chromium_version);

}  // namespace koan::version

#endif  // KOAN_VERSION_KOAN_VERSION_H_
