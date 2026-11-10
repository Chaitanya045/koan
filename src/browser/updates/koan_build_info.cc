// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_build_info.h"

#include "components/version_info/version_info.h"
#include "koan/browser/updates/koan_lastchange.h"
#include "koan/browser/updates/koan_update_logic.h"

namespace koan::build_info {

std::string_view Commit() {
  return KOAN_BUILD_COMMIT;
}

base::Time CommitTime() {
  return Commit().empty()
             ? base::Time()
             : base::Time::UnixEpoch() + base::Seconds(KOAN_BUILD_COMMIT_TIME);
}

std::string_view GitHubRepo() {
  return KOAN_BUILD_GITHUB_REPO;
}

std::string Stamp() {
  return updates::BuildStamp(version_info::GetVersionNumber(), Commit());
}

}  // namespace koan::build_info
