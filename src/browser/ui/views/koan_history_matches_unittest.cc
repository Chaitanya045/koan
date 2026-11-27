// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_history_matches.h"

#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/fake_autocomplete_provider.h"
#include "components/omnibox/browser/fake_autocomplete_provider_client.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "koan/browser/ui/views/koan_zero_suggest.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "url/gurl.h"

namespace koan {
namespace {

using Type = AutocompleteProvider::Type;
using MatchType = omnibox::AutocompleteMatchType;

class KoanHistoryMatchesTest : public testing::Test {
 protected:
  AutocompleteMatch Match(Type provider_type, MatchType type) {
    auto provider =
        base::MakeRefCounted<FakeAutocompleteProvider>(provider_type);
    providers_.push_back(provider);
    return AutocompleteMatch(provider.get(), 1000, false, type);
  }

  base::test::TaskEnvironment task_environment_;
  std::vector<scoped_refptr<AutocompleteProvider>> providers_;
};

TEST_F(KoanHistoryMatchesTest, HistoryProvidersMatchesAreHistory) {
  for (Type type : {Type::kHistoryQuick, Type::kHistoryUrl, Type::kHistoryFuzzy,
                    Type::kShortcuts, Type::kZeroSuggestLocalHistory,
                    Type::kMostVisitedSites, Type::kHistoryClusterProvider,
                    Type::kHistoryEmbeddings}) {
    EXPECT_TRUE(IsSpaceHistoryMatch(Match(type, MatchType::kHistoryUrl)))
        << AutocompleteProvider::TypeToString(type);
  }
  // A shortcut to a search, and the search provider's past searches.
  EXPECT_TRUE(IsSpaceHistoryMatch(
      Match(Type::kShortcuts, MatchType::kSearchWhatYouTyped)));
  EXPECT_TRUE(
      IsSpaceHistoryMatch(Match(Type::kSearch, MatchType::kSearchHistory)));
}

TEST_F(KoanHistoryMatchesTest, WhatYouTypedAndOtherSourcesAreNot) {
  // HistoryURLProvider's address as typed, before it is in history.
  EXPECT_FALSE(IsSpaceHistoryMatch(
      Match(Type::kHistoryUrl, MatchType::kUrlWhatYouTyped)));
  // Bookmarks stay shared; open tabs are the tab matcher's; search
  // suggestions and Koan's actions aren't history.
  EXPECT_FALSE(
      IsSpaceHistoryMatch(Match(Type::kBookmark, MatchType::kBookmarkTitle)));
  EXPECT_FALSE(IsSpaceHistoryMatch(Match(Type::kOpenTab, MatchType::kOpenTab)));
  EXPECT_FALSE(
      IsSpaceHistoryMatch(Match(Type::kSearch, MatchType::kSearchSuggest)));
  EXPECT_FALSE(
      IsSpaceHistoryMatch(Match(Type::kBuiltin, MatchType::kNavsuggest)));
  EXPECT_FALSE(IsSpaceHistoryMatch(
      AutocompleteMatch(nullptr, 1000, false, MatchType::kHistoryUrl)));
}

TEST_F(KoanHistoryMatchesTest, EmptyUrlBarsTopSitesAreHistory) {
  FakeAutocompleteProviderClient client;
  scoped_refptr<AutocompleteProvider> provider =
      CreateZeroSuggestProvider(&client);
  AutocompleteMatch top_site(provider.get(), 1000, false,
                             MatchType::kNavsuggest);
  EXPECT_TRUE(IsSpaceHistoryMatch(top_site));
  // The copied link (the only deletable row) and the open tabs aren't.
  AutocompleteMatch copied(provider.get(), 1600, /*deletable=*/true,
                           MatchType::kNavsuggest);
  EXPECT_FALSE(IsSpaceHistoryMatch(copied));
  AutocompleteMatch tab(provider.get(), 1500, false, MatchType::kOpenTab);
  EXPECT_FALSE(IsSpaceHistoryMatch(tab));
}

TEST_F(KoanHistoryMatchesTest, PageKeyIsTheOmniboxsDedupeKey) {
  EXPECT_EQ(HistoryPageKey(GURL("https://www.example.com/a"), nullptr),
            HistoryPageKey(GURL("http://example.com/a"), nullptr));
  EXPECT_NE(HistoryPageKey(GURL("https://example.com/a"), nullptr),
            HistoryPageKey(GURL("https://example.com/b"), nullptr));
}

TEST_F(KoanHistoryMatchesTest, TheTypedAddressIsKept) {
  AutocompleteMatch match = Match(Type::kHistoryUrl, MatchType::kHistoryUrl);
  match.destination_url = GURL("https://www.example.com/page");
  const AutocompleteInput typed(u"example.com/page",
                                metrics::OmniboxEventProto::OTHER,
                                TestSchemeClassifier());
  EXPECT_TRUE(IsTypedAddress(match, typed, nullptr));
  // A longer page the history completes it to is not the typed address.
  const AutocompleteInput prefix(u"example.com/pa",
                                 metrics::OmniboxEventProto::OTHER,
                                 TestSchemeClassifier());
  EXPECT_FALSE(IsTypedAddress(match, prefix, nullptr));
  // Words aren't an address.
  const AutocompleteInput words(u"example page",
                                metrics::OmniboxEventProto::OTHER,
                                TestSchemeClassifier());
  EXPECT_FALSE(IsTypedAddress(match, words, nullptr));
}

}  // namespace
}  // namespace koan
