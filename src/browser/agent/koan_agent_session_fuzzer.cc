// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Drives a Session with arbitrary bytes, prompt answers, clock jumps and
// revocations, next to a client that holds a real key and signs (sometimes
// correctly, sometimes over a changed nonce), and checks what is checked here,
// and no more:
//   - the session is authenticated only as the paired client, and only after
//     the fuzzer has sent at least one correct signature of a nonce while the
//     session was waiting for a proof (a counter, not a match of that
//     signature to this nonce: the unit tests do the matching);
//   - a prompt about a changed program or chain is never requested before such
//     a signature has been sent (a prompt for a new client is not held to it);
//   - the paired client's pin changes only after some approval was given while
//     the peer was unchanged (any such approval, not the matching one; the
//     driver flips the peer's identity, see peer_unchanged);
//   - nothing is sent after the session was closed, it never closes twice, and
//     the session reports closed once the delegate was told;
//   - an authenticated session ends authenticated or closed.
// It does not check what an authenticated session answers (the unit tests
// check that nothing is forwarded), nor limits, timeouts or replies.

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "base/base64.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/browser/agent/koan_agent_session.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace {

using koan::agent::ApprovalDecision;
using koan::agent::ApprovalRequest;
using koan::agent::CloseReason;
using koan::agent::Session;

constexpr char kClientId[] = "0123456789abcdef0123456789abcdef";

struct Key {
  explicit Key(uint8_t seed_byte) {
    std::array<uint8_t, 32> seed;
    seed.fill(seed_byte);
    ED25519_keypair_from_seed(public_key.data(), private_key.data(),
                              seed.data());
  }

  std::vector<uint8_t> Sign(const std::vector<uint8_t>& message) const {
    std::array<uint8_t, koan::agent::kSignatureSize> signature;
    CHECK_EQ(ED25519_sign(signature.data(), message.data(), message.size(),
                          private_key.data()),
             1);
    return std::vector<uint8_t>(signature.begin(), signature.end());
  }

  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> public_key;
  std::array<uint8_t, ED25519_PRIVATE_KEY_LEN> private_key;
};

class FuzzDelegate final : public koan::agent::SessionDelegate {
 public:
  void Send(std::vector<uint8_t> frame) override {
    CHECK(!closed);
    CHECK(!frame.empty());
    // Remember the nonce a reply carries, to be able to sign it.
    koan::agent::FrameDecoder decoder;
    if (!decoder.Append(frame)) {
      return;
    }
    std::optional<std::vector<uint8_t>> payload = decoder.TakeFrame();
    if (!payload) {
      return;
    }
    std::optional<base::DictValue> message = base::JSONReader::ReadDict(
        std::string_view(reinterpret_cast<const char*>(payload->data()),
                         payload->size()),
        base::JSON_PARSE_RFC);
    if (!message) {
      return;
    }
    if (const base::DictValue* result = message->FindDict("result")) {
      if (const std::string* nonce = result->FindString("nonce")) {
        if (auto bytes = base::Base64Decode(*nonce)) {
          last_nonce = std::move(*bytes);
        }
      }
    }
  }
  void Close(CloseReason) override {
    CHECK(!closed);
    closed = true;
  }
  void RequestApproval(const ApprovalRequest& request) override {
    CHECK(!closed);
    pending.push_back(request.id);
    // A question about a changed program or chain only follows a correct
    // signature (the count is of signatures sent, see the header).
    if (request.kind != koan::agent::ApprovalKind::kNewClient) {
      CHECK_GT(*valid_proofs, 0);
    }
  }

  bool PeerUnchanged() override { return peer_unchanged; }

  bool closed = false;
  // What the second look at the peer finds; the driver flips it.
  bool peer_unchanged = true;
  std::vector<uint64_t> pending;
  std::vector<uint8_t> last_nonce;
  // Counts of correct signatures the fuzzer sent; set by the driver.
  raw_ptr<const int> valid_proofs;
};

// A label built from pieces that include the ones the label check exists for.
std::string FuzzLabel(FuzzedDataProvider* provider) {
  static constexpr std::array<const char*, 14> kPieces = {
      "a",
      "Claude",
      " ",
      "  ",
      "\t",
      "\xC2\x85",
      "\xE2\x80\xAE",
      "\xE2\x81\xA6",
      "\xE2\x80\x8B",
      "\xE2\x80\xA8",
      "\xC3\xA9",
      "\xE2\x98\x95",
      "\xC2\xA0",
      "../",
  };
  std::string label;
  const size_t count = provider->ConsumeIntegralInRange<size_t>(0, 24);
  for (size_t i = 0; i < count; ++i) {
    label += provider->PickValueInArray(kPieces);
  }
  return label;
}

// A string of ASCII (any, controls included): always valid UTF-8.
std::string FuzzAscii(FuzzedDataProvider* provider, size_t max_length) {
  std::string text = provider->ConsumeRandomLengthString(max_length);
  for (char& c : text) {
    c = static_cast<char>(c & 0x7f);
  }
  return text;
}

void FillFrom(FuzzedDataProvider* provider, base::span<uint8_t> out) {
  for (uint8_t& byte : out) {
    byte = provider->ConsumeIntegral<uint8_t>();
  }
}

std::vector<uint8_t> JsonFrame(const base::DictValue& message) {
  const std::optional<std::string> json = base::WriteJson(message);
  CHECK(json);
  return *koan::agent::EncodeFrame(base::as_byte_span(*json));
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

  // A paired client, with a key this process holds, and a second key for
  // pairing another.
  const Key paired(1);
  const Key newcomer(2);

  koan::agent::PeerIdentity peer;
  peer.uid = 501;
  peer.process = {"/x/koan", "TEAM"};
  peer.ancestors = {{"/bin/zsh", "apple"}, {"/x/agent", "TEAM"}};
  // The fuzzer chooses whether the peer is where the client paired from.
  koan::agent::PeerIdentity pinned_at = peer;
  if (provider.ConsumeBool()) {
    pinned_at.process.path = "/y/koan";
  }
  if (provider.ConsumeBool()) {
    pinned_at.ancestors[1] = {"/y/agent", "TEAM"};
  }
  if (provider.ConsumeIntegralInRange<int>(0, 7) == 0) {
    peer.process.path.clear();  // an unnamed peer, now and then
  }
  if (provider.ConsumeBool()) {
    pinned_at.ancestors = {{"/bin/zsh", "apple"}};  // an unknown chain
  }

  koan::agent::ClientRecord record;
  record.id = kClientId;
  record.label = "fuzz";
  record.public_key.assign(paired.public_key.begin(), paired.public_key.end());
  record.pinned = koan::agent::PinPeer(pinned_at);
  record.created = record.last_used = wall.Now();
  CHECK(store.Add(record));

  koan::agent::SessionEnvironment env;
  env.browser_uid = 501;
  env.store = &store;
  env.limiter = &limiter;
  env.accepted_chains = &chains;
  env.tick_clock = &ticks;
  env.clock = &wall;
  env.random_bytes =
      base::BindRepeating(&FillFrom, base::Unretained(&provider));

  int valid_proofs = 0;
  FuzzDelegate delegate;
  delegate.valid_proofs = &valid_proofs;
  Session session(peer, env, &delegate);
  bool was_authenticated = false;

  const koan::agent::PinnedIdentity original_pin = record.pinned;
  int approvals_granted = 0;

  while (provider.remaining_bytes() > 0) {
    switch (provider.ConsumeIntegralInRange<int>(0, 9)) {
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
          if (decision.approved && delegate.peer_unchanged) {
            ++approvals_granted;
          }
          session.OnApprovalDecision(delegate.pending[index], decision);
        }
        break;
      case 2:
        ticks.Advance(base::Seconds(provider.ConsumeIntegralInRange(0, 90)));
        session.CheckTimeouts();
        break;
      case 3:
        wall.Advance(base::Days(provider.ConsumeIntegralInRange(0, 40)));
        break;
      case 4: {
        // Ask for a nonce for the paired client (or a random id).
        base::DictValue params;
        params.Set("clientId", provider.ConsumeBool()
                                   ? std::string(kClientId)
                                   : FuzzAscii(&provider, 40));
        base::DictValue message;
        message.Set("id", 1);
        message.Set("method", "Koan.authenticate");
        message.Set("params", std::move(params));
        session.OnBytesReceived(JsonFrame(message));
        break;
      }
      case 5: {
        // Answer the nonce the session gave, with the paired client's key if
        // the fuzzer decides to sign it correctly and a wrong message if not.
        std::vector<uint8_t> nonce = delegate.last_nonce;
        const bool correct = provider.ConsumeBool();
        if (!correct && !nonce.empty()) {
          nonce[0] ^= 1;
        }
        const std::vector<uint8_t> signature =
            paired.Sign(koan::agent::ProofMessage(kClientId, nonce));
        base::DictValue params;
        params.Set("clientId", kClientId);
        params.Set("signature", base::Base64Encode(signature));
        base::DictValue message;
        message.Set("id", 2);
        message.Set("method", "Koan.authenticate");
        message.Set("params", std::move(params));
        if (correct && !nonce.empty() &&
            session.state() == Session::State::kAwaitingProof) {
          ++valid_proofs;
        }
        // A nonce is for one attempt: the next signature is over nothing.
        delegate.last_nonce.clear();
        session.OnBytesReceived(JsonFrame(message));
        break;
      }
      case 6: {
        // Pair another client, with a proof of holding its key (or without).
        const std::string label = FuzzLabel(&provider);
        const bool proves = provider.ConsumeBool();
        base::DictValue params;
        params.Set("publicKey", base::Base64Encode(newcomer.public_key));
        params.Set("label", label);
        params.Set("capabilities", base::ListValue().Append("observe"));
        std::vector<uint8_t> proof(koan::agent::kSignatureSize, 0);
        if (proves) {
          proof = newcomer.Sign(
              koan::agent::PairingMessage(newcomer.public_key, label));
        }
        params.Set("proof", base::Base64Encode(proof));
        base::DictValue message;
        message.Set("id", 3);
        message.Set("method", "Koan.pair");
        message.Set("params", std::move(params));
        session.OnBytesReceived(JsonFrame(message));
        break;
      }
      case 7:
        if (provider.ConsumeBool()) {
          session.OnClientRevoked(kClientId);
        } else if (store.Find(kClientId)) {
          // Revoke it in the store without telling the session.
          store.Remove(kClientId);
        }
        break;
      case 9:
        // The peer becomes another program (or is itself again).
        delegate.peer_unchanged = provider.ConsumeBool();
        break;
      default:
        if (session.state() == Session::State::kAuthenticated) {
          base::DictValue message;
          message.Set("id", 4);
          message.Set("method",
                      provider.ConsumeBool() ? "Koan.status" : "Page.navigate");
          session.OnBytesReceived(JsonFrame(message));
        }
        break;
    }

    if (session.state() == Session::State::kAuthenticated) {
      was_authenticated = true;
      // Only the paired client gets here, and only after a correct signature
      // was sent (a client paired through the fuzzed prompt is another id, and
      // its key never signs a login, so it cannot authenticate).
      CHECK_EQ(session.client_id(), std::string(kClientId));
      CHECK_GT(valid_proofs, 0);
    }
    if (delegate.closed) {
      CHECK_EQ(session.state(), Session::State::kClosed);
    }
    // The pin of the paired client only changes with an approval.
    if (const auto stored = store.Find(kClientId)) {
      if (!(stored->pinned == original_pin)) {
        CHECK_GT(approvals_granted, 0);
      }
    }
  }
  // An authenticated session ends authenticated or closed.
  if (was_authenticated) {
    CHECK(session.state() == Session::State::kAuthenticated ||
          session.state() == Session::State::kClosed);
  }
  return 0;
}
