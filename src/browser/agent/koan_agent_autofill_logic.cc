// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_autofill_logic.h"

#include "base/i18n/case_conversion.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"

namespace koan::agent {

namespace {

constexpr char16_t kDots[] = u"••••";
constexpr char16_t kSeparator[] = u" · ";

// A run of 7 or more digits, with single spaces, dots or dashes between
// them, becomes "••••" and its last 4 digits.
std::u16string MaskDigitRuns(std::u16string_view text) {
  std::u16string out;
  size_t i = 0;
  while (i < text.size()) {
    if (!base::IsAsciiDigit(text[i])) {
      out.push_back(text[i++]);
      continue;
    }
    size_t end = i;
    std::u16string digits;
    while (end < text.size()) {
      if (base::IsAsciiDigit(text[end])) {
        digits.push_back(text[end++]);
      } else if (end + 1 < text.size() &&
                 (text[end] == ' ' || text[end] == '-' || text[end] == '.' ||
                  text[end] == u' ') &&
                 base::IsAsciiDigit(text[end + 1])) {
        ++end;
      } else {
        break;
      }
    }
    if (digits.size() >= 7) {
      out += kDots;
      out += digits.substr(digits.size() - 4);
    } else {
      out.append(text.substr(i, end - i));
    }
    i = end;
  }
  return out;
}

std::u16string Join(const std::vector<std::u16string>& parts,
                    std::u16string_view separator) {
  std::u16string out;
  for (const std::u16string& part : parts) {
    const std::u16string trimmed(base::TrimWhitespace(part, base::TRIM_ALL));
    if (trimmed.empty()) {
      continue;
    }
    if (!out.empty()) {
      out += separator;
    }
    out += trimmed;
  }
  return out;
}

std::u16string Truncated(std::u16string text) {
  if (text.size() <= kMaxLabelLength) {
    return text;
  }
  size_t cut = kMaxLabelLength - 1;
  // Never half a surrogate pair.
  if (cut > 0 && CBU16_IS_LEAD(text[cut - 1])) {
    --cut;
  }
  text.resize(cut);
  text += u"…";
  return text;
}

}  // namespace

std::string_view AutofillKindName(AutofillKind kind) {
  switch (kind) {
    case AutofillKind::kAddress:
      return "address";
    case AutofillKind::kCard:
      return "card";
    case AutofillKind::kIban:
      return "iban";
    case AutofillKind::kPassword:
      return "password";
    case AutofillKind::kPrevious:
      return "previous";
  }
  return "";
}

bool IsSecretKind(AutofillKind kind) {
  return kind == AutofillKind::kCard || kind == AutofillKind::kIban ||
         kind == AutofillKind::kPassword;
}

std::string LabelFor(const SuggestionRow& row,
                     base::span<const std::u16string> secrets) {
  std::vector<std::u16string> label_parts;
  for (const std::vector<std::u16string>& line : row.labels) {
    for (const std::u16string& text : line) {
      label_parts.push_back(text);
    }
  }
  std::u16string label;
  switch (row.kind) {
    case AutofillKind::kAddress:
    case AutofillKind::kCard:
    case AutofillKind::kIban: {
      std::vector<std::u16string> first = {row.main_text};
      first.insert(first.end(), row.minor_texts.begin(), row.minor_texts.end());
      std::vector<std::u16string> parts = {Join(first, u" ")};
      parts.push_back(Join(label_parts, row.kind == AutofillKind::kAddress
                                            ? u", "
                                            : kSeparator));
      label = Join(parts, kSeparator);
      break;
    }
    case AutofillKind::kPassword:
      // The row's own label is the password as dots: never shown.
      label = Join({row.main_text, row.additional_label}, kSeparator) +
              u" (saved password)";
      break;
    case AutofillKind::kPrevious:
      label = Join({row.main_text}, u"");
      break;
  }
  if (row.kind != AutofillKind::kAddress) {
    label = MaskDigitRuns(label);
  }
  for (const std::u16string& secret : secrets) {
    if (!secret.empty()) {
      base::ReplaceSubstringsAfterOffset(&label, 0, secret, kDots);
    }
  }
  return base::UTF16ToUTF8(Truncated(std::move(label)));
}

PickMatch MatchPick(const std::vector<std::string>& labels,
                    const AutofillPick& pick) {
  PickMatch match;
  if (pick.index) {
    if (*pick.index >= 1 && static_cast<size_t>(*pick.index) <= labels.size()) {
      match.index = static_cast<size_t>(*pick.index - 1);
      return match;
    }
    match.error_code = "Koan.noSuchSuggestion";
    match.error_message = base::StrCat(
        {"there is no suggestion ", base::NumberToString(*pick.index),
         "; the field has ", base::NumberToString(labels.size()),
         " (koan autofill <tab> @field --list)"});
    return match;
  }
  for (size_t i = 0; i < labels.size(); ++i) {
    if (labels[i] == pick.label) {
      match.index = i;
      return match;
    }
  }
  const std::u16string wanted =
      base::i18n::ToLower(base::UTF8ToUTF16(pick.label));
  std::vector<size_t> found;
  for (size_t i = 0; i < labels.size(); ++i) {
    if (!wanted.empty() &&
        base::i18n::ToLower(base::UTF8ToUTF16(labels[i])).find(wanted) !=
            std::u16string::npos) {
      found.push_back(i);
    }
  }
  if (found.size() == 1) {
    match.index = found[0];
    return match;
  }
  if (found.empty()) {
    match.error_code = "Koan.noSuchSuggestion";
    match.error_message = "no suggestion for this field matches \"" +
                          pick.label + "\" (koan autofill <tab> @field --list)";
    return match;
  }
  match.error_code = "Koan.ambiguousSuggestion";
  match.error_message = "\"" + pick.label + "\" matches " +
                        base::NumberToString(found.size()) +
                        " suggestions; pick one by its number:";
  for (size_t i : found) {
    match.error_message +=
        " " + base::NumberToString(i + 1) + ". " + labels[i] + ";";
  }
  match.error_message.pop_back();
  return match;
}

}  // namespace koan::agent
