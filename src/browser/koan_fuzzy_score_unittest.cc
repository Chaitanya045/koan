// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_fuzzy_score.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanFuzzyScoreTest, NoMatch) {
  EXPECT_EQ(0, FuzzyScore(u"toggle compact mode", u"tcz"));
  // Characters must come in the query's order.
  EXPECT_EQ(0, FuzzyScore(u"toggle compact mode", u"modet"));
}

TEST(KoanFuzzyScoreTest, EmptyOrTooLongQuery) {
  EXPECT_EQ(0, FuzzyScore(u"", u"a"));
  EXPECT_EQ(0, FuzzyScore(u"toggle", u""));
  EXPECT_EQ(0, FuzzyScore(u"tab", u"tabs"));
}

TEST(KoanFuzzyScoreTest, ExactMatchBeatsEverything) {
  EXPECT_EQ(kExactMatchScore, FuzzyScore(u"new tab", u"new tab"));
  EXPECT_GT(kExactMatchScore, kPrefixMatchScore);
}

TEST(KoanFuzzyScoreTest, PrefixMatchScoresByQueryLength) {
  EXPECT_EQ(kPrefixMatchScore + 6,
            FuzzyScore(u"toggle compact mode", u"toggle"));
  EXPECT_GT(FuzzyScore(u"toggle compact mode", u"toggle c"),
            FuzzyScore(u"toggle compact mode", u"toggle"));
}

TEST(KoanFuzzyScoreTest, PrefixOutranksFuzzy) {
  EXPECT_GT(FuzzyScore(u"settings", u"set"),
            FuzzyScore(u"toggle compact mode", u"tcm"));
  // Even a long fuzzy match stays below the lowest prefix match.
  EXPECT_GT(kPrefixMatchScore, FuzzyScore(u"toggle compact mode", u"tcm"));
}

TEST(KoanFuzzyScoreTest, KnownScores) {
  // Zen's scoring, worked out by hand: 10 per match, +15 at a word start,
  // +20 per step of a run, minus the gap (at most 10) since the last match.
  EXPECT_EQ(73, FuzzyScore(u"toggle compact mode", u"tcm"));
  EXPECT_EQ(63, FuzzyScore(u"toggle compact mode", u"tgl"));
  EXPECT_EQ(94, FuzzyScore(u"toggle compact mode", u"mode"));
  EXPECT_EQ(505, FuzzyScore(u"toggle compact mode", u"compact"));
  EXPECT_EQ(64, FuzzyScore(u"unload space", u"us"));
  EXPECT_EQ(28, FuzzyScore(u"xaxbxc", u"abc"));
}

TEST(KoanFuzzyScoreTest, WordStartsBeatMidWordMatches) {
  // "us" hits the starts of "unload" and "space"; "ls" hits the end of one.
  EXPECT_GT(FuzzyScore(u"unload space", u"us"),
            FuzzyScore(u"unload space", u"ls"));
}

TEST(KoanFuzzyScoreTest, RunsBeatScatteredMatches) {
  EXPECT_GT(FuzzyScore(u"abcd", u"abd"), FuzzyScore(u"xaxbxd", u"abd"));
  EXPECT_GT(FuzzyScore(u"abcd", u"bcd"), FuzzyScore(u"bxcxd", u"bcd"));
}

TEST(KoanFuzzyScoreTest, WordSeparators) {
  // Words start after a space, a dash and an underscore.
  EXPECT_EQ(FuzzyScore(u"a b", u"ab"), FuzzyScore(u"a-b", u"ab"));
  EXPECT_EQ(FuzzyScore(u"a b", u"ab"), FuzzyScore(u"a_b", u"ab"));
  EXPECT_GT(FuzzyScore(u"a b", u"ab"), FuzzyScore(u"axb", u"ab"));
}

}  // namespace koan
