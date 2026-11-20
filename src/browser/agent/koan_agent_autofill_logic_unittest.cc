// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_autofill_logic.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

SuggestionRow Row(AutofillKind kind,
                  std::u16string main,
                  std::vector<std::u16string> minor,
                  std::vector<std::vector<std::u16string>> labels,
                  std::u16string additional = u"") {
  SuggestionRow row;
  row.kind = kind;
  row.main_text = std::move(main);
  row.minor_texts = std::move(minor);
  row.labels = std::move(labels);
  row.additional_label = std::move(additional);
  return row;
}

TEST(KoanAgentAutofillLogicTest, LabelsAsTheDropdownShowsThem) {
  EXPECT_EQ(LabelFor(Row(AutofillKind::kAddress, u"Home", {},
                         {{u"12 Main St", u"Springfield"}}),
                     {}),
            "Home \xC2\xB7 12 Main St, Springfield");
  EXPECT_EQ(LabelFor(Row(AutofillKind::kCard, u"Visa", {u"••••4242"},
                         {{u"exp 08/29"}}),
                     {}),
            "Visa \xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"
            "4242 \xC2\xB7 exp 08/29");
  EXPECT_EQ(
      LabelFor(Row(AutofillKind::kPrevious, u"ada@example.com", {}, {}), {}),
      "ada@example.com");
}

TEST(KoanAgentAutofillLogicTest, APasswordRowNeverShowsItsPassword) {
  // Chrome's row: the username, and the password as dots (its length).
  const SuggestionRow row =
      Row(AutofillKind::kPassword, u"ada@example.com", {}, {{u"•••••••••"}});
  const std::string label = LabelFor(row, {});
  EXPECT_EQ(label, "ada@example.com (saved password)");
  EXPECT_EQ(label.find("\xE2\x80\xA2"), std::string::npos);
  // A login saved for a related site says which.
  EXPECT_EQ(LabelFor(Row(AutofillKind::kPassword, u"ada", {}, {},
                         u"accounts.example.com"),
                     {}),
            "ada \xC2\xB7 accounts.example.com (saved password)");
}

TEST(KoanAgentAutofillLogicTest, LabelsNeverCarryASecret) {
  // A full card number anywhere in a card row is cut to its last four.
  const std::string label =
      LabelFor(Row(AutofillKind::kCard, u"My card 4111 1111 1111 1111",
                   {u"4111-1111-1111-1111"}, {{u"exp 01/30"}}),
               {});
  EXPECT_EQ(label.find("4111 1111"), std::string::npos) << label;
  EXPECT_EQ(label.find("41111111"), std::string::npos) << label;
  EXPECT_NE(label.find("1111"), std::string::npos);
  // An IBAN.
  const std::string iban = LabelFor(
      Row(AutofillKind::kIban, u"DE89 3704 0044 0532 0130 00", {}, {}), {});
  EXPECT_EQ(iban.find("3704 0044"), std::string::npos) << iban;
  // A value Koan knows must not show (a password a site put in a username
  // field's previous values, the number of the card) is replaced.
  const std::vector<std::u16string> secrets = {u"hunter2!"};
  EXPECT_EQ(
      LabelFor(Row(AutofillKind::kPrevious, u"hunter2!", {}, {}), secrets),
      "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
  // An address keeps its digits: the user sees them, and so may the agent.
  EXPECT_EQ(
      LabelFor(Row(AutofillKind::kAddress, u"Work", {}, {{u"+1 555 123 4567"}}),
               {}),
      "Work \xC2\xB7 +1 555 123 4567");
}

TEST(KoanAgentAutofillLogicTest, LongLabelsAreCut) {
  const std::u16string street(200, u'x');
  const std::string label =
      LabelFor(Row(AutofillKind::kAddress, u"Home", {}, {{street}}), {});
  // 79 UTF-16 characters ("Home · " and 72 x's: 80 bytes of UTF-8, as the
  // dot takes 2) and an ellipsis (3 bytes).
  EXPECT_EQ(label.size(), 80u + 3u);
  EXPECT_TRUE(label.ends_with("\xE2\x80\xA6"));
}

TEST(KoanAgentAutofillLogicTest, PicksByNumberOrLabel) {
  const std::vector<std::string> labels = {
      "Home \xC2\xB7 12 Main St", "Work \xC2\xB7 1 Office Park",
      "Visa \xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 4242"};
  EXPECT_EQ(MatchPick(labels, {.index = 2}).index, 1u);
  EXPECT_EQ(MatchPick(labels, {.label = labels[0]}).index, 0u);
  EXPECT_EQ(MatchPick(labels, {.label = "office"}).index, 1u);
  EXPECT_EQ(MatchPick(labels, {.label = "visa"}).index, 2u);

  PickMatch none = MatchPick(labels, {.index = 4});
  EXPECT_FALSE(none.index);
  EXPECT_EQ(none.error_code, "Koan.noSuchSuggestion");
  EXPECT_EQ(MatchPick(labels, {.index = 0}).error_code,
            "Koan.noSuchSuggestion");
  EXPECT_EQ(MatchPick(labels, {.label = "Mastercard"}).error_code,
            "Koan.noSuchSuggestion");
  PickMatch two = MatchPick(labels, {.label = "\xC2\xB7"});
  EXPECT_FALSE(two.index);
  EXPECT_EQ(two.error_code, "Koan.ambiguousSuggestion");
  EXPECT_NE(two.error_message.find("1. Home"), std::string::npos);
  EXPECT_NE(two.error_message.find("2. Work"), std::string::npos);
}

TEST(KoanAgentAutofillLogicTest, KindsAndSecrets) {
  EXPECT_EQ(AutofillKindName(AutofillKind::kCard), "card");
  EXPECT_EQ(AutofillKindName(AutofillKind::kPassword), "password");
  EXPECT_TRUE(IsSecretKind(AutofillKind::kCard));
  EXPECT_TRUE(IsSecretKind(AutofillKind::kIban));
  EXPECT_TRUE(IsSecretKind(AutofillKind::kPassword));
  EXPECT_FALSE(IsSecretKind(AutofillKind::kAddress));
  EXPECT_FALSE(IsSecretKind(AutofillKind::kPrevious));
}

}  // namespace
}  // namespace koan::agent
