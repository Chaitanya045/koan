// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UPDATES_KOAN_UPDATE_LOGIC_H_
#define KOAN_BROWSER_UPDATES_KOAN_UPDATE_LOGIC_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "url/gurl.h"

// Koan has no binary releases: a clone is built from source
// (scripts/build.sh) and updated with `git pull && scripts/build.sh`. So an
// update is a newer commit of the repository's develop branch on GitHub than
// the commit a build was made from (stamped at build time, koan_build_info.h).
// Pure, so the unit tests need nothing else.
namespace koan::updates {

// The branch a clone follows (docs/BUILDING-MAC.md).
inline constexpr char kBranch[] = "develop";

// The repository the links point at when the build's clone has no GitHub
// remote.
inline constexpr char kDefaultGitHubRepo[] = "Chaitanya045/koan";

// At most one check a day.
inline constexpr base::TimeDelta kCheckInterval = base::Days(1);
// Not at startup: a minute after the first window.
inline constexpr base::TimeDelta kFirstCheckDelay = base::Minutes(1);

// The update command, as docs/BUILDING-MAC.md gives it.
inline constexpr char16_t kUpdateCommand[] = u"git pull && scripts/build.sh";

struct Commit {
  std::string sha;
  base::Time time;
};

// The newest commit in a GitHub "list commits" response (a JSON array of
// commits, newest first), or nullopt if `json` isn't one.
std::optional<Commit> ParseLatestCommit(std::string_view json);

// A GitHub API timestamp, "2026-11-10T12:34:56Z" (UTC, ISO 8601).
std::optional<base::Time> ParseGitHubTime(std::string_view text);

// Whether `latest` (the branch on GitHub) is newer than the commit a build
// was made from (`built_sha`, committed at `built_time`). Only a newer
// commit: a clone with local commits made after the branch's newest isn't
// behind it. An unstamped build (empty sha) never is.
bool IsUpdateAvailable(std::string_view built_sha,
                       base::Time built_time,
                       const Commit& latest);

// How long to wait before the next check, the last one being at `last_check`
// (null if never): kCheckInterval after it, but no sooner than
// kFirstCheckDelay from `now`. A `last_check` in the future (a clock that
// went back) counts as now.
base::TimeDelta NextCheckDelay(base::Time last_check, base::Time now);

// What makes one build differ from another: the Chromium version and the
// Koan commit, e.g. "158.0.8127.2/67585c575222". Zen compares its version and
// build ID the same way (ZenUpdates.mjs).
std::string BuildStamp(std::string_view version, std::string_view commit);

// The commit of a BuildStamp() (empty if it has none).
std::string_view CommitOfStamp(std::string_view stamp);

// The API request: the branch's newest commit, nothing about the user.
GURL LatestCommitUrl(std::string_view repo);

// "What's new": the commits from `from` to `to` (commits or branches) on
// GitHub, or the branch's history without a `from`.
GURL WhatsNewUrl(std::string_view repo,
                 std::string_view from,
                 std::string_view to);

// How to update, in the clone's own docs.
GURL HowToUpdateUrl(std::string_view repo);

// Where to report what broke.
GURL IssuesUrl(std::string_view repo);

}  // namespace koan::updates

#endif  // KOAN_BROWSER_UPDATES_KOAN_UPDATE_LOGIC_H_
