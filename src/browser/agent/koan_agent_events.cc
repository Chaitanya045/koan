// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_events.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "base/strings/string_util.h"

namespace koan::agent {

namespace {

// The events an agent client receives. Sorted by method name, because
// FilterEvent() binary-searches. The lines between the markers are parsed by
// scripts/agent/check_policy_coverage.py, which checks that each exists in the
// protocol of a Chromium checkout. An event that is not here is dropped.
//
// BEGIN EVENTS
constexpr auto kEvents = std::to_array<EventRule>({
    {"Accessibility.loadComplete", EventAction::kPass},
    {"Accessibility.nodesUpdated", EventAction::kPass},
    {"DOM.attributeModified", EventAction::kPass},
    {"DOM.attributeRemoved", EventAction::kPass},
    {"DOM.characterDataModified", EventAction::kPass},
    {"DOM.childNodeCountUpdated", EventAction::kPass},
    {"DOM.childNodeInserted", EventAction::kPass},
    {"DOM.childNodeRemoved", EventAction::kPass},
    {"DOM.documentUpdated", EventAction::kPass},
    {"DOM.setChildNodes", EventAction::kPass},
    {"DOM.shadowRootPopped", EventAction::kPass},
    {"DOM.shadowRootPushed", EventAction::kPass},
    {"Inspector.detached", EventAction::kPass},
    {"Inspector.targetCrashed", EventAction::kPass},
    {"Inspector.targetReloadedAfterCrash", EventAction::kPass},
    {"Log.entryAdded", EventAction::kPass},
    {"Network.loadingFailed", EventAction::kScrubNetwork},
    {"Network.loadingFinished", EventAction::kScrubNetwork},
    {"Network.requestServedFromCache", EventAction::kScrubNetwork},
    {"Network.requestWillBeSent", EventAction::kScrubNetwork},
    {"Network.requestWillBeSentExtraInfo", EventAction::kScrubNetwork},
    {"Network.responseReceived", EventAction::kScrubNetwork},
    {"Network.responseReceivedExtraInfo", EventAction::kScrubNetwork},
    {"Network.webSocketCreated", EventAction::kScrubNetwork},
    {"Page.domContentEventFired", EventAction::kPass},
    {"Page.frameAttached", EventAction::kPass},
    {"Page.frameDetached", EventAction::kPass},
    {"Page.frameNavigated", EventAction::kPass},
    {"Page.frameStartedLoading", EventAction::kPass},
    {"Page.frameStoppedLoading", EventAction::kPass},
    {"Page.javascriptDialogClosed", EventAction::kPass},
    {"Page.javascriptDialogOpening", EventAction::kPass},
    {"Page.lifecycleEvent", EventAction::kPass},
    {"Page.loadEventFired", EventAction::kPass},
    {"Page.navigatedWithinDocument", EventAction::kPass},
    {"Page.screencastFrame", EventAction::kPass},
    {"Page.screencastVisibilityChanged", EventAction::kPass},
    {"Runtime.bindingCalled", EventAction::kPass},
    {"Runtime.consoleAPICalled", EventAction::kPass},
    {"Runtime.exceptionThrown", EventAction::kPass},
    {"Runtime.executionContextCreated", EventAction::kNeedsContextTracker},
    {"Runtime.executionContextDestroyed", EventAction::kNeedsContextTracker},
    {"Runtime.executionContextsCleared", EventAction::kNeedsContextTracker},
});
// END EVENTS

constexpr bool EventsAreSortedAndUnique() {
  for (size_t i = 1; i < kEvents.size(); ++i) {
    if (!(kEvents[i - 1].method < kEvents[i].method)) {
      return false;
    }
  }
  return true;
}

static_assert(EventsAreSortedAndUnique(),
              "kEvents must be sorted by method name, without duplicates");

// Headers that carry credentials, lower case.
constexpr std::array<std::string_view, 6> kSecretHeaders = {
    "authorization",       "cookie",     "cookie2",
    "proxy-authorization", "set-cookie", "set-cookie2",
};

bool IsSecretHeader(std::string_view name) {
  const std::string lower = base::ToLowerASCII(name);
  return std::ranges::find(kSecretHeaders, lower) != kSecretHeaders.end();
}

// An entry whose name says it holds cookies or a request body, or raw header
// text, is removed whatever it is nested in.
bool IsSecretKey(std::string_view name) {
  const std::string lower = base::ToLowerASCII(name);
  return lower.find("cookie") != std::string::npos ||
         lower.find("postdata") != std::string::npos ||
         lower.ends_with("headerstext");
}

bool ScrubDict(base::DictValue& dict, int depth, bool is_headers);

bool ScrubValue(base::Value& value, int depth) {
  if (base::DictValue* dict = value.GetIfDict()) {
    return ScrubDict(*dict, depth, /*is_headers=*/false);
  }
  if (base::ListValue* list = value.GetIfList()) {
    if (depth > kMaxScrubDepth) {
      return false;
    }
    for (base::Value& element : *list) {
      if (!ScrubValue(element, depth + 1)) {
        return false;
      }
    }
  }
  return true;
}

bool ScrubDict(base::DictValue& dict, int depth, bool is_headers) {
  if (depth > kMaxScrubDepth) {
    return false;
  }
  std::vector<std::string> remove;
  for (auto [key, value] : dict) {
    if (is_headers ? IsSecretHeader(key) : IsSecretKey(key)) {
      remove.push_back(key);
      continue;
    }
    if (base::DictValue* child = value.GetIfDict()) {
      const bool child_is_headers =
          base::ToLowerASCII(key).ends_with("headers");
      if (!ScrubDict(*child, depth + 1, child_is_headers)) {
        return false;
      }
    } else if (!ScrubValue(value, depth + 1)) {
      return false;
    }
  }
  for (const std::string& key : remove) {
    dict.Remove(key);
  }
  return true;
}

}  // namespace

bool ScrubNetworkParams(base::DictValue& dict) {
  return ScrubDict(dict, /*depth=*/0, /*is_headers=*/false);
}

EventVerdict FilterEvent(std::string_view method, base::DictValue& params) {
  const auto it =
      std::lower_bound(kEvents.begin(), kEvents.end(), method,
                       [](const EventRule& rule, std::string_view name) {
                         return rule.method < name;
                       });
  if (it == kEvents.end() || it->method != method) {
    return EventVerdict::kDrop;
  }
  switch (it->action) {
    case EventAction::kPass:
      return EventVerdict::kDeliver;
    case EventAction::kScrubNetwork:
      return ScrubNetworkParams(params) ? EventVerdict::kDeliver
                                        : EventVerdict::kDrop;
    case EventAction::kNeedsContextTracker:
      return EventVerdict::kNeedsContextTracker;
  }
  return EventVerdict::kDrop;
}

base::span<const EventRule> EventRules() {
  return kEvents;
}

}  // namespace koan::agent
