// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_EVENTS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_EVENTS_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/values.h"

// What events an agent client may receive, and what is taken out of them
// (.agent/notes/agent-control.md section 4.5). The mirror of koan_agent_policy
// for the other direction: an event is delivered only if it is in the allowlist
// below, and the Network events are rewritten so that no cookie, no
// Authorization header and no request body reaches the agent. An event that is
// not listed is dropped: a Chromium roll that adds an event cannot make it
// reachable, and scripts/agent/check_policy_coverage.py flags it for review.
//
// Nothing may route client traffic through this until the gateway applies
// FilterEvent() to every event it forwards.
//
// The scrubber is written for the shapes the Network events have: objects named
// *headers (a map of header name to value) and the field names the scrubber
// matches. It also removes the elements of an array that look like
// {name, value} header entries (Fetch.requestPaused.responseHeaders is one) so
// that it can be used for Fetch events if they are ever allowlisted; today no
// Fetch event is. scripts/agent/check_policy_coverage.py hashes each
// allowlisted event together with every type it reaches, so a new field in any
// of them is a review, and fails if an event delivered as it is reaches a
// secret or a context.
namespace koan::agent {

enum class EventAction {
  // Delivered as it is.
  kPass,
  // A Network event: delivered after the scrubber has removed cookies, the
  // Cookie, Set-Cookie, Authorization and Proxy-Authorization headers, raw
  // header text and request bodies.
  kScrubNetwork,
  // An event that belongs to an execution context: Runtime.executionContext*,
  // and the console, exception and binding events, whose
  // executionContextId would otherwise let the console output and exceptions
  // of an extension's content script (its own isolated world) reach the agent.
  // They are delivered only for the main world and for the worlds the agent
  // created (design section 4.4). That needs the gateway's per-session context
  // tracker, so FilterEvent() does not deliver them.
  kHoldForContextTracker,
};

struct EventRule {
  std::string_view method;  // "Domain.event"
  EventAction action;
};

enum class EventVerdict {
  // Deliver the event, with `params` as FilterEvent() left them.
  kDeliver,
  // Do not deliver it.
  kDrop,
  // Hold it for the context tracker, which decides. **Only kDeliver delivers**:
  // a caller that cannot track contexts drops this (do not test `!= kDrop`).
  kHoldForContextTracker,
};

// Applies the allowlist and, for a Network event, the scrubber to `params` (the
// event's "params" object, modified in place). An event that is not listed, or
// whose params are nested deeper than the scrubber will follow, is dropped.
EventVerdict FilterEvent(std::string_view method, base::DictValue& params);

// The explicit rules, sorted by method name. For the tests and the coverage
// check.
base::span<const EventRule> EventRules();

// Removes from `dict`, recursively, what the Network scrubber removes:
//  - any entry whose name contains "cookie" or "postdata" (any case), and any
//    entry whose name ends in "headerstext": raw header text carries the same
//    secrets as the headers;
//  - from any object whose name ends in "headers", the headers Cookie, Cookie2,
//    Set-Cookie, Set-Cookie2, Authorization and Proxy-Authorization (any case);
//  - from any array, the elements that are {name, value} entries naming one of
//    those headers.
// Returns false, leaving `dict` partly scrubbed, if it is nested more than
// kMaxScrubDepth deep: the caller must not deliver it.
inline constexpr int kMaxScrubDepth = 32;
bool ScrubNetworkParams(base::DictValue& dict);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_EVENTS_H_
