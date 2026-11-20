// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_secret_fields.h"

#include <string_view>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/field_types.h"

namespace koan::agent {

namespace {

bool HasCardAutocomplete(const autofill::AutofillField& field) {
  for (std::string_view token : base::SplitStringPiece(
           field.autocomplete_attribute(), " \t\n", base::TRIM_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    if (base::StartsWith(token, "cc-", base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool IsSecretField(const autofill::AutofillField& field) {
  if (field.form_control_type() == autofill::FormControlType::kInputPassword) {
    return true;
  }
  const autofill::FieldTypeGroupSet groups = field.Type().GetGroups();
  return groups.contains(autofill::FieldTypeGroup::kCreditCard) ||
         groups.contains(autofill::FieldTypeGroup::kStandaloneCvcField) ||
         groups.contains(autofill::FieldTypeGroup::kIban) ||
         HasCardAutocomplete(field);
}

bool IsSecretByMarkup(const autofill::AutofillField& field) {
  return field.form_control_type() ==
             autofill::FormControlType::kInputPassword ||
         HasCardAutocomplete(field);
}

bool IsMaskedField(const autofill::AutofillField& field) {
  if (field.form_control_type() == autofill::FormControlType::kInputPassword) {
    return false;
  }
  const autofill::FieldTypeSet types = field.Type().GetTypes();
  return types.contains(autofill::CREDIT_CARD_NUMBER) ||
         types.contains(autofill::CREDIT_CARD_VERIFICATION_CODE) ||
         types.contains(autofill::CREDIT_CARD_STANDALONE_VERIFICATION_CODE) ||
         types.contains(autofill::IBAN_VALUE) ||
         field.Type().GetGroups().contains(
             autofill::FieldTypeGroup::kStandaloneCvcField);
}

}  // namespace koan::agent
