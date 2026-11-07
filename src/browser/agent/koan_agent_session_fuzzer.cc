// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Drives a Session with arbitrary bytes, prompt answers and clock jumps, with
// a paired client whose key the fuzzer does not hold, and checks what must hold
// whatever arrives: the session never becomes authenticated without a valid
// proof (which random bytes cannot produce), never sends after it was closed,
// never closes twice, and never forwards anything.

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_session.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace {

using koan::agent::ApprovalDecision;
using koan::agent::ApprovalRequest;
using koan::agent::CloseReason;
using koan::agent::Session;

class FuzzDelegate final : public koan::agent::SessionDelegate {
 public:
  void Send(std::vector<uint8_t> frame) override {
    CHECK(!closed);
    CHECK(!frame.empty());
  }
  void Close(CloseReason) override {
    CHECK(!closed);
    closed = true;
  }
  void RequestApproval(const ApprovalRequest& request) override {
    CHECK(!closed);
    pending.push_back(request.id);
  }

  bool closed = false;
  std::vector<uint64_t> pending;
};

void FillFrom(FuzzedDataProvider* provider, base::span<uint8_t> out) {
  for (uint8_t& byte : out) {
    byte = provider->ConsumeIntegral<uint8_t>();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  koan::agent::InMemoryClientStore store;
  koan::agent::RateLimiter limiter;
  std::set<std::string> chains;
  base::SimpleTestTickClock ticks;
  base::SimpleTestClock wall;
  ticks.SetNowTicks(base::TimeTicks() + base::Seconds(1000));
  wall.SetNow(base::Time::FromSecondsSinceUnixEpoch(1700000000));

  // A paired client, with a key nobody in this process holds the private half
  // of.
  std::array<uint8_t, 32> seed = {1};
  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> public_key;
  std::array<uint8_t, ED25519_PRIVATE_KEY_LEN> private_key;
  ED25519_keypair_from_seed(public_key.data(), private_key.data(), seed.data());
  koan::agent::ClientRecord record;
  record.id = "0123456789abcdef0123456789abcdef";
  record.label = "fuzz";
  record.public_key.assign(public_key.begin(), public_key.end());
  record.created = record.last_used = wall.Now();
  store.Add(record);

  koan::agent::PeerIdentity peer;
  peer.process = {"/x/koan", "TEAM"};
  peer.ancestors = {{"/bin/zsh", "apple"}, {"/x/agent", "TEAM"}};

  koan::agent::SessionEnvironment env;
  env.store = &store;
  env.limiter = &limiter;
  env.accepted_chains = &chains;
  env.tick_clock = &ticks;
  env.clock = &wall;
  env.random_bytes =
      base::BindRepeating(&FillFrom, base::Unretained(&provider));

  FuzzDelegate delegate;
  Session session(peer, env, &delegate);

  while (provider.remaining_bytes() > 0) {
    switch (provider.ConsumeIntegralInRange<int>(0, 3)) {
      case 0: {
        const std::vector<uint8_t> chunk = provider.ConsumeBytes<uint8_t>(
            provider.ConsumeIntegralInRange<size_t>(0, 300));
        session.OnBytesReceived(chunk);
        break;
      }
      case 1:
        if (!delegate.pending.empty()) {
          ApprovalDecision decision;
          decision.approved = provider.ConsumeBool();
          decision.granted.observe = provider.ConsumeBool();
          decision.granted.drive = provider.ConsumeBool();
          const size_t index = provider.ConsumeIntegralInRange<size_t>(
              0, delegate.pending.size() - 1);
          session.OnApprovalDecision(delegate.pending[index], decision);
        }
        break;
      case 2:
        ticks.Advance(base::Seconds(provider.ConsumeIntegralInRange(0, 90)));
        session.CheckTimeouts();
        break;
      default:
        wall.Advance(base::Days(provider.ConsumeIntegralInRange(0, 40)));
        break;
    }
    // A client paired through the fuzzed prompt is a different client with a
    // key the fuzzer chose, so authentication is only impossible for ours.
    if (session.state() == Session::State::kAuthenticated) {
      CHECK_NE(session.client_id(), record.id);
    }
    if (delegate.closed) {
      CHECK_EQ(session.state(), Session::State::kClosed);
    }
  }
  return 0;
}
