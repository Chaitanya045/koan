// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_FILTERS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_FILTERS_H_

#include <stddef.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_policy.h"

// The parameter filters of the method policy (.agent/notes/agent-control.md
// section 4.3): what a forwarded method's parameters must satisfy, or are
// rewritten to, before the gateway sends the command to the tab. Each
// ParamFilter named in the policy table is applied here. A filter either
// accepts the (possibly rewritten) parameters or returns the error to send back
// to the agent instead; it never forwards anything itself.
//
// Fail closed: a filter that needs state the caller did not provide, or that
// is not implemented yet, refuses.
namespace koan::agent {

// The most Network.enable may buffer for an agent: it never needs more, and a
// client must not be able to make the browser hold more.
inline constexpr int kMaxNetworkTotalBufferSize = 10 * 1024 * 1024;
inline constexpr int kMaxNetworkResourceBufferSize = 5 * 1024 * 1024;

// The IO stream handles a session may read: the ones its own commands created
// (Page.printToPDF with transferMode ReturnAsStream), and no others. Bounded.
class StreamRegistry {
 public:
  static constexpr size_t kMaxHandles = 64;

  StreamRegistry();
  StreamRegistry(const StreamRegistry&) = delete;
  StreamRegistry& operator=(const StreamRegistry&) = delete;
  ~StreamRegistry();

  // Returns false if the registry is full (the caller then closes the stream).
  bool Add(std::string handle);
  bool Contains(std::string_view handle) const;
  void Remove(std::string_view handle);

  // Records the stream a command's result created, if the method creates one.
  // `method` is the command that was answered, `result` the answer. Returns
  // false if the stream could not be recorded.
  bool RecordResult(std::string_view method, const base::DictValue& result);

 private:
  std::set<std::string, std::less<>> handles_;
};

// What the filters need to know about the call. The pointers are not owned.
struct FilterContext {
  // The tab was opened by the agent (as opposed to the user's own tab the agent
  // was handed). Some methods change the page's presentation and are for agent
  // tabs only.
  bool tab_is_agent_owned = false;
  // The session's streams, for kOwnStream.
  raw_ptr<StreamRegistry> streams = nullptr;
};

// The error to answer the agent's command with.
struct FilterError {
  // A CDP error code name, e.g. "Koan.urlNotAllowed", "Koan.notPermitted",
  // "Koan.invalidParams", "Koan.unavailable".
  std::string code;
  std::string message;
};

// Applies `filter` to `params` (the command's "params" object, modified in
// place). Returns nullopt if the command may be forwarded with the params as
// they are now, or the error to answer with.
//
//  kNone             nothing.
//  kNavigationUrl    `url` must be a string that passes
//  IsAllowedNavigationUrl(). kStripKeyCommands removes `commands`.
//  kIsolatedWorld    sets `grantUniveralAccess` to false (and removes the
//                    correctly spelled variant, in case upstream fixes it).
//  kScreenshot       refuses `fromSurface: false`; `clip` and
//                    `captureBeyondViewport: true` only on agent-owned tabs.
//  kNetworkEnable    removes `maxPostDataSize`, caps the two buffer sizes
//                    (setting them if absent), removes the options that open
//                    more traffic.
//  kOwnStream        `handle` must be a stream in context.streams.
//  kOwnTabOnly       only on agent-owned tabs.
//  kRuntimeContext   not implemented yet (it needs the session's execution
//                    context tracker): refuses.
std::optional<FilterError> ApplyParamFilter(ParamFilter filter,
                                            base::DictValue& params,
                                            const FilterContext& context);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FILTERS_H_
