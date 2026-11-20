// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_autofill.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_batch.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

ParseResult Args(std::vector<std::string> args) {
  return ParseArgs(args);
}

TEST(KoanCliAutofillTest, ParsesTheCommand) {
  ParseResult list = Args({"autofill", "ab12", "@7"});
  ASSERT_TRUE(list.command) << list.error;
  EXPECT_EQ(list.command->kind, CommandKind::kAutofill);
  EXPECT_EQ(list.command->tab, "ab12");
  EXPECT_EQ(list.command->ref, "@7");
  EXPECT_TRUE(list.command->autofill_list);

  ParseResult pick = Args({"autofill", "ab12", "@7", "--pick", "2"});
  ASSERT_TRUE(pick.command) << pick.error;
  EXPECT_FALSE(pick.command->autofill_list);
  EXPECT_EQ(pick.command->pick, "2");

  ParseResult label =
      Args({"autofill", "ab12", "loc:role=textbox[name=\"Email\"]",
            "--pick=Visa 4242", "--json"});
  ASSERT_TRUE(label.command) << label.error;
  EXPECT_EQ(label.command->pick, "Visa 4242");
  EXPECT_TRUE(label.command->json);

  EXPECT_TRUE(Args({"autofill", "ab12", "@7", "--list"}).command);
  // Not both, a field, and the options are autofill's.
  EXPECT_FALSE(
      Args({"autofill", "ab12", "@7", "--list", "--pick", "1"}).command);
  EXPECT_FALSE(Args({"autofill", "ab12"}).command);
  EXPECT_FALSE(Args({"autofill", "ab12", "7"}).command);
  EXPECT_FALSE(Args({"autofill", "ab12", "@7", "--pick"}).command);
  EXPECT_FALSE(Args({"snapshot", "ab12", "--list"}).command);
  EXPECT_FALSE(Args({"act", "ab12", "click", "@3", "--pick", "1"}).command);
  EXPECT_TRUE(HelpText("autofill"));
  EXPECT_NE(UsageText().find("koan autofill"), std::string::npos);
}

TEST(KoanCliAutofillTest, AnActStep) {
  ParseResult act = Args({"act", "ab12", "autofill", "@7", "2", "autofill",
                          "@9", "Visa", "click", "@31"});
  ASSERT_TRUE(act.command) << act.error;
  ASSERT_EQ(act.command->steps.size(), 3u);
  EXPECT_EQ(act.command->steps[0].act, ActKind::kAutofill);
  EXPECT_EQ(act.command->steps[0].target, "@7");
  EXPECT_EQ(act.command->steps[0].text, "2");
  EXPECT_EQ(StepLabel(act.command->steps[1]), "autofill @9 \"Visa\"");
  // A step needs a field and a pick.
  EXPECT_FALSE(Args({"act", "ab12", "autofill", "@7"}).command);
  EXPECT_FALSE(Args({"act", "ab12", "autofill", "Visa", "2"}).command);
  std::vector<Step> steps;
  std::string error;
  EXPECT_TRUE(ParseStepLines("autofill @7 'Home'\nclick @9\n", &steps, &error))
      << error;
  EXPECT_EQ(steps[0].text, "Home");
}

TEST(KoanCliAutofillTest, TheBrowsersParams) {
  base::DictValue list = AutofillParams(7, /*list=*/true, "");
  EXPECT_EQ(list.FindInt("backendNodeId"), 7);
  EXPECT_EQ(list.FindBool("list"), true);
  EXPECT_FALSE(list.contains("pick"));
  base::DictValue number = AutofillParams(7, false, "2");
  EXPECT_EQ(number.FindInt("pick"), 2);
  EXPECT_FALSE(number.contains("pickLabel"));
  base::DictValue label = AutofillParams(7, false, "Visa 4242");
  EXPECT_EQ(*label.FindString("pickLabel"), "Visa 4242");
  // Not a number: "0", "-1", "2b" are labels (and the browser says none
  // matches).
  for (const char* text : {"0", "-1", "2b"}) {
    EXPECT_TRUE(AutofillParams(7, false, text).FindString("pickLabel")) << text;
  }
}

TEST(KoanCliAutofillTest, WhatItPrints) {
  const base::DictValue listed = *base::JSONReader::ReadDict(
      R"json({"suggestions": [
          {"index": 1, "label": "Home · 12 Main St", "kind": "address"},
          {"index": 2, "label": "ada@example.com (saved password)",
           "kind": "password"}]})json",
      base::JSON_PARSE_RFC);
  EXPECT_EQ(SuggestionLines(listed),
            "1. Home · 12 Main St  [address]\n"
            "2. ada@example.com (saved password)  [password]\n");
  EXPECT_EQ(SuggestionLines(base::DictValue()), "");

  const AutofillOutcome filled = ReadAutofillPick(
      *base::JSONReader::ReadDict(
          R"({"filled": true, "kind": "card", "label": "Visa ••••4242"})",
          base::JSON_PARSE_RFC),
      "ab12");
  EXPECT_TRUE(filled.filled);
  EXPECT_FALSE(filled.user_needed);
  EXPECT_EQ(filled.line, "ok: filled card \"Visa ••••4242\"");

  const AutofillOutcome verify =
      ReadAutofillPick(*base::JSONReader::ReadDict(
                           R"({"filled": false, "userNeeded": "autofill-verify",
              "kind": "card", "label": "Visa ••••4242"})",
                           base::JSON_PARSE_RFC),
                       "ab12");
  EXPECT_FALSE(verify.filled);
  EXPECT_TRUE(verify.user_needed);
  EXPECT_TRUE(verify.line.starts_with("Koan.userNeeded: "));
  EXPECT_NE(verify.line.find("verify this card"), std::string::npos);
  EXPECT_NE(verify.line.find("koan watch ab12"), std::string::npos);

  const AutofillOutcome login = ReadAutofillPick(
      *base::JSONReader::ReadDict(
          R"({"userNeeded": "autofill-verify", "kind": "password"})",
          base::JSON_PARSE_RFC),
      "ab12");
  EXPECT_NE(login.line.find("verify this login"), std::string::npos);
}

}  // namespace
}  // namespace koan::cli
