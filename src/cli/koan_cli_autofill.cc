// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_autofill.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace koan::cli {

namespace {

std::string Str(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

}  // namespace

base::DictValue AutofillParams(int node, bool list, std::string_view pick) {
  base::DictValue params;
  params.Set("backendNodeId", node);
  if (list) {
    params.Set("list", true);
    return params;
  }
  int index = 0;
  const bool digits =
      !pick.empty() && std::ranges::all_of(pick, base::IsAsciiDigit<char>);
  if (digits && base::StringToInt(pick, &index) && index > 0) {
    params.Set("pick", index);
  } else {
    params.Set("pickLabel", pick);
  }
  return params;
}

std::string SuggestionLines(const base::DictValue& result) {
  std::string out;
  const base::ListValue* rows = result.FindList("suggestions");
  if (!rows) {
    return out;
  }
  for (const base::Value& row : *rows) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    base::StrAppend(
        &out, {base::NumberToString(dict->FindInt("index").value_or(0)), ". ",
               Str(*dict, "label"), "  [", Str(*dict, "kind"), "]\n"});
  }
  return out;
}

AutofillOutcome ReadAutofillPick(const base::DictValue& result,
                                 std::string_view tab) {
  AutofillOutcome outcome;
  const std::string kind = Str(result, "kind");
  const std::string label = Str(result, "label");
  if (Str(result, "userNeeded") == "autofill-verify") {
    outcome.user_needed = true;
    outcome.line = base::StrCat(
        {"Koan.userNeeded: Chrome asks the user to verify this ",
         kind == "password" ? "login" : "card", " (\"", label,
         "\"); the user is asked in Koan. Wait for Koan.userNeededDone (koan "
         "watch ",
         tab,
         "), then take a snapshot: the field then reads \"•••• "
         "(filled)\""});
    return outcome;
  }
  outcome.filled = result.FindBool("filled").value_or(false);
  outcome.line = outcome.filled
                     ? base::StrCat({"ok: filled ", kind, " \"", label, "\""})
                     : "Koan.notFilled: Chrome did not fill the form";
  return outcome;
}

}  // namespace koan::cli
