// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_LOGIC_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_LOGIC_H_

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

// `koan autofill` (.agent/notes/agent-control.md section 7.0, "Autofill for
// agents"): what an agent is shown of the suggestions Chrome would offer for
// a field, and which one it picked. Pure logic; the browser side
// (koan_agent_autofill.h) turns autofill::Suggestion into a SuggestionRow and
// runs the pick through Chrome's own fill.
namespace koan::agent {

// What a row fills, as the agent sees it, and which "What agents can use"
// switch it needs.
enum class AutofillKind {
  kAddress,
  kCard,
  kIban,
  kPassword,
  // A value typed in this field before (autocomplete), or the page's
  // <datalist>.
  kPrevious,
};

// "address", "card", "iban", "password", "previous".
std::string_view AutofillKindName(AutofillKind kind);
// Whether a row of that kind fills a secret: a password, a card number (and
// its CVC) or an IBAN.
bool IsSecretKind(AutofillKind kind);

// What a dropdown row shows: autofill::Suggestion's texts.
struct SuggestionRow {
  AutofillKind kind = AutofillKind::kAddress;
  std::u16string main_text;
  std::vector<std::u16string> minor_texts;
  // Suggestion::labels, row by row.
  std::vector<std::vector<std::u16string>> labels;
  // Passwords: the site the login is saved for, when it is not this one
  // exactly.
  std::u16string additional_label;
};

// The most a label is long, in characters; longer ones end in "…".
inline constexpr size_t kMaxLabelLength = 80;

// The row as the agent sees it, the way the dropdown shows it, never with a
// secret:
//   address   "Home · 12 Main St, Springfield"
//   card      "Visa ••••4242 · exp 08/29"
//   password  "ada@example.com (saved password)",
//             "ada@example.com · accounts.example.com (saved password)"
//   previous  "ada@example.com"
// A password row's own label (the password as dots, which says its length)
// is never shown. In every label any run of 7 or more digits (spaces, dots
// and dashes between them allowed: a card number, an account number) is cut
// to "••••" and its last 4 digits, and any of `secrets` (values Koan knows
// must not show: the passwords and numbers of the rows) is replaced by
// "••••".
std::string LabelFor(const SuggestionRow& row,
                     base::span<const std::u16string> secrets);

// --pick N (1-based) or --pick "label".
struct AutofillPick {
  std::optional<int> index;
  std::string label;
};

struct PickMatch {
  // 0-based, into `labels`.
  std::optional<size_t> index;
  // Koan.noSuchSuggestion or Koan.ambiguousSuggestion, and the message,
  // when there is no index.
  std::string error_code;
  std::string error_message;
};

// The row `pick` names among `labels` (LabelFor()'s): its number; or the
// label exactly; or the only label that contains it, ignoring case.
PickMatch MatchPick(const std::vector<std::string>& labels,
                    const AutofillPick& pick);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_LOGIC_H_
