// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_space_title.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

using ::testing::Pair;

TEST(KoanSpaceTitleTest, SplitsIconAndName) {
  EXPECT_THAT(SplitSpaceTitle(u"\U0001F680 Work"),
              Pair(u"\U0001F680", u"Work"));
  EXPECT_THAT(SplitSpaceTitle(u"\U0001F680 Side projects"),
              Pair(u"\U0001F680", u"Side projects"));
}

TEST(KoanSpaceTitleTest, PlainName) {
  EXPECT_THAT(SplitSpaceTitle(u"Work"), Pair(u"", u"Work"));
  // Digits and punctuation aren't emoji.
  EXPECT_THAT(SplitSpaceTitle(u"1 Space"), Pair(u"", u"1 Space"));
  EXPECT_THAT(SplitSpaceTitle(u"-> Next"), Pair(u"", u"-> Next"));
}

TEST(KoanSpaceTitleTest, EmptyAndWhitespace) {
  EXPECT_THAT(SplitSpaceTitle(u""), Pair(u"", u""));
  EXPECT_THAT(SplitSpaceTitle(u"   "), Pair(u"", u""));
  EXPECT_THAT(SplitSpaceTitle(u"  \U0001F680   Work  "),
              Pair(u"\U0001F680", u"Work"));
}

TEST(KoanSpaceTitleTest, IconOnly) {
  EXPECT_THAT(SplitSpaceTitle(u"\U0001F680"), Pair(u"\U0001F680", u""));
}

TEST(KoanSpaceTitleTest, TextSymbolsAreNotIcons) {
  // (c) is text by default: only the emoji variation selector makes it one.
  EXPECT_THAT(SplitSpaceTitle(u"© Legal"), Pair(u"", u"© Legal"));
  EXPECT_THAT(SplitSpaceTitle(u"©️ Legal"), Pair(u"©️", u"Legal"));
}

TEST(KoanSpaceTitleTest, ClustersStayWhole) {
  // A flag is two regional indicators.
  EXPECT_THAT(SplitSpaceTitle(u"\U0001F1EF\U0001F1F5 Japan"),
              Pair(u"\U0001F1EF\U0001F1F5", u"Japan"));
  // A ZWJ sequence: man, woman, girl.
  const std::u16string family = u"\U0001F468‍\U0001F469‍\U0001F467";
  EXPECT_THAT(SplitSpaceTitle(family + u" Family"), Pair(family, u"Family"));
}

TEST(KoanSpaceTitleTest, JoinsIconAndName) {
  EXPECT_EQ(u"\U0001F680 Work", JoinSpaceTitle(u"\U0001F680", u"Work"));
  EXPECT_EQ(u"\U0001F680", JoinSpaceTitle(u"\U0001F680", u""));
  EXPECT_EQ(u"Work", JoinSpaceTitle(u"", u"Work"));
  EXPECT_EQ(u"", JoinSpaceTitle(u"", u""));
}

TEST(KoanSpaceTitleTest, SplitInvertsJoin) {
  for (const auto& [icon, name] :
       {std::pair<std::u16string, std::u16string>{u"\U0001F680", u"Work"},
        {u"", u"Plain"},
        {u"\U0001F1EF\U0001F1F5", u""},
        {u"\U0001F680", u"Two words"}}) {
    EXPECT_THAT(SplitSpaceTitle(JoinSpaceTitle(icon, name)), Pair(icon, name));
  }
}

}  // namespace koan
