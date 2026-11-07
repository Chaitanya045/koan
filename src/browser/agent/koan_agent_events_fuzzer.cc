// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The oracle below restates the scrubber's name rules, so this finds traversal
// and depth bugs (something missed, something crashed), not a wrong rule; the
// rules themselves are checked against the protocol by
// scripts/agent/check_policy_coverage.py.
//
// Feeds arbitrary JSON to the event filter as the params of each listed
// Network event and checks the scrubber's promise: whatever the shape, what is
// delivered has no entry whose name mentions cookies or a request body, no raw
// header text, and no credential header in any object named *headers.

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_events.h"

namespace {

bool IsSecretHeader(std::string_view name) {
  const std::string lower = base::ToLowerASCII(name);
  return lower == "cookie" || lower == "cookie2" || lower == "set-cookie" ||
         lower == "set-cookie2" || lower == "authorization" ||
         lower == "proxy-authorization";
}

void CheckClean(const base::Value& value, bool is_headers) {
  if (const base::DictValue* dict = value.GetIfDict()) {
    for (const auto [key, child] : *dict) {
      const std::string lower = base::ToLowerASCII(key);
      if (is_headers) {
        CHECK(!IsSecretHeader(key));
      } else {
        CHECK(lower.find("cookie") == std::string::npos);
        CHECK(lower.find("postdata") == std::string::npos);
        CHECK(!lower.ends_with("headerstext"));
      }
      CheckClean(child, lower.ends_with("headers"));
    }
  } else if (const base::ListValue* list = value.GetIfList()) {
    for (const base::Value& element : *list) {
      CheckClean(element, /*is_headers=*/false);
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
  for (const koan::agent::EventRule& rule : koan::agent::EventRules()) {
    if (rule.action != koan::agent::EventAction::kScrubNetwork) {
      continue;
    }
    base::DictValue params = parsed->Clone();
    if (koan::agent::FilterEvent(rule.method, params) ==
        koan::agent::EventVerdict::kDeliver) {
      CheckClean(base::Value(std::move(params)), /*is_headers=*/false);
    }
  }
  return 0;
}
