// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_contexts.h"

#include <utility>

namespace koan::agent {

namespace {

// The context an event belongs to, for the events that name one.
std::optional<int> EventContextId(std::string_view method,
                                  const base::DictValue& params) {
  if (method == "Runtime.exceptionThrown") {
    const base::DictValue* details = params.FindDict("exceptionDetails");
    return details ? details->FindInt("executionContextId") : std::nullopt;
  }
  // Runtime.consoleAPICalled, bindingCalled and executionContextDestroyed.
  return params.FindInt("executionContextId");
}

}  // namespace

ContextTracker::ContextTracker() = default;
ContextTracker::~ContextTracker() = default;

void ContextTracker::OnIsolatedWorldRequested() {
  ++worlds_in_flight_;
}

std::vector<ContextEvent> ContextTracker::OnIsolatedWorldAnswered(
    std::optional<int> context_id) {
  if (worlds_in_flight_ > 0) {
    --worlds_in_flight_;
  }
  if (context_id) {
    // The answer is what makes a world the agent's, whether or not Runtime is
    // enabled (without it there is no created event to have held).
    auto candidate = candidates_.find(*context_id);
    std::string unique_id;
    if (candidate != candidates_.end()) {
      unique_id = std::move(candidate->second.unique_id);
      candidates_.erase(candidate);
    }
    Allow(*context_id, std::move(unique_id));
  }

  std::vector<ContextEvent> released;
  std::deque<Held> still_held;
  for (Held& held : held_) {
    if (allowed_.contains(held.context_id)) {
      released.push_back(std::move(held.event));
    } else if (worlds_in_flight_ > 0) {
      still_held.push_back(std::move(held));
    }
  }
  held_ = std::move(still_held);
  if (worlds_in_flight_ == 0) {
    candidates_.clear();
  }
  // A context the answer allowed may have been destroyed while it was held.
  for (const ContextEvent& event : released) {
    if (event.method == "Runtime.executionContextDestroyed") {
      if (std::optional<int> id = EventContextId(event.method, event.params)) {
        Forget(*id);
      }
    }
  }
  return released;
}

std::vector<ContextEvent> ContextTracker::OnEvent(std::string method,
                                                  base::DictValue params) {
  if (method == "Runtime.executionContextCreated") {
    return OnContextCreated(std::move(method), std::move(params));
  }
  if (method == "Runtime.executionContextsCleared") {
    // Every context of the page is gone (a navigation, Runtime.disable). A
    // world still being made keeps its call in flight; its answer allows it.
    allowed_.clear();
    allowed_unique_ids_.clear();
    candidates_.clear();
    held_.clear();
    return One(std::move(method), std::move(params));
  }

  const std::optional<int> id = EventContextId(method, params);
  if (!id) {
    // An exception or console message without a context: nothing says whose
    // it is.
    return {};
  }
  switch (OwnerOf(*id)) {
    case Owner::kAllowed: {
      if (method == "Runtime.executionContextDestroyed") {
        Forget(*id);
      }
      return One(std::move(method), std::move(params));
    }
    case Owner::kCandidate:
      if (method == "Runtime.executionContextDestroyed") {
        candidates_.erase(*id);
      }
      return Hold(std::move(method), std::move(params), *id);
    case Owner::kOther:
      return {};
  }
  return {};
}

bool ContextTracker::IsAllowedContextId(int id) const {
  return allowed_.contains(id);
}

bool ContextTracker::IsAllowedUniqueId(std::string_view unique_id) const {
  return !unique_id.empty() && allowed_unique_ids_.contains(unique_id);
}

ContextTracker::Owner ContextTracker::OwnerOf(int id) const {
  if (allowed_.contains(id)) {
    return Owner::kAllowed;
  }
  if (worlds_in_flight_ > 0 && candidates_.contains(id)) {
    return Owner::kCandidate;
  }
  return Owner::kOther;
}

void ContextTracker::Allow(int id, std::string unique_id) {
  if (allowed_.size() >= kMaxContexts && !allowed_.contains(id)) {
    return;
  }
  if (!unique_id.empty()) {
    allowed_unique_ids_.insert(unique_id);
  }
  allowed_[id] = Context{std::move(unique_id)};
}

void ContextTracker::Forget(int id) {
  auto it = allowed_.find(id);
  if (it == allowed_.end()) {
    return;
  }
  allowed_unique_ids_.erase(it->second.unique_id);
  allowed_.erase(it);
}

std::vector<ContextEvent> ContextTracker::OnContextCreated(
    std::string method,
    base::DictValue params) {
  const base::DictValue* context = params.FindDict("context");
  if (!context) {
    return {};
  }
  const std::optional<int> id = context->FindInt("id");
  if (!id) {
    return {};
  }
  const std::string* unique = context->FindString("uniqueId");
  std::string unique_id = unique ? *unique : std::string();
  const base::DictValue* aux = context->FindDict("auxData");
  const std::string* type = aux ? aux->FindString("type") : nullptr;

  // The main world of a frame.
  if (aux && aux->FindBool("isDefault").value_or(false) && type &&
      *type == "default") {
    Allow(*id, std::move(unique_id));
    return IsAllowedContextId(*id) ? One(std::move(method), std::move(params))
                                   : std::vector<ContextEvent>();
  }
  // A world the agent made, reported again (Runtime.enable after a disable).
  if (IsAllowedContextId(*id)) {
    return One(std::move(method), std::move(params));
  }
  // Maybe the world the agent is making; the answer decides.
  if (type && *type == "isolated" && worlds_in_flight_ > 0 &&
      candidates_.size() < kMaxContexts) {
    candidates_[*id] = Context{std::move(unique_id)};
    return Hold(std::move(method), std::move(params), *id);
  }
  return {};
}

std::vector<ContextEvent> ContextTracker::Hold(std::string method,
                                               base::DictValue params,
                                               int context_id) {
  if (held_.size() < kMaxHeldEvents) {
    held_.push_back(
        Held{context_id, ContextEvent{std::move(method), std::move(params)}});
  }
  return {};
}

std::vector<ContextEvent> ContextTracker::One(std::string method,
                                              base::DictValue params) {
  std::vector<ContextEvent> events;
  events.push_back(ContextEvent{std::move(method), std::move(params)});
  return events;
}

}  // namespace koan::agent
