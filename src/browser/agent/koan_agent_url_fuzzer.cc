// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Feeds arbitrary strings to the navigation filter and checks its promise: a
// URL is allowed only if GURL says it is http, https or about:blank, and the
// scheme named in a refusal is bounded.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/check.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "url/gurl.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string input(reinterpret_cast<const char*>(data), size);
  const GURL url(input);
  if (koan::agent::IsAllowedNavigationUrl(url)) {
    CHECK(url.is_valid());
    CHECK(url.SchemeIsHTTPOrHTTPS() || url.IsAboutBlank());
    CHECK(!url.SchemeIs("javascript"));
    CHECK(!url.SchemeIs("file"));
  } else {
    CHECK_LE(koan::agent::DisallowedNavigationScheme(url).size(), 35u);
  }
  return 0;
}
