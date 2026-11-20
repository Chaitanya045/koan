// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SECRET_FIELDS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SECRET_FIELDS_H_

namespace autofill {
class AutofillField;
}

namespace koan::agent {

// A field whose value is a secret (.agent/notes/agent-control.md sections
// 4.5 and 9.2): a password field, or one autofill classifies as a card
// number, CVC, card field or IBAN (from its heuristics, the server's
// predictions or the page's autocomplete="cc-*"). A focused one holds a pause
// (#408); a filled one locks script out of its frame, and every answer and
// event hides its value (`koan autofill`).
bool IsSecretField(const autofill::AutofillField& field);

// Of those, the ones the page's markup marks as secret (a password field, or
// autocomplete="cc-*"), which Koan reads in the page itself; autofill's copy
// of the form tells the others.
bool IsSecretByMarkup(const autofill::AutofillField& field);

// Of those, the ones a screenshot paints over once Koan filled them: a card
// number, a CVC or an IBAN (a password field shows dots).
bool IsMaskedField(const autofill::AutofillField& field);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SECRET_FIELDS_H_
