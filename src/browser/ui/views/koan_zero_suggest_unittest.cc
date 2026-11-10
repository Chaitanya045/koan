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
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/clipboard/test/test_clipboard.h"
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
    // The provider reads the clipboard: a fake one, empty unless a test copies.
    ui::TestClipboard::CreateForCurrentThread();
    provider_ = CreateZeroSuggestProvider(&client_);
  }

  void TearDown() override {
    provider_.reset();
    ui::Clipboard::DestroyClipboardForCurrentThread();
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

  void Copy(const std::u16string& text) {
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(text);
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

TEST_F(KoanZeroSuggestTest, NothingToListIsDoneAtOnce) {
  // An incognito window with one tab, a profile with no top sites: no rows,
  // and no request left running (the controller asks again only while one is
  // not done).
  EXPECT_TRUE(Run(Input()).empty());
  EXPECT_TRUE(provider_->done());
}

TEST_F(KoanZeroSuggestTest, LongDataUrlsAreCutInTheRowNotWhereTheyGo) {
  const std::string url =
      "data:text/html;charset=utf-8," + std::string(100000, 'a');
  AddTab(u"", url, 1);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(GURL(url), matches[0].destination_url);
  EXPECT_LT(matches[0].contents.size(), 100u);
  EXPECT_LT(matches[0].fill_into_edit.size(), 100u);
  EXPECT_TRUE(matches[0].description.empty());
}

TEST_F(KoanZeroSuggestTest, LongHttpUrlsAndTitlesAreCapped) {
  const std::string url = "https://example.com/" + std::string(100000, 'p');
  AddTab(std::u16string(5000, u't'), url, 1);
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(1u, matches.size());
  EXPECT_EQ(GURL(url), matches[0].destination_url);
  EXPECT_LE(matches[0].contents.size(), 32768u);
  EXPECT_LE(matches[0].fill_into_edit.size(), 32768u);
  EXPECT_LE(matches[0].description.size(), 1024u);
  EXPECT_FALSE(matches[0].description.empty());
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

TEST_F(KoanZeroSuggestTest, ACopiedLinkComesFirstTwiceThenNotAgain) {
  AddTab(u"Tab", "https://tab.example/", 1);
  Copy(u"https://copied.example/page?q=1");
  for (int i = 0; i < 2; ++i) {
    const ACMatches& matches = Run(Input());
    ASSERT_EQ(2u, matches.size()) << i;
    EXPECT_EQ(GURL("https://copied.example/page?q=1"),
              matches[0].destination_url);
    EXPECT_TRUE(matches[0].deletable);
    EXPECT_GT(matches[0].relevance, matches[1].relevance);
    EXPECT_TRUE(provider_->done());
  }
  // Firefox's CLIPBOARD_IMPRESSION_LIMIT.
  EXPECT_EQ(1u, Run(Input()).size());
  // Not while typing.
  Copy(u"https://other.example/");
  EXPECT_TRUE(Run(Input(u"abc", OEP::OTHER, /*zero_suggest=*/false)).empty());
}

TEST_F(KoanZeroSuggestTest, RemovingTheCopiedLinkForgetsIt) {
  Copy(u"https://removed.example/");
  const ACMatches& matches = Run(Input());
  ASSERT_EQ(1u, matches.size());
  provider_->DeleteMatch(matches[0]);
  EXPECT_TRUE(provider_->matches().empty());
  EXPECT_TRUE(Run(Input()).empty());
}

TEST(KoanClipboardSuggestionTest, OnlyAWholeHttpLink) {
  EXPECT_EQ(GURL("https://a.example/x"),
            ClipboardUrlForSuggestion(u"https://a.example/x"));
  EXPECT_EQ(GURL("http://a.example/"),
            ClipboardUrlForSuggestion(u"http://a.example"));
  for (const std::u16string& text :
       {std::u16string(), std::u16string(u"a.example"),
        std::u16string(u"ftp://a.example/"), std::u16string(u"javascript:x"),
        std::u16string(u"https://a.example/ b"),
        std::u16string(u"https://a.example/\n"),
        std::u16string(u" https://a.example/"), std::u16string(u"some text"),
        u"https://a.example/" + std::u16string(2048, u'a')}) {
    EXPECT_FALSE(ClipboardUrlForSuggestion(text)) << text;
  }
}

TEST(KoanClipboardSuggestionTest, TwicePerLinkUntilForgotten) {
  const GURL a("https://twice-a.example/");
  const GURL b("https://twice-b.example/");
  EXPECT_TRUE(TakeClipboardImpression(a));
  EXPECT_TRUE(TakeClipboardImpression(a));
  EXPECT_FALSE(TakeClipboardImpression(a));
  EXPECT_TRUE(TakeClipboardImpression(b));
  ForgetClipboardSuggestion(b);
  EXPECT_FALSE(TakeClipboardImpression(b));
  // A link copied again after another counts afresh, as in Firefox.
  EXPECT_TRUE(TakeClipboardImpression(a));
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
