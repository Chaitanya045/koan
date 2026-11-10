// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_REDACT_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_REDACT_H_

#include <string>
#include <string_view>

#include "base/values.h"

// What an agent may not read off a user's tab that it was only allowed to read
// (.agent/notes/agent-control.md section 9.3; #196 review, N1): the `value` of
// an <input type=hidden> (CSRF and session tokens) or <input type=password>.
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
// hidden or password (any case, surrounding ASCII spaces ignored) has the
// values of its `value` attributes emptied. Comments and the contents of raw
// text elements (script, style, ...) are not tags. A tag cut off by the end of
// the string is treated as secret.
std::string RedactSecretInputsInHtml(std::string_view html);

// The attribute list part: a flat [name, value, ...] list.
void RedactSecretInputsInAttributes(base::ListValue& attributes);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_REDACT_H_
