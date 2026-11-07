// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_filters.h"

#include <algorithm>
#include <utility>

#include "url/gurl.h"

namespace koan::agent {

namespace {

FilterError Error(std::string code, std::string message) {
  return FilterError{std::move(code), std::move(message)};
}

// Lowers a numeric parameter to `cap`, or sets it if absent or not a number.
void CapOrSet(base::DictValue& params, std::string_view key, int cap) {
  const base::Value* value = params.Find(key);
  if (value && value->is_int() && value->GetInt() >= 0 &&
      value->GetInt() <= cap) {
    return;
  }
  params.Set(key, cap);
}

std::optional<FilterError> FilterNavigationUrl(base::DictValue& params) {
  const std::string* url = params.FindString("url");
  if (!url) {
    return Error("Koan.invalidParams", "url must be a string");
  }
  // The GURL is built from the exact wire string, and that same string is what
  // is forwarded: nothing is re-serialised.
  const GURL parsed(*url);
  if (!IsAllowedNavigationUrl(parsed)) {
    return Error("Koan.urlNotAllowed", "navigation to " +
                                           DisallowedNavigationScheme(parsed) +
                                           " is not allowed");
  }
  return std::nullopt;
}

std::optional<FilterError> FilterScreenshot(base::DictValue& params,
                                            const FilterContext& context) {
  const base::Value* from_surface = params.Find("fromSurface");
  if (from_surface && !(from_surface->is_bool() && from_surface->GetBool())) {
    return Error("Koan.notPermitted",
                 "fromSurface: false is not allowed (it needs a visible "
                 "window)");
  }
  if (!context.tab_is_agent_owned) {
    // clip, captureBeyondViewport and emulation resize the real view, which
    // would flicker the user's own tab.
    const base::Value* beyond = params.Find("captureBeyondViewport");
    if (params.contains("clip") ||
        (beyond && !(beyond->is_bool() && !beyond->GetBool()))) {
      return Error("Koan.notPermitted",
                   "clip and captureBeyondViewport are only allowed on tabs "
                   "the agent owns");
    }
  }
  return std::nullopt;
}

}  // namespace

StreamRegistry::StreamRegistry() = default;
StreamRegistry::~StreamRegistry() = default;

bool StreamRegistry::Add(std::string handle) {
  if (handles_.size() >= kMaxHandles && !handles_.contains(handle)) {
    return false;
  }
  handles_.insert(std::move(handle));
  return true;
}

bool StreamRegistry::Contains(std::string_view handle) const {
  return handles_.contains(handle);
}

void StreamRegistry::Remove(std::string_view handle) {
  auto it = handles_.find(handle);
  if (it != handles_.end()) {
    handles_.erase(it);
  }
}

bool StreamRegistry::RecordResult(std::string_view method,
                                  const base::DictValue& result) {
  // The one allowed method that makes a stream: printToPDF with
  // transferMode ReturnAsStream answers with {stream: handle}.
  if (method != "Page.printToPDF") {
    return true;
  }
  const std::string* stream = result.FindString("stream");
  return !stream || Add(*stream);
}

std::optional<FilterError> ApplyParamFilter(ParamFilter filter,
                                            base::DictValue& params,
                                            const FilterContext& context) {
  switch (filter) {
    case ParamFilter::kNone:
      return std::nullopt;
    case ParamFilter::kNavigationUrl:
      return FilterNavigationUrl(params);
    case ParamFilter::kStripKeyCommands:
      params.Remove("commands");
      return std::nullopt;
    case ParamFilter::kIsolatedWorld:
      // A world with universal access bypasses the same-origin policy.
      params.Remove("grantUniversalAccess");
      params.Set("grantUniveralAccess", false);
      return std::nullopt;
    case ParamFilter::kScreenshot:
      return FilterScreenshot(params, context);
    case ParamFilter::kNetworkEnable:
      // Bodies are never inlined into requestWillBeSent, and the buffers are
      // bounded whatever the agent asked for.
      params.Remove("maxPostDataSize");
      params.Remove("reportDirectSocketTraffic");
      params.Remove("enableDurableMessages");
      CapOrSet(params, "maxTotalBufferSize", kMaxNetworkTotalBufferSize);
      CapOrSet(params, "maxResourceBufferSize", kMaxNetworkResourceBufferSize);
      return std::nullopt;
    case ParamFilter::kOwnStream: {
      const std::string* handle = params.FindString("handle");
      if (!handle || !context.streams || !context.streams->Contains(*handle)) {
        return Error("Koan.notPermitted", "not a stream this session created");
      }
      return std::nullopt;
    }
    case ParamFilter::kOwnTabOnly:
      if (!context.tab_is_agent_owned) {
        return Error("Koan.notPermitted",
                     "only allowed on tabs the agent owns");
      }
      return std::nullopt;
    case ParamFilter::kRuntimeContext:
      return Error("Koan.unavailable",
                   "the execution context rule is not implemented yet");
  }
  return Error("Koan.unavailable", "unknown filter");
}

}  // namespace koan::agent
