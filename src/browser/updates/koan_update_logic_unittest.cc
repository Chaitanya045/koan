// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_logic.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::updates {
namespace {

constexpr char kSha[] = "67585c5752228beb631820cda5bf5bfdfb944286";
constexpr char kOtherSha[] = "1111111111111111111111111111111111111111";

base::Time At(const char* text) {
  return ParseGitHubTime(text).value();
}

TEST(KoanUpdateLogicTest, ParsesGitHubTimes) {
  base::Time::Exploded exploded;
  At("2026-11-10T12:34:56Z").UTCExplode(&exploded);
  EXPECT_EQ(exploded.year, 2026);
  EXPECT_EQ(exploded.month, 11);
  EXPECT_EQ(exploded.day_of_month, 10);
  EXPECT_EQ(exploded.hour, 12);
  EXPECT_EQ(exploded.minute, 34);
  EXPECT_EQ(exploded.second, 56);
  EXPECT_FALSE(ParseGitHubTime("2026-11-10 12:34:56Z"));
  EXPECT_FALSE(ParseGitHubTime("2026-11-10T12:34:56+01:00"));
  EXPECT_FALSE(ParseGitHubTime("2026-13-10T12:34:56Z"));
  EXPECT_FALSE(ParseGitHubTime(""));
}

TEST(KoanUpdateLogicTest, ParsesTheNewestCommit) {
  const std::optional<Commit> commit = ParseLatestCommit(
      R"([{"sha": "67585c5752228beb631820cda5bf5bfdfb944286",
           "commit": {"committer": {"date": "2026-11-10T12:34:56Z"}}},
          {"sha": "1111111111111111111111111111111111111111",
           "commit": {"committer": {"date": "2026-11-09T00:00:00Z"}}}])");
  ASSERT_TRUE(commit);
  EXPECT_EQ(commit->sha, kSha);
  EXPECT_EQ(commit->time, At("2026-11-10T12:34:56Z"));
}

TEST(KoanUpdateLogicTest, RejectsOtherResponses) {
  // GitHub's error body (rate limited, not found).
  EXPECT_FALSE(ParseLatestCommit(R"({"message": "API rate limit exceeded"})"));
  EXPECT_FALSE(ParseLatestCommit("[]"));
  EXPECT_FALSE(ParseLatestCommit("not json"));
  EXPECT_FALSE(ParseLatestCommit(
      R"([{"sha": "nothex", "commit": {"committer": {"date":
          "2026-11-10T12:34:56Z"}}}])"));
  EXPECT_FALSE(ParseLatestCommit(
      R"([{"sha": "67585c5752228beb631820cda5bf5bfdfb944286"}])"));
}

TEST(KoanUpdateLogicTest, OnlyANewerCommitIsAnUpdate) {
  const base::Time built = At("2026-11-10T12:00:00Z");
  EXPECT_TRUE(
      IsUpdateAvailable(kSha, built, {kOtherSha, At("2026-11-11T08:00:00Z")}));
  // Up to date.
  EXPECT_FALSE(IsUpdateAvailable(kSha, built, {kSha, built}));
  // Local commits newer than the branch.
  EXPECT_FALSE(
      IsUpdateAvailable(kSha, built, {kOtherSha, At("2026-11-09T08:00:00Z")}));
  // An unstamped build.
  EXPECT_FALSE(
      IsUpdateAvailable("", built, {kOtherSha, At("2026-11-11T08:00:00Z")}));
}

TEST(KoanUpdateLogicTest, ChecksAtMostDaily) {
  const base::Time now = At("2026-11-11T12:00:00Z");
  EXPECT_EQ(NextCheckDelay(base::Time(), now), kFirstCheckDelay);
  EXPECT_EQ(NextCheckDelay(now - base::Hours(3), now), base::Hours(21));
  // Overdue: soon, not at once.
  EXPECT_EQ(NextCheckDelay(now - base::Days(3), now), kFirstCheckDelay);
  // A clock that went back.
  EXPECT_EQ(NextCheckDelay(now + base::Days(2), now), kCheckInterval);
}

TEST(KoanUpdateLogicTest, StampsVersionAndCommit) {
  EXPECT_EQ(BuildStamp("158.0.8127.2", kSha), "158.0.8127.2/67585c575222");
  EXPECT_EQ(BuildStamp("158.0.8127.2", ""), "158.0.8127.2/");
  EXPECT_EQ(CommitOfStamp("158.0.8127.2/67585c575222"), "67585c575222");
  EXPECT_EQ(CommitOfStamp(""), "");
}

TEST(KoanUpdateLogicTest, Urls) {
  // Nothing but the repository and the branch.
  EXPECT_EQ(LatestCommitUrl("Chaitanya045/koan").spec(),
            "https://api.github.com/repos/Chaitanya045/koan/commits"
            "?sha=develop&per_page=1");
  EXPECT_EQ(WhatsNewUrl("o/r", "aaa", "bbb").spec(),
            "https://github.com/o/r/compare/aaa...bbb");
  EXPECT_EQ(WhatsNewUrl("o/r", "", "bbb").spec(),
            "https://github.com/o/r/commits/develop");
  EXPECT_EQ(
      HowToUpdateUrl("o/r").spec(),
      "https://github.com/o/r/blob/develop/docs/BUILDING-MAC.md#updating");
}

}  // namespace
}  // namespace koan::updates
