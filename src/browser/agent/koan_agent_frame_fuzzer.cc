// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Drives FrameDecoder with input-chosen reads, takes, authentication and
// limits, and checks what the gateway relies on: the unread bytes stay
// bounded, every payload is between one byte and the limit, a failed decoder
// stays failed and holds nothing, and a decoder that has not authenticated
// yields only a few frames.

#include <stddef.h>
#include <stdint.h>

#include <fuzzer/FuzzedDataProvider.h>

#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "koan/browser/agent/koan_agent_frame.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using koan::agent::FrameDecoder;

  FuzzedDataProvider provider(data, size);
  // Either the gateway's decoder (unauthenticated) or one with an explicit
  // limit (authenticated from the start).
  const bool explicit_limit = provider.ConsumeBool();
  FrameDecoder decoder = explicit_limit
                             ? FrameDecoder(provider.ConsumeIntegral<size_t>())
                             : FrameDecoder();
  int frames_before_auth = 0;
  bool authenticated = explicit_limit;
  bool failed = false;

  while (provider.remaining_bytes() > 0) {
    switch (provider.ConsumeIntegralInRange<int>(0, 3)) {
      case 0: {
        const std::vector<uint8_t> chunk = provider.ConsumeBytes<uint8_t>(
            provider.ConsumeIntegralInRange<size_t>(0, 70000));
        const bool ok = decoder.Append(chunk);
        CHECK(!failed || !ok);
        break;
      }
      case 1: {
        if (auto frame = decoder.TakeFrame()) {
          CHECK(!frame->empty());
          CHECK_LE(frame->size(), decoder.max_payload());
          if (!authenticated) {
            ++frames_before_auth;
            CHECK_LE(frames_before_auth, koan::agent::kMaxPreAuthFrames);
          }
        }
        break;
      }
      case 2: {
        const bool ok = decoder.OnAuthenticated();
        CHECK(!failed || !ok);
        if (ok) {
          authenticated = true;
        }
        break;
      }
      default:
        CHECK(!(decoder.has_partial_frame() && decoder.failed()));
        break;
    }
    failed = failed || decoder.failed();
    CHECK_EQ(failed, decoder.failed());
    CHECK_LE(decoder.max_payload(), koan::agent::kMaxPayload);
    CHECK_LE(decoder.buffered_bytes(),
             2 * (koan::agent::kFrameHeaderSize + decoder.max_payload()));
    if (failed) {
      CHECK_EQ(decoder.buffered_bytes(), 0u);
    }
  }
  return 0;
}
