// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_CONTEXTS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_CONTEXTS_H_

#include <stddef.h>

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// The execution contexts an agent may see and name in one attached tab
// (.agent/notes/agent-control.md section 4.4): the main world of each frame,
// and the isolated worlds the agent made itself with Page.createIsolatedWorld.
// Every other world (an extension's content script, Chromium's own utility
// worlds) is invisible: its Runtime.executionContextCreated, console messages,
// exceptions and binding calls are dropped, and a command that names its id is
// refused.
//
// The order the renderer reports a new isolated world in is: the
// executionContextCreated event first, then the answer to createIsolatedWorld
// with its executionContextId. So while a createIsolatedWorld is in flight, an
// isolated context's events are held, and the answer decides: the context the
// answer names is the agent's and its events are delivered in order; when no
// call is in flight any more, the rest are dropped. A world's name says
// nothing (an agent can name its world after an extension), so it is never
// used to decide.
//
// One tracker per attached tab and session; not thread-safe.
namespace koan::agent {

// An event as it is delivered: the method and its params.
struct ContextEvent {
  std::string method;
  base::DictValue params;
};

class ContextTracker {
 public:
  // The most contexts remembered, and the most events held while a world is
  // being made. Over either limit, new ones are dropped (never delivered).
  static constexpr size_t kMaxContexts = 1024;
  static constexpr size_t kMaxHeldEvents = 256;

  ContextTracker();
  ContextTracker(const ContextTracker&) = delete;
  ContextTracker& operator=(const ContextTracker&) = delete;
  ~ContextTracker();

  // A Page.createIsolatedWorld was forwarded to the tab.
  void OnIsolatedWorldRequested();
  // Its answer: the new world's executionContextId, or nullopt for an error
  // (or an answer without one). Returns the held events this releases, in the
  // order they arrived.
  std::vector<ContextEvent> OnIsolatedWorldAnswered(
      std::optional<int> context_id);

  // An event FilterEvent() held for the tracker (kHoldForContextTracker).
  // Returns the events to deliver now, in order: none if it is dropped or
  // held, the event itself if it belongs to a context the agent may see.
  std::vector<ContextEvent> OnEvent(std::string method, base::DictValue params);

  // Whether a command may name this context.
  bool IsAllowedContextId(int id) const;
  bool IsAllowedUniqueId(std::string_view unique_id) const;

  size_t held_count_for_testing() const { return held_.size(); }

 private:
  struct Context {
    std::string unique_id;
  };

  enum class Owner { kAllowed, kCandidate, kOther };

  // Whose context `id` is: one the agent may see, one waiting for an answer,
  // or another world's.
  Owner OwnerOf(int id) const;
  void Allow(int id, std::string unique_id);
  void Forget(int id);
  std::vector<ContextEvent> OnContextCreated(std::string method,
                                             base::DictValue params);
  std::vector<ContextEvent> Hold(std::string method,
                                 base::DictValue params,
                                 int context_id);
  std::vector<ContextEvent> One(std::string method, base::DictValue params);

  std::map<int, Context> allowed_;
  std::set<std::string, std::less<>> allowed_unique_ids_;
  // Isolated contexts reported while a createIsolatedWorld was in flight.
  std::map<int, Context> candidates_;
  struct Held {
    int context_id;
    ContextEvent event;
  };
  std::deque<Held> held_;
  int worlds_in_flight_ = 0;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_CONTEXTS_H_
