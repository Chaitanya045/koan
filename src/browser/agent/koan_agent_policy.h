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
// 4.3). Per-method tables, and unlisted means deny: a method is allowed only
// by its full name, there are no domain wildcards, and a Chromium roll that
// adds a protocol method cannot make it reachable. The table is data in
// koan_agent_policy.cc; scripts/agent/check_policy_coverage.py checks it
// against the protocol of a Chromium checkout, so every method of a classified
// domain is listed on purpose and every other domain is explicitly denied.
//
// Classify() is the table, not the gateway. Nothing may route client traffic
// through it until the parameter filters named by ParamFilter and the event
// scrubber exist (design sections 4.3 to 4.5): see the note on kAllow.
//
// Koan.* methods belong to the gateway and are not in this table; Classify()
// says deny for them.
namespace koan::agent {

enum class Verdict {
  // Forwarded to the tab's untrusted DevTools client, after the filter named
  // in ParamFilter if there is one. kAllow is NOT "forward unchanged".
  kAllow,
  // Forwarded only if the client has the capability in `gate`; v1 grants none.
  kGated,
  // Answered with success without doing anything (e.g. Page.bringToFront:
  // agent tabs are never raised).
  kNoOp,
  // Answered by the gateway's browser-level facade, never forwarded; its
  // filter, if any, is applied by the facade.
  kFacade,
  // Refused with an explicit CDP error.
  kDeny,
};

// A capability a gated method needs. v1 offers none of these (decision 11).
enum class Capability {
  kNone,
  kNetwork,
};

// The filter a method's parameters (or results, or the target) must pass
// before an allowed or facade call is forwarded. The table names the filter;
// the filters themselves are the next PR. A row with kNone is forwarded as is.
enum class ParamFilter {
  kNone,
  // `url` must pass IsAllowedNavigationUrl() (Page.navigate, and the facade's
  // Target.createTarget).
  kNavigationUrl,
  // Remove `commands` from the event (Input.dispatchKeyEvent): "paste" would
  // read the OS clipboard, "copy" and "cut" write it.
  kStripKeyCommands,
  // grantUniveralAccess (sic, upstream's spelling) is forced to false, and the
  // new world is registered with the Runtime context rule
  // (Page.createIsolatedWorld).
  kIsolatedWorld,
  // contextId, uniqueContextId and executionContextId must name the main
  // world or a world the agent created (design section 4.4): Runtime.evaluate,
  // callFunctionOn and addBinding, and DOM.resolveNode, whose
  // executionContextId would otherwise hand back an object of another world.
  // The other methods that take an id (objectId, frameId, targetId) need no
  // filter of their own: an objectId is one this session was given, and a
  // frame outside the tab's process (cross-origin, an extension's) needs an
  // attach of its own, which design section 4.4 refuses.
  kRuntimeContext,
  // fromSurface:false is refused; clip, captureBeyondViewport and emulation
  // only on agent-owned tabs.
  kScreenshot,
  // maxPostDataSize is left unset (so bodies are never inlined into
  // Network.requestWillBeSent) and the buffer sizes are capped; the events
  // pass the allowlist and the scrubber (design section 4.5).
  kNetworkEnable,
  // The stream handle must be one this session created (IO.read, IO.close).
  kOwnStream,
  // Only on tabs the agent owns (Emulation.*). Keeping a single writer per tab
  // is the gateway's job, not this filter's.
  kOwnTabOnly,
};

struct MethodRule {
  std::string_view method;  // "Domain.method"
  Verdict verdict;
  Capability gate;
  ParamFilter filter;
};

struct Decision {
  Verdict verdict = Verdict::kDeny;
  Capability gate = Capability::kNone;
  ParamFilter filter = ParamFilter::kNone;
};

// The rule for `method` ("Domain.method"); a deny for anything not listed,
// including names with other case, whitespace, non-ASCII look-alikes or a NUL.
Decision Classify(std::string_view method);

// The explicit rules, sorted by method name, and the domains denied as a
// whole. For the tests and the coverage check.
base::span<const MethodRule> MethodRules();
base::span<const std::string_view> DeniedDomains();

// Navigations the agent starts (Page.navigate, Target.createTarget,
// Koan.openTab, a run step's `go`) may only go to http, https and about:blank,
// and to a URL of at most url::kMaxURLChars (2 MB, the most Chromium passes
// between processes; GURL itself parses any length). Anything else
// (javascript:, file:, chrome:, view-source:, data:, blob:, ...) is refused
// before dispatch with Koan.urlNotAllowed, instead of surprising the agent with
// a detach later.
//
// The caller must build `url` from the exact string that arrived on the wire
// and navigate that same string (content's PageHandler::Navigate does
// GURL(url) itself), never a re-serialised or "fixed-up" copy: the filter is
// only as good as the URL it was given. It cannot see a history entry's URL,
// so Page.navigateToHistoryEntry relies on MayAttachToURL (design section 4.4).
bool IsAllowedNavigationUrl(const GURL& url);

// The scheme to name in the Koan.urlNotAllowed error and the audit log: the
// URL's scheme (at most 32 characters, then "..."), "(invalid)" for a URL that
// does not parse, or "(too long)" for one over the length limit. A valid scheme
// is short ASCII, so neither marker can be one.
std::string DisallowedNavigationScheme(const GURL& url);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_POLICY_H_
