// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_filters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "url/gurl.h"

namespace koan::agent {

namespace {

FilterError Error(std::string code, std::string message) {
  return FilterError{std::move(code), std::move(message)};
}

// Removes every entry of `params` whose name is not in `known`. A parameter the
// filter's author did not review never reaches the tab, whatever upstream adds
// to the method later (the drift gate flags that on the roll as well).
void KeepOnly(base::DictValue& params,
              base::span<const std::string_view> known) {
  std::vector<std::string> remove;
  for (const auto [key, value] : params) {
    if (std::ranges::find(known, key) == known.end()) {
      remove.push_back(key);
    }
  }
  for (const std::string& key : remove) {
    params.Remove(key);
  }
}

// Input.dispatchKeyEvent without `commands`.
constexpr std::array<std::string_view, 14> kKeyEventParams = {
    "type",
    "modifiers",
    "timestamp",
    "text",
    "unmodifiedText",
    "keyIdentifier",
    "code",
    "key",
    "windowsVirtualKeyCode",
    "nativeVirtualKeyCode",
    "autoRepeat",
    "isKeypad",
    "isSystemKey",
    "location",
};

constexpr std::array<std::string_view, 3> kIsolatedWorldParams = {
    "frameId",
    "worldName",
    "contentSecurityPolicy",
};

constexpr std::array<std::string_view, 2> kNetworkEnableParams = {
    "maxTotalBufferSize",
    "maxResourceBufferSize",
};

// The most a screenshot clip may ask the browser to allocate: the bitmap is
// width * scale by height * scale pixels. Bounded well under what a page can
// legitimately need, far under what upstream would try (it only refuses a zero
// side).
constexpr double kMaxClipOrigin = 1e6;
constexpr double kMaxClipSide = 16384;
constexpr double kMaxClipScale = 4;
constexpr double kMaxClipPixels = 64e6;

// A finite number in [min, max] (or (min, max] if `exclusive_min`), or nothing.
std::optional<double> BoundedNumber(const base::DictValue& dict,
                                    std::string_view key,
                                    double min,
                                    double max,
                                    bool exclusive_min) {
  const std::optional<double> number = dict.FindDouble(key);
  if (!number || !std::isfinite(*number) || *number > max ||
      (exclusive_min ? *number <= min : *number < min)) {
    return std::nullopt;
  }
  return number;
}

// Whether `clip` is an object of finite numbers within the limits, with an
// area the browser may allocate.
bool IsValidClip(const base::Value& clip) {
  const base::DictValue* dict = clip.GetIfDict();
  if (!dict) {
    return false;
  }
  const auto x = BoundedNumber(*dict, "x", 0, kMaxClipOrigin, false);
  const auto y = BoundedNumber(*dict, "y", 0, kMaxClipOrigin, false);
  const auto width = BoundedNumber(*dict, "width", 0, kMaxClipSide, true);
  const auto height = BoundedNumber(*dict, "height", 0, kMaxClipSide, true);
  const auto scale = BoundedNumber(*dict, "scale", 0, kMaxClipScale, true);
  return x && y && width && height && scale &&
         *width * *height * *scale * *scale <= kMaxClipPixels;
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
  if (const base::Value* format = params.Find("format")) {
    const std::string* name = format->GetIfString();
    if (!name || (*name != "png" && *name != "jpeg" && *name != "webp")) {
      return Error("Koan.invalidParams", "format must be png, jpeg or webp");
    }
  }
  if (const base::Value* quality = params.Find("quality")) {
    if (!quality->is_int() || quality->GetInt() < 0 ||
        quality->GetInt() > 100) {
      return Error("Koan.invalidParams", "quality must be an integer 0 to 100");
    }
  }
  const base::Value* clip = params.Find("clip");
  const base::Value* beyond = params.Find("captureBeyondViewport");
  if (beyond && !beyond->is_bool()) {
    return Error("Koan.invalidParams",
                 "captureBeyondViewport must be a boolean");
  }
  const bool wants_beyond = beyond && beyond->GetBool();
  if (!clip && !wants_beyond) {
    return std::nullopt;  // a plain viewport screenshot
  }
  // clip and captureBeyondViewport resize the real view, which would flicker
  // the user's own tab; on an agent tab they are bounded.
  if (!context.tab_is_agent_owned) {
    return Error("Koan.notPermitted",
                 "clip and captureBeyondViewport are only allowed on tabs "
                 "the agent owns");
  }
  if (clip && !IsValidClip(*clip)) {
    return Error("Koan.invalidParams",
                 "clip must be {x, y, width, height, scale} of finite "
                 "numbers: x and y in [0, 1e6], width and height in "
                 "(0, 16384], scale in (0, 4], at most 64 million pixels");
  }
  if (wants_beyond && !clip) {
    // Without a clip the whole page is rendered, up to 128K pixels a side.
    return Error("Koan.notPermitted",
                 "captureBeyondViewport needs a clip that bounds the capture");
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
      // `commands` is dropped with every parameter this filter's author did
      // not review.
      KeepOnly(params, kKeyEventParams);
      return std::nullopt;
    case ParamFilter::kIsolatedWorld:
      // A world with universal access bypasses the same-origin policy.
      KeepOnly(params, kIsolatedWorldParams);
      params.Set("grantUniveralAccess", false);
      return std::nullopt;
    case ParamFilter::kScreenshot:
      return FilterScreenshot(params, context);
    case ParamFilter::kNetworkEnable:
      // Bodies are never inlined into requestWillBeSent, and the buffers are
      // bounded whatever the agent asked for.
      KeepOnly(params, kNetworkEnableParams);
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
