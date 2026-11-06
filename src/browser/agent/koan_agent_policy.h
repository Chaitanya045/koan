// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_POLICY_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_POLICY_H_

#include <string>
#include <string_view>

#include "base/containers/span.h"

class GURL;

// What an agent client may ask of a tab (.agent/notes/agent-control.md section
// 4.3). Per-method tables, and **unlisted = deny**: a method is allowed only
// by its full name, there are no domain wildcards, and a Chromium roll that
// adds a protocol method cannot make it reachable. The table is data in
// koan_agent_policy.cc; scripts/agent/check_policy_coverage.py checks it
// against the protocol of a Chromium checkout, so every method of a classified
// domain is listed on purpose and every other domain is explicitly denied.
namespace koan::agent {

enum class Verdict {
  // Forwarded to the tab's untrusted DevTools client.
  kAllow,
  // Forwarded only if the client has the capability in `gate`; v1 grants none.
  kGated,
  // Answered with success without doing anything (e.g. Page.bringToFront:
  // agent tabs are never raised).
  kNoOp,
  // Answered by the gateway's browser-level facade, never forwarded.
  kFacade,
  // Refused with an explicit CDP error.
  kDeny,
};

// A capability a gated method needs. v1 offers none of these (decision 11).
enum class Capability {
  kNone,
  kNetwork,
};

struct MethodRule {
  std::string_view method;  // "Domain.method"
  Verdict verdict;
  Capability gate;
};

struct Decision {
  Verdict verdict = Verdict::kDeny;
  Capability gate = Capability::kNone;
};

// The rule for `method` ("Domain.method"); a deny for anything not listed,
// including names with other case, whitespace or a NUL.
Decision Classify(std::string_view method);

// The explicit rules, sorted by method name, and the domains denied as a
// whole. For the tests and the coverage check.
base::span<const MethodRule> MethodRules();
base::span<const std::string_view> DeniedDomains();

// Navigations the agent starts (Page.navigate, Target.createTarget,
// Koan.openTab, a run step's `go`) may only go to http, https and about:blank.
// Anything else (javascript:, file:, chrome:, view-source:, data:, blob:, ...)
// is refused before dispatch with Koan.urlNotAllowed, instead of surprising
// the agent with a detach later.
bool IsAllowedNavigationUrl(const GURL& url);

// The scheme to name in the Koan.urlNotAllowed error: the URL's scheme, or
// "invalid" for a URL that does not parse.
std::string DisallowedNavigationScheme(const GURL& url);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_POLICY_H_
