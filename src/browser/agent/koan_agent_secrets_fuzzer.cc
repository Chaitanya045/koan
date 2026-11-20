// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Feeds arbitrary text, and a secret taken from its start, to the egress
// scrubber and checks its promise: no form of the secret it looks for is left,
// and valid JSON stays valid JSON.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/strings/string_view_util.h"
#include "koan/browser/agent/koan_agent_secrets.h"
#include "testing/libfuzzer/libfuzzer_base_wrappers.h"

DEFINE_LLVM_FUZZER_TEST_ONE_INPUT_SPAN(base::span<const uint8_t> data) {
  if (data.size() < 2) {
    return 0;
  }
  const size_t secret_size = data.front() % 24;
  if (data.size() < 1 + secret_size) {
    return 0;
  }
  const std::string secret(base::as_string_view(data.subspan(1u, secret_size)));
  std::string text(base::as_string_view(data.subspan(1u + secret_size)));
  // A secret with a bracket could make a new occurrence out of the mark.
  if (secret.find_first_of("[]") != std::string::npos) {
    return 0;
  }
  koan::agent::SecretScrubber scrubber;
  if (!scrubber.Add(secret)) {
    return 0;
  }
  const bool was_json =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC).has_value();
  scrubber.Scrub(text);
  for (const std::string& needle : koan::agent::SecretNeedles(secret)) {
    CHECK_EQ(text.find(needle), std::string::npos);
  }
  if (was_json) {
    CHECK(base::JSONReader::Read(text, base::JSON_PARSE_RFC).has_value());
  }
  return 0;
}
