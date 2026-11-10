// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UPDATES_KOAN_BUILD_INFO_H_
#define KOAN_BROWSER_UPDATES_KOAN_BUILD_INFO_H_

#include <string>
#include <string_view>

#include "base/time/time.h"

// The Koan commit this build was made from, stamped at build time
// (lastchange.py). Empty without git or outside a clone.
namespace koan::build_info {

// The full commit hash, or empty.
std::string_view Commit();
// When it was committed (null without a commit).
base::Time CommitTime();
// "owner/name" of the clone's `origin` on GitHub, or empty for another
// remote (or none): then Koan doesn't check for updates.
std::string_view GitHubRepo();
// This build: the Chromium version and the commit (updates::BuildStamp()).
std::string Stamp();

}  // namespace koan::build_info

#endif  // KOAN_BROWSER_UPDATES_KOAN_BUILD_INFO_H_
