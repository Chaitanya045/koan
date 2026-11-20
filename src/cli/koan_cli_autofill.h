// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_AUTOFILL_H_
#define KOAN_CLI_KOAN_CLI_AUTOFILL_H_

#include <string>
#include <string_view>

#include "base/values.h"

// `koan autofill` and act's `autofill` step (.agent/notes/agent-control.md
// section 7.0, "Autofill for agents"): what is sent to the browser's
// Koan.autofill and what is printed of its answer. Pure: no I/O.
namespace koan::cli {

// Koan.autofill's params: the field, and --list, or --pick as a suggestion's
// number (all digits) or its label.
base::DictValue AutofillParams(int node, bool list, std::string_view pick);

// --list's answer as numbered lines ("1. Home · 12 Main St, …"), one per
// suggestion, each with its kind; "" if it has none.
std::string SuggestionLines(const base::DictValue& result);

// A pick's answer: filled, or waiting for the user to verify it.
struct AutofillOutcome {
  bool filled = false;
  // Chrome asks the user first (Koan.userNeeded autofill-verify).
  bool user_needed = false;
  // "ok: filled card \"Visa ••••4242 · exp 08/29\"", or the line that says
  // to wait for the user.
  std::string line;
};
AutofillOutcome ReadAutofillPick(const base::DictValue& result,
                                 std::string_view tab);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_AUTOFILL_H_
