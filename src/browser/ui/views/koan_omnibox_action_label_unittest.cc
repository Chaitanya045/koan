// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// An open tab's suggestion says Zen's "Switch to Tab". Koan relabels the
// button Chromium shows for the match's tab switch action, so this also
// checks what that rests on upstream: a typed address that is open in a tab
// gets has_tab_match and a TabSwitchAction, whose button the popup shows
// (docs/REBASE.md, "Look: an open tab's Switch to Tab").

#include "koan/browser/ui/views/koan_omnibox_action_label.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "components/omnibox/browser/actions/omnibox_action.h"
#include "components/omnibox/browser/actions/omnibox_action_concepts.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/fake_autocomplete_provider_client.h"
#include "components/omnibox/browser/fake_tab_matcher.h"
#include "components/omnibox/browser/omnibox_popup_selection.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace koan {
namespace {

// The fake client with the open tabs of the test's choosing.
class TabsClient : public FakeAutocompleteProviderClient {
 public:
  const TabMatcher& GetTabMatcher() const override { return tabs_; }
  FakeTabMatcher& tabs() { return tabs_; }

 private:
  FakeTabMatcher tabs_;
};

class KoanOmniboxActionLabelTest : public testing::Test {
 protected:
  void SetUp() override {
    // TabSwitchAction asks for Chromium's localised labels, which needs a
    // ResourceBundle; with no locale pak they are empty, which is all this
    // test needs (Koan's label is its own).
    if (!ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          "en-US", /*delegate=*/nullptr,
          ui::ResourceBundle::DO_NOT_LOAD_COMMON_RESOURCES);
      owns_resource_bundle_ = true;
    }
  }

  void TearDown() override {
    if (owns_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
    }
  }

 private:
  // The fake client's services post tasks.
  base::test::TaskEnvironment task_environment_;
  bool owns_resource_bundle_ = false;
};

TEST_F(KoanOmniboxActionLabelTest, OpenTabsMatchSaysSwitchToTab) {
  TabsClient client;
  // Every tab whose URL has "open-site" is open (in any window or Space:
  // TabMatcherDesktop looks at every tab strip of the profile).
  client.tabs().set_url_substring_match("open-site");

  ACMatches matches;
  AutocompleteMatch match;
  match.destination_url = GURL("https://open-site.example/");
  matches.push_back(match);
  match.destination_url = GURL("https://closed.example/");
  matches.push_back(match);
  AutocompleteResult result;
  result.AppendMatches(matches);

  // A typed address, not the empty bar.
  TestSchemeClassifier classifier;
  AutocompleteInput input(u"open-site.example",
                          metrics::OmniboxEventProto::OTHER, classifier);
  result.ConvertOpenTabMatches(&client, &input);

  const AutocompleteMatch& open = *result.match_at(0);
  EXPECT_TRUE(open.has_tab_match.value_or(false));
  ASSERT_EQ(open.actions.size(), 1u);
  const OmniboxAction& action = *open.actions[0];
  EXPECT_EQ(action.ActionId(), OmniboxActionId::TAB_SWITCH);
  EXPECT_EQ(OmniboxActionLabel(action),
            std::optional<std::u16string_view>(u"Switch to Tab"));
  // Chromium shows the action's button beside the row, where Koan's label
  // goes.
  EXPECT_TRUE(OmniboxPopupSelection(
                  0, OmniboxPopupSelection::LineState::kFocusedButtonAction, 0)
                  .IsControlPresentOnMatch(result));

  const AutocompleteMatch& closed = *result.match_at(1);
  EXPECT_FALSE(closed.has_tab_match.value_or(false));
  EXPECT_TRUE(closed.actions.empty());
  EXPECT_FALSE(OmniboxPopupSelection(
                   1, OmniboxPopupSelection::LineState::kFocusedButtonAction, 0)
                   .IsControlPresentOnMatch(result));
}

TEST_F(KoanOmniboxActionLabelTest, OtherActionsKeepChromiumsLabel) {
  auto action = base::MakeRefCounted<OmniboxAction>(
      OmniboxAction::LabelStrings(u"Hint", u"Contents", u"Suffix", u"Hint"),
      GURL("https://example.com/"));
  ASSERT_NE(action->ActionId(), OmniboxActionId::TAB_SWITCH);
  EXPECT_EQ(OmniboxActionLabel(*action), std::nullopt);
}

}  // namespace
}  // namespace koan
