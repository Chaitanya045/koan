// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's tabs search (Shift+Cmd+A) before anything is typed: Chromium's @tabs
// keyword mode with an empty query lists the open tabs, through the hooks in
// OpenTabProvider (open_tab_provider.cc.patch) and SearchProvider
// (search_provider.cc.patch).

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/fake_autocomplete_provider_client.h"
#include "components/omnibox/browser/fake_tab_matcher.h"
#include "components/omnibox/browser/open_tab_provider.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "components/search_engines/search_engines_test_environment.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_starter_pack_data.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace koan {
namespace {

class KoanTabSearchTest : public testing::Test {
 protected:
  void SetUp() override {
    // OpenTabProvider's matches ask for localised strings, which needs a
    // ResourceBundle. This suite's launcher has none. With no locale pak
    // (this test ships none) the bundle answers with empty strings: enough
    // for the matches, but not for the starter pack's keywords, so the tests
    // add those engines themselves.
    if (!ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          "en-US", /*delegate=*/nullptr,
          ui::ResourceBundle::DO_NOT_LOAD_COMMON_RESOURCES);
      owns_resource_bundle_ = true;
    }
    client_.set_template_url_service(
        search_engines_test_environment_.template_url_service());
    AddStarterPackEngine(template_url_starter_pack_data::StarterPackId::kTabs,
                         u"@tabs", "chrome://tabs/?q={searchTerms}");
    AddStarterPackEngine(
        template_url_starter_pack_data::StarterPackId::kHistory, u"@history",
        "chrome://history/?q={searchTerms}");
    provider_ = base::MakeRefCounted<OpenTabProvider>(&client_);
  }

  void TearDown() override {
    provider_.reset();
    if (owns_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
    }
  }

  // A starter pack engine as upstream's en-US strings make it
  // (TemplateURLDataFromStarterPackEngine).
  void AddStarterPackEngine(template_url_starter_pack_data::StarterPackId id,
                            const std::u16string& keyword,
                            const std::string& url) {
    TemplateURLData data;
    data.SetShortName(keyword);
    data.SetKeyword(keyword);
    data.SetURL(url);
    data.starter_pack_id = static_cast<int>(id);
    data.safe_for_autoreplace = true;
    data.is_active = TemplateURLData::ActiveStatus::kTrue;
    client_.GetTemplateURLService()->Add(std::make_unique<TemplateURL>(data));
  }

  // `age` minutes since the tab was last shown.
  void AddTab(const std::string& url, int age) {
    static_cast<FakeTabMatcher&>(
        const_cast<TabMatcher&>(client_.GetTabMatcher()))
        .AddOpenTab(TabMatcher::TabWrapper(
            u"title", GURL(url), base::Time::Now() - base::Minutes(age)));
  }

  // The box as Shift+Cmd+A leaves it: the @tabs chip, then `query`.
  AutocompleteInput Input(const std::u16string& text,
                          bool keyword_mode = true) {
    AutocompleteInput input(text, metrics::OmniboxEventProto::OTHER,
                            TestSchemeClassifier());
    input.set_in_keyword_mode(keyword_mode);
    input.set_current_url(GURL("https://current.example/"));
    return input;
  }

  std::vector<std::string> Urls() {
    std::vector<std::string> urls;
    std::vector<AutocompleteMatch> matches = provider_->matches();
    std::ranges::stable_sort(
        matches, {}, [](const AutocompleteMatch& m) { return -m.relevance; });
    for (const AutocompleteMatch& match : matches) {
      urls.push_back(match.destination_url.spec());
    }
    return urls;
  }

  base::test::TaskEnvironment task_environment_;
  search_engines::SearchEnginesTestEnvironment search_engines_test_environment_;
  FakeAutocompleteProviderClient client_;
  scoped_refptr<OpenTabProvider> provider_;
  bool owns_resource_bundle_ = false;
};

TEST_F(KoanTabSearchTest, EmptyQueryListsTheOpenTabsMostRecentFirst) {
  AddTab("https://old.example/", 600);
  AddTab("https://recent.example/", 1);
  AddTab("https://current.example/", 0);
  AddTab("chrome://newtab/", 2);
  AddTab("https://middle.example/", 20);

  EXPECT_TRUE(
      IsEmptyTabSearch(Input(u"@tabs "), client_.GetTemplateURLService()));
  provider_->Start(Input(u"@tabs "), /*minimal_changes=*/false);
  // Not the page searched from, nor a new tab page; no "No results found".
  EXPECT_EQ(Urls(), (std::vector<std::string>{"https://recent.example/",
                                              "https://middle.example/",
                                              "https://old.example/"}));
  for (const AutocompleteMatch& match : provider_->matches()) {
    EXPECT_EQ(match.type, omnibox::AutocompleteMatchType::kOpenTab);
    // Picking one keeps the keyword mode (upstream's tab match).
    EXPECT_EQ(match.keyword, u"@tabs");
  }
}

TEST_F(KoanTabSearchTest, TypedQueryStillFilters) {
  AddTab("https://recent.example/", 1);
  AddTab("https://middle.example/", 20);
  EXPECT_FALSE(
      IsEmptyTabSearch(Input(u"@tabs mid"), client_.GetTemplateURLService()));
  provider_->Start(Input(u"@tabs mid"), /*minimal_changes=*/false);
  EXPECT_EQ(Urls(), std::vector<std::string>{"https://middle.example/"});
}

TEST_F(KoanTabSearchTest, OnlyTheTabsKeywordInKeywordMode) {
  const TemplateURLService* service = client_.GetTemplateURLService();
  EXPECT_FALSE(IsEmptyTabSearch(Input(u"@history "), service));
  EXPECT_FALSE(
      IsEmptyTabSearch(Input(u"@tabs ", /*keyword_mode=*/false), service));
  EXPECT_FALSE(IsEmptyTabSearch(Input(u"@tabs "), nullptr));
}

TEST_F(KoanTabSearchTest, RelevanceFallsWithAgeAndStaysPositive) {
  const AutocompleteInput input = Input(u"@tabs ");
  const GURL url("https://a.example/");
  const base::Time now = base::Time::Now();
  const int fresh = EmptyTabSearchRelevance(input, url, now);
  const int minute =
      EmptyTabSearchRelevance(input, url, now - base::Minutes(1));
  const int day = EmptyTabSearchRelevance(input, url, now - base::Days(1));
  EXPECT_GT(fresh, minute);
  EXPECT_GT(minute, day);
  EXPECT_GT(day, 0);
  // Within upstream's typed range (OpenTabProvider scores up to 10000), and a
  // clock change can't push it past the freshest.
  EXPECT_LE(EmptyTabSearchRelevance(input, url, now + base::Hours(1)), fresh);
  EXPECT_EQ(EmptyTabSearchRelevance(input, GURL("about:blank"), now), 0);
}

}  // namespace
}  // namespace koan
