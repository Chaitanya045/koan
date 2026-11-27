// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_history_profiles.h"

#include <optional>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

constexpr char kWork[] = "user:swork";
constexpr char kHome[] = "user:shome";

TEST(HistoryProfilesTest, OnlySavedUserContainersAreTagged) {
  EXPECT_EQ("koan:user:swork", HistoryAppIdForProfile(kWork));
  // The default profile, an agent's container, a throwaway one and garbage
  // record no tag: their visits are the default profile's.
  EXPECT_EQ("", HistoryAppIdForProfile(""));
  EXPECT_EQ("", HistoryAppIdForProfile("agent:swork"));
  EXPECT_EQ("", HistoryAppIdForProfile("user:twork"));
  EXPECT_EQ("", HistoryAppIdForProfile("user:sWork"));
  EXPECT_EQ("", HistoryAppIdForProfile("none"));
}

TEST(HistoryProfilesTest, TagsReadBack) {
  EXPECT_EQ(kWork, HistoryProfileFromAppId("koan:user:swork"));
  // No tag (visits from before #474, the importer's, synced ones), an app's
  // own tag (Android) and a Koan tag that doesn't parse: the default
  // profile's.
  EXPECT_EQ("", HistoryProfileFromAppId(std::nullopt));
  EXPECT_EQ("", HistoryProfileFromAppId(""));
  EXPECT_EQ("", HistoryProfileFromAppId("com.example.app"));
  EXPECT_EQ("", HistoryProfileFromAppId("user:swork"));
  EXPECT_EQ("", HistoryProfileFromAppId("koan:agent:swork"));
  EXPECT_EQ("", HistoryProfileFromAppId("koan:user:twork"));
  EXPECT_EQ("", HistoryProfileFromAppId("koan:"));
}

TEST(HistoryProfilesTest, VisitKeepsUpstreamsAppId) {
  // Android's Custom Tab tag stays; a tab with no container records none.
  EXPECT_EQ("com.example.app", VisitAppId(nullptr, "com.example.app"));
  EXPECT_EQ(std::nullopt, VisitAppId(nullptr, std::nullopt));
}

TEST(HistoryProfilesTest, PageWithoutTaggedVisitsIsTheDefaults) {
  EXPECT_TRUE(PageShowsInProfile(nullptr, ""));
  EXPECT_FALSE(PageShowsInProfile(nullptr, kWork));
}

TEST(HistoryProfilesTest, PageShowsInEveryProfileItWasVisitedIn) {
  const PageProfiles work_only{.in_default = false, .profiles = {kWork}};
  EXPECT_TRUE(PageShowsInProfile(&work_only, kWork));
  EXPECT_FALSE(PageShowsInProfile(&work_only, ""));
  EXPECT_FALSE(PageShowsInProfile(&work_only, kHome));

  // Visited in Work and, before or after, in a Space without a profile.
  const PageProfiles both{.in_default = true, .profiles = {kWork}};
  EXPECT_TRUE(PageShowsInProfile(&both, kWork));
  EXPECT_TRUE(PageShowsInProfile(&both, ""));
  EXPECT_FALSE(PageShowsInProfile(&both, kHome));

  const PageProfiles two{.in_default = false, .profiles = {kWork, kHome}};
  EXPECT_TRUE(PageShowsInProfile(&two, kWork));
  EXPECT_TRUE(PageShowsInProfile(&two, kHome));
  EXPECT_FALSE(PageShowsInProfile(&two, ""));
}

}  // namespace
}  // namespace koan::containers
