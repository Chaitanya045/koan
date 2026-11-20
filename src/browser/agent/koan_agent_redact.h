// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_REDACT_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_REDACT_H_

#include <set>
#include <string>
#include <string_view>

#include "base/values.h"

// What an agent may not read off a user's tab that it was only allowed to read
// (.agent/notes/agent-control.md section 9.3; #196 review, N1): the `value` of
// an <input type=hidden> (CSRF and session tokens), <input type=password> or
// a card field (autocomplete="cc-*").
// The page's DOM answers carry those as attributes, so the facade passes the
// answers to DOM methods and the DOM events through here on a readable user
// tab. The agent's own tabs are not redacted: it typed what is there.
//
// The value is replaced by an empty string; the attribute stays, so the
// page's structure is what the agent expects. Pure logic.
namespace koan::agent {

// Redacts `params` (a DOM command's result, or a DOM event's params) in place:
//  - DOM.getOuterHTML's `outerHTML`;
//  - DOM.getAttributes' `attributes` (a flat [name, value, ...] list; the
//    answer does not say which element it is, so any element with such a type
//    is redacted);
//  - every Node object (one with `nodeName` and `attributes`) anywhere in it:
//    DOM.getDocument, DOM.describeNode, DOM.setChildNodes,
//    DOM.childNodeInserted ...;
//  - DOM.attributeModified of a `value` attribute (the event does not say
//    which element it is either).
// Other methods are left alone.
void RedactSecretInputs(std::string_view method, base::DictValue& params);

// The HTML part: every <input> start tag whose first `type` attribute is
// hidden or password (any case, surrounding ASCII spaces ignored), or whose
// first `autocomplete` has a cc-* token, has the values of its `value`
// attributes emptied. Comments and the contents of raw
// text elements (script, style, ...) are not tags. A tag cut off by the end of
// the string is treated as secret.
std::string RedactSecretInputsInHtml(std::string_view html);
// The same, for password inputs only (`hidden`: false) or for both.
std::string RedactInputsInHtml(std::string_view html, bool hidden);

// The attribute list part: a flat [name, value, ...] list.
void RedactSecretInputsInAttributes(base::ListValue& attributes);

// The secret fields of a tab the agent drives (its own, or a user's tab it
// was given; design section 4.5, "Secrets Koan filled"), which every answer
// and event hides too: the agent may see that such a field is filled, never
// its value. `fields` are the backend DOM node ids, in the renderer process of
// the session the message comes from, of the fields whose value is a secret:
// password fields and the fields autofill classifies as a card number, a CVC
// or an IBAN (IsSecretField(), koan_agent_secret_fields.h).
//  - DOM answers and events (getOuterHTML, getAttributes, describeNode,
//    getDocument, setChildNodes, ...): the `value` attribute of every
//    password input and card field (autocomplete="cc-*"; hidden inputs too
//    with `hidden`, as RedactSecretInputs() does), and of every Node object
//    of a field in `fields`, is emptied. A DOM answer carries no live value
//    (that is the `value` property, which only script and the accessibility
//    tree read; DOMSnapshot, which has it, is a denied domain);
//  - Accessibility answers and events (getFullAXTree, getPartialAXTree,
//    queryAXTree, getChildAXNodes, getRootAXNode, getAXNodeAndAncestors,
//    nodesUpdated, loadComplete): the value of a node of a field in
//    `fields` becomes kSecretFilled or kSecretEmpty, and the name and value
//    of every node inside it are emptied. (Blink already shows a password
//    field's value as dots.)
inline constexpr char kSecretFilled[] =
    "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 (filled)";
inline constexpr char kSecretEmpty[] =
    "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 (empty)";
void RedactSecretFields(std::string_view method,
                        base::DictValue& params,
                        const std::set<int>& fields,
                        bool hidden);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_REDACT_H_
