// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_bookmark_spaces_rule.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::bookmark_spaces {
namespace {

// Three Spaces, "work" active.
Filter InWork() {
  return {.active = "work", .known = {"work", "home", "study"}};
}

TEST(KoanBookmarkSpacesRuleTest, ABookmarkOfNoSpaceShowsEverywhere) {
  EXPECT_TRUE(IsShown("", InWork()));
  EXPECT_TRUE(IsShown(",", InWork()));
}

TEST(KoanBookmarkSpacesRuleTest, ABookmarkOfTheActiveSpaceShows) {
  EXPECT_TRUE(IsShown("work", InWork()));
  // Also when it belongs to other Spaces too (Zen: "in the active one").
  EXPECT_TRUE(IsShown("home,work", InWork()));
}

TEST(KoanBookmarkSpacesRuleTest, ABookmarkOfOtherSpacesOnlyIsHidden) {
  EXPECT_FALSE(IsShown("home", InWork()));
  EXPECT_FALSE(IsShown("home,study", InWork()));
}

TEST(KoanBookmarkSpacesRuleTest, SpacesThatAreGoneDontCount) {
  // Its only Space was deleted: it belongs to none now and shows everywhere.
  EXPECT_TRUE(IsShown("deleted", InWork()));
  // One of its Spaces is gone, another one stays: that one decides.
  EXPECT_FALSE(IsShown("deleted,home", InWork()));
  EXPECT_TRUE(IsShown("deleted,work", InWork()));
}

TEST(KoanBookmarkSpacesRuleTest, ParseDropsEmptyAndRepeatedIds) {
  EXPECT_EQ(Parse(" a,,b ,a"), (std::vector<std::string>{"a", "b"}));
  EXPECT_TRUE(Parse("").empty());
}

TEST(KoanBookmarkSpacesRuleTest, SerializeRoundTripsAndDropsBadIds) {
  EXPECT_EQ(Serialize({"a", "b"}), "a,b");
  EXPECT_EQ(Parse(Serialize({"a", "b"})), (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(Serialize({"a", "", "a", "x,y"}), "a");
  EXPECT_EQ(Serialize({}), "");
}

TEST(KoanBookmarkSpacesRuleTest, WithoutRemovesADeletedSpace) {
  EXPECT_EQ(Without("work,home", "work"), "home");
  // Its last Space: no value, so the key goes and it shows everywhere.
  EXPECT_EQ(Without("work", "work"), "");
  EXPECT_EQ(Without("home", "work"), "home");
}

}  // namespace
}  // namespace koan::bookmark_spaces
