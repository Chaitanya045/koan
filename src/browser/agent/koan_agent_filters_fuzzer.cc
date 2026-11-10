// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Feeds arbitrary JSON to every parameter filter, with both ownership values
// and a registry holding one stream, and checks the post-conditions that make a
// forwarded command safe. The oracle is written out here from the design
// (sections 4.3 and 4.4), not from the filters' code: it states what must be
// true of the params when a filter says "forward", whatever they were.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/check_op.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_contexts.h"
#include "koan/browser/agent/koan_agent_filters.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "url/gurl.h"

namespace {

using koan::agent::ParamFilter;

bool IsInt(const base::DictValue& d, std::string_view key, int lo, int hi) {
  const base::Value* v = d.Find(key);
  return v && v->is_int() && v->GetInt() >= lo && v->GetInt() <= hi;
}

// A parameter that is not on the list reviewed for the method must be gone.
void CheckOnlyKnownKeys(const base::DictValue& params,
                        std::initializer_list<std::string_view> known) {
  for (const auto [key, value] : params) {
    CHECK(std::ranges::find(known, key) != known.end()) << key;
  }
}

void CheckClipWithinLimits(const base::DictValue& params) {
  const base::DictValue* clip = params.FindDict("clip");
  CHECK(clip);
  const double x = clip->FindDouble("x").value();
  const double y = clip->FindDouble("y").value();
  const double w = clip->FindDouble("width").value();
  const double h = clip->FindDouble("height").value();
  const double scale = clip->FindDouble("scale").value();
  for (double v : {x, y, w, h, scale}) {
    CHECK(std::isfinite(v));
  }
  CHECK(x >= 0 && x <= 1e6 && y >= 0 && y <= 1e6);
  CHECK(w > 0 && w <= 16384 && h > 0 && h <= 16384);
  CHECK(scale > 0 && scale <= 4);
  CHECK(w * h * scale * scale <= 64e6);
}

void CheckForwardable(ParamFilter filter,
                      const base::DictValue& params,
                      bool owned) {
  switch (filter) {
    case ParamFilter::kNone:
      break;
    case ParamFilter::kNavigationUrl: {
      const std::string* url = params.FindString("url");
      CHECK(url);
      const GURL parsed(*url);
      CHECK(parsed.is_valid());
      CHECK(parsed.SchemeIsHTTPOrHTTPS() || parsed.IsAboutBlank());
      CHECK(!parsed.SchemeIs("javascript") && !parsed.SchemeIs("file"));
      break;
    }
    case ParamFilter::kStripKeyCommands:
      CHECK(!params.contains("commands"));
      CheckOnlyKnownKeys(
          params, {"type", "modifiers", "timestamp", "text", "unmodifiedText",
                   "keyIdentifier", "code", "key", "windowsVirtualKeyCode",
                   "nativeVirtualKeyCode", "autoRepeat", "isKeypad",
                   "isSystemKey", "location"});
      break;
    case ParamFilter::kIsolatedWorld:
      CHECK(params.FindBool("grantUniveralAccess") == false);
      CHECK(!params.contains("grantUniversalAccess"));
      CheckOnlyKnownKeys(params,
                         {"frameId", "worldName", "contentSecurityPolicy",
                          "grantUniveralAccess"});
      break;
    case ParamFilter::kScreenshot: {
      const base::Value* from_surface = params.Find("fromSurface");
      CHECK(!from_surface ||
            (from_surface->is_bool() && from_surface->GetBool()));
      const base::Value* format = params.Find("format");
      CHECK(!format ||
            (format->is_string() &&
             (format->GetString() == "png" || format->GetString() == "jpeg" ||
              format->GetString() == "webp")));
      if (params.contains("quality")) {
        const std::optional<double> quality = params.FindDouble("quality");
        CHECK(quality);
        CHECK(*quality >= 0 && *quality <= 100 &&
              *quality == std::floor(*quality));
      }
      const base::Value* beyond = params.Find("captureBeyondViewport");
      CHECK(!beyond || beyond->is_bool());
      const bool wants_beyond = beyond && beyond->GetBool();
      if (!owned) {
        CHECK(!params.contains("clip"));
        CHECK(!wants_beyond);
      }
      if (params.contains("clip")) {
        CheckClipWithinLimits(params);
      }
      if (wants_beyond) {
        CHECK(params.contains("clip"));
      }
      break;
    }
    case ParamFilter::kNetworkEnable:
      CHECK(!params.contains("maxPostDataSize"));
      CHECK(!params.contains("reportDirectSocketTraffic"));
      CHECK(!params.contains("enableDurableMessages"));
      CheckOnlyKnownKeys(params,
                         {"maxTotalBufferSize", "maxResourceBufferSize"});
      CHECK(IsInt(params, "maxTotalBufferSize", 0,
                  koan::agent::kMaxNetworkTotalBufferSize));
      CHECK(IsInt(params, "maxResourceBufferSize", 0,
                  koan::agent::kMaxNetworkResourceBufferSize));
      break;
    case ParamFilter::kOwnStream: {
      const std::string* handle = params.FindString("handle");
      CHECK(handle);
      CHECK_EQ(*handle, "h1");  // the one stream in the registry
      break;
    }
    case ParamFilter::kOwnTabOnly:
      CHECK(owned);
      break;
    case ParamFilter::kRuntimeContext: {
      // The tracker allows context 1 ("u1") only.
      CHECK(!params.contains("executionContextName"));
      for (std::string_view key : {"contextId", "executionContextId"}) {
        const base::Value* id = params.Find(key);
        CHECK(!id || (id->is_int() && id->GetInt() == 1));
      }
      const base::Value* unique = params.Find("uniqueContextId");
      CHECK(!unique || (unique->is_string() && unique->GetString() == "u1"));
      break;
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view json(reinterpret_cast<const char*>(data), size);
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return 0;
  }
  koan::agent::StreamRegistry streams;
  CHECK(streams.Add("h1"));
  koan::agent::ContextTracker contexts;
  contexts.OnEvent("Runtime.executionContextCreated",
                   base::JSONReader::ReadDict(
                       R"({"context":{"id":1,"uniqueId":"u1","auxData":)"
                       R"({"isDefault":true,"type":"default"}}})",
                       base::JSON_PARSE_RFC)
                       .value());
  for (ParamFilter filter :
       {ParamFilter::kNone, ParamFilter::kNavigationUrl,
        ParamFilter::kStripKeyCommands, ParamFilter::kIsolatedWorld,
        ParamFilter::kRuntimeContext, ParamFilter::kScreenshot,
        ParamFilter::kNetworkEnable, ParamFilter::kOwnStream,
        ParamFilter::kOwnTabOnly}) {
    for (bool owned : {false, true}) {
      base::DictValue params = parsed->Clone();
      koan::agent::FilterContext context;
      context.tab_is_agent_owned = owned;
      context.streams = &streams;
      context.contexts = &contexts;
      if (!koan::agent::ApplyParamFilter(filter, params, context)) {
        CheckForwardable(filter, params, owned);
      }
    }
  }
  return 0;
}
