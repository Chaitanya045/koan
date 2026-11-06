// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_zero_suggest.h"

#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/fake_autocomplete_provider_client.h"
#include "components/omnibox/browser/fake_tab_matcher.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/metrics_proto/omnibox_focus_type.pb.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace koan {
namespace {

using OEP = metrics::OmniboxEventProto;

// The fake client with the tabs of the test's choosing, and no top sites.
class TabsClient : public FakeAutocompleteProviderClient {
 public:
  const TabMatcher& GetTabMatcher() const override { return tabs_; }
  FakeTabMatcher& tabs() { return tabs_; }

 private:
  FakeTabMatcher tabs_;
};

class KoanZeroSuggestTest : public testing::Test {
 protected:
  void SetUp() override {
    // TabSwitchAction asks for localised strings (the Switch to Tab labels),
    // which needs a ResourceBundle. This suite's launcher has none. With no
    // locale pak (this test ships none) the bundle answers with empty
    // strings, which is all these tests need.
    if (!ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          "en-US", /*delegate=*/nullptr,
          ui::ResourceBundle::DO_NOT_LOAD_COMMON_RESOURCES);
      owns_resource_bundle_ = true;
    }
    provider_ = CreateZeroSuggestProvider(&client_);
  }

  void TearDown() override {
    provider_.reset();
    if (owns_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
    }
  }

  // `age` minutes since the tab was last shown.
  void AddTab(const std::u16string& title, const std::string& url, int age) {
    client_.tabs().AddOpenTab(TabMatcher::TabWrapper(
        title, GURL(url), base::Time::Now() - base::Minutes(age)));
  }

  // A box that was just opened (empty: Ctrl+T) or has the page's address
  // (Ctrl+L): zero-suggest. Or one the user typed in (`zero_suggest` false).
  AutocompleteInput Input(const std::u16string& text = std::u16string(),
                          OEP::PageClassification classification = OEP::OTHER,
                          bool zero_suggest = true) {
    AutocompleteInput input(text, classification, scheme_classifier_);
    input.set_current_url(GURL("https://current.example/"));
    input.set_focus_type(zero_suggest
                             ? metrics::OmniboxFocusType::INTERACTION_FOCUS
                             : metrics::OmniboxFocusType::INTERACTION_DEFAULT);
    return input;
  }

  const ACMatches& Run(const AutocompleteInput& input) {
    provider_->Start(input, /*minimal_changes=*/false);
    return provider_->matches();
  }

  base::test::TaskEnvironment task_environment_;
  TestSchemeClassifier scheme_classifier_;
  TabsClient client_;
  scoped_refptr<AutocompleteProvider> provider_;
  bool owns_resource_bundle_ = false;
};

TEST_F(KoanZeroSuggestTest, ListsOpenTabsMostRecentlyUsedFirst) {
  AddTab(u"Old", "https://old.example/", 30);
  AddTab(u"New", "https://new.example/", 1);
  AddTab(u"Middle", "https://middle.example/", 10);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(3u, matches.size());
  EXPECT_EQ(u"New", matches[0].description);
  EXPECT_EQ(u"Middle", matches[1].description);
  EXPECT_EQ(u"Old", matches[2].description);
  for (const AutocompleteMatch& match : matches) {
    EXPECT_EQ(omnibox::AutocompleteMatchType::kOpenTab, match.type);
    EXPECT_TRUE(match.has_tab_match.value_or(false));
    EXPECT_FALSE(match.actions.empty());  // The Switch to Tab button.
    EXPECT_TRUE(IsLocalZeroSuggestMatch(match));
  }
  // Most recent first also in relevance, the order the results keep.
  EXPECT_GT(matches[0].relevance, matches[1].relevance);
  EXPECT_GT(matches[1].relevance, matches[2].relevance);
}

TEST_F(KoanZeroSuggestTest, AtMostFiveTabs) {
  for (int i = 0; i < 8; ++i) {
    AddTab(u"Tab", "https://site" + std::to_string(i) + ".example/", i);
  }
  EXPECT_EQ(5u, Run(Input()).size());
}

TEST_F(KoanZeroSuggestTest, AnyPageButBlankAndNewTabPagesAndEachOnce) {
  AddTab(u"A", "https://a.example/", 1);
  AddTab(u"A again", "https://a.example/", 2);
  AddTab(u"Settings", "chrome://settings/", 3);
  AddTab(u"Data page", "data:text/html,<title>x</title>y", 4);
  AddTab(u"File", "file:///tmp/a.html", 5);
  AddTab(u"Blank", "about:blank", 6);
  AddTab(u"New Tab", "chrome://newtab/", 7);
  AddTab(u"Koan new tab", "chrome://koan-newtab/", 8);
  AddTab(u"Chromium new tab", "chrome://new-tab-page/", 9);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(4u, matches.size());
  EXPECT_EQ(u"A", matches[0].description);
  EXPECT_EQ(u"Settings", matches[1].description);
  EXPECT_EQ(u"Data page", matches[2].description);
  EXPECT_EQ(u"File", matches[3].description);
}

TEST_F(KoanZeroSuggestTest, NotTheTabBeingLeftNorACopyOfIt) {
  // Navigate()'s switch would find the active tab first and do nothing.
  AddTab(u"Copy of the current page", "https://current.example/#frag", 1);
  AddTab(u"Other", "https://other.example/", 2);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(u"Other", matches[0].description);
}

TEST_F(KoanZeroSuggestTest, TitleAndContentsAreSanitized) {
  // A title can end in a non-ASCII space; a URL ending in %20 formats with a
  // trailing space: AutocompleteResult::AppendMatches() DCHECKs both (#166).
  AddTab(u"Title　", "https://example.com/a%20", 1);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(AutocompleteMatch::SanitizeString(matches[0].description),
            matches[0].description);
  EXPECT_EQ(AutocompleteMatch::SanitizeString(matches[0].contents),
            matches[0].contents);
  EXPECT_EQ(u"Title", matches[0].description);
}

TEST_F(KoanZeroSuggestTest, EmptyTitleHasNoClassification) {
  // ValidateClassifications() rejects a classification of an empty text.
  AddTab(u"", "https://untitled.example/", 1);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(1u, matches.size());
  EXPECT_TRUE(matches[0].description.empty());
  EXPECT_TRUE(matches[0].description_class.empty());
  EXPECT_FALSE(matches[0].contents_class.empty());
}

TEST_F(KoanZeroSuggestTest, OnlyForABoxJustOpened) {
  AddTab(u"Tab", "https://tab.example/", 1);
  // The address selected (Ctrl+L) lists too; typed text does not.
  EXPECT_EQ(1u, Run(Input(u"current.example")).size());
  EXPECT_TRUE(Run(Input(u"tab", OEP::OTHER, /*zero_suggest=*/false)).empty());
  // Not the NTP's realbox or a composebox.
  EXPECT_TRUE(Run(Input(u"", OEP::NTP_REALBOX)).empty());
  EXPECT_TRUE(Run(Input(u"", OEP::NTP_COMPOSEBOX)).empty());
  // Every omnibox: also about:blank, the home page, an invalid address.
  EXPECT_EQ(1u, Run(Input(u"", OEP::BLANK)).size());
  EXPECT_EQ(1u, Run(Input(u"", OEP::HOME_PAGE)).size());
  EXPECT_EQ(1u, Run(Input(u"", OEP::INVALID_SPEC)).size());
  EXPECT_EQ(1u, Run(Input(u"", OEP::NTP)).size());
}

TEST_F(KoanZeroSuggestTest, OnlyTheProvidersOwnMatchesAreLocal) {
  AutocompleteMatch foreign(nullptr, 1000, false,
                            omnibox::AutocompleteMatchType::kNavsuggest);
  EXPECT_FALSE(IsLocalZeroSuggestMatch(foreign));
  AddTab(u"Tab", "https://tab.example/", 1);
  ASSERT_EQ(1u, Run(Input()).size());
  EXPECT_TRUE(IsLocalZeroSuggestMatch(provider_->matches()[0]));
}

TEST(KoanZeroSuggestSectionTest, EveryOmniboxButTheWebUISearchboxes) {
  for (OEP::PageClassification c :
       {OEP::OTHER, OEP::BLANK, OEP::HOME_PAGE, OEP::INVALID_SPEC, OEP::NTP,
        OEP::INSTANT_NTP_WITH_OMNIBOX_AS_STARTING_FOCUS,
        OEP::SEARCH_RESULT_PAGE_NO_SEARCH_TERM_REPLACEMENT}) {
    EXPECT_TRUE(UsesLocalZeroSuggestSection(c)) << c;
  }
  for (OEP::PageClassification c :
       {OEP::NTP_REALBOX, OEP::OMNIBOX_EVERYWHERE, OEP::NTP_COMPOSEBOX,
        OEP::LENS_SIDE_PANEL_SEARCHBOX}) {
    EXPECT_FALSE(UsesLocalZeroSuggestSection(c)) << c;
  }
}

}  // namespace
}  // namespace koan
