// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_session.h"

#include <stdint.h>

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace koan::agent {

namespace {

ProcessInfo Process(std::string path, std::string identity = "TEAM1") {
  return ProcessInfo{std::move(path), std::move(identity)};
}

PeerIdentity Peer() {
  PeerIdentity peer;
  peer.uid = 501;
  peer.process = Process("/Applications/Koan.app/Contents/Resources/bin/koan");
  peer.ancestors = {Process("/bin/zsh", "apple"),
                    Process("/usr/local/bin/claude", "ANTHROPIC")};
  return peer;
}

// Deterministic "random" bytes: a counter, so nonces and ids differ.
class Counter {
 public:
  void Fill(base::span<uint8_t> out) {
    for (uint8_t& byte : out) {
      byte = static_cast<uint8_t>(++next_);
    }
  }

 private:
  int next_ = 0;
};

class FakeDelegate final : public SessionDelegate {
 public:
  void Send(std::vector<uint8_t> frame) override {
    ASSERT_TRUE(decoder_.Append(frame));
    while (auto payload = decoder_.TakeFrame()) {
      const std::string text(payload->begin(), payload->end());
      replies.push_back(
          *base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC));
    }
  }
  void Close(CloseReason reason) override { closed = reason; }
  void RequestApproval(const ApprovalRequest& request) override {
    approvals.push_back(request);
  }

  const base::DictValue& Last() const { return replies.back(); }
  // The Koan error code of the last reply, or "".
  std::string LastError() const {
    const base::DictValue* error = Last().FindDict("error");
    return error ? *error->FindString("data") : std::string();
  }
  const base::DictValue& LastResult() const {
    return *Last().FindDict("result");
  }

  std::vector<base::DictValue> replies;
  std::vector<ApprovalRequest> approvals;
  std::optional<CloseReason> closed;

 private:
  FrameDecoder decoder_{kMaxPayload};
};

struct Client {
  Client() { ED25519_keypair(public_key.data(), private_key.data()); }

  std::string PublicKeyBase64() const { return base::Base64Encode(public_key); }
  std::string Sign(std::string_view client_id,
                   const std::vector<uint8_t>& nonce) const {
    const std::vector<uint8_t> message = ProofMessage(client_id, nonce);
    std::array<uint8_t, kSignatureSize> signature;
    EXPECT_EQ(ED25519_sign(signature.data(), message.data(), message.size(),
                           private_key.data()),
              1);
    return base::Base64Encode(signature);
  }

  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> public_key;
  std::array<uint8_t, ED25519_PRIVATE_KEY_LEN> private_key;
};

std::vector<uint8_t> Frame(const std::string& json) {
  return *EncodeFrame(base::as_byte_span(json));
}

class KoanAgentSessionTest : public testing::Test {
 protected:
  KoanAgentSessionTest() {
    ticks_.SetNowTicks(base::TimeTicks() + base::Seconds(1000));
    wall_.SetNow(base::Time::FromSecondsSinceUnixEpoch(1700000000));
  }

  SessionEnvironment Env() {
    SessionEnvironment env;
    env.store = &store_;
    env.limiter = &limiter_;
    env.accepted_chains = &chains_;
    env.tick_clock = &ticks_;
    env.clock = &wall_;
    env.random_bytes =
        base::BindRepeating(&Counter::Fill, base::Unretained(&counter_));
    return env;
  }

  std::unique_ptr<Session> NewSession(PeerIdentity peer = Peer()) {
    delegates_.push_back(std::make_unique<FakeDelegate>());
    return std::make_unique<Session>(std::move(peer), Env(),
                                     delegates_.back().get());
  }
  FakeDelegate& delegate() { return *delegates_.back(); }

  void Send(Session& session, const std::string& json) {
    session.OnBytesReceived(Frame(json));
  }

  // Pairs `client` and returns its id, approving with `granted`.
  std::string Pair(const Client& client,
                   const std::string& capabilities = "[]",
                   ClientCapabilities granted = ClientCapabilities()) {
    auto session = NewSession();
    Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                       client.PublicKeyBase64() +
                       R"(","label":"Claude Code",)" + R"("capabilities":)" +
                       capabilities + "}}");
    EXPECT_EQ(delegate().approvals.size(), 1u);
    ApprovalDecision decision;
    decision.approved = true;
    decision.granted = granted;
    session->OnApprovalDecision(delegate().approvals[0].id, decision);
    return *delegate().LastResult().FindString("clientId");
  }

  // Runs the handshake on `session`; returns whether it authenticated.
  bool Authenticate(Session& session,
                    const Client& client,
                    const std::string& client_id) {
    Send(session, R"({"id":10,"method":"Koan.authenticate","params":)"
                  R"({"clientId":")" +
                      client_id + R"("}})");
    if (session.state() != Session::State::kAwaitingProof) {
      return false;
    }
    const std::vector<uint8_t> nonce =
        *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
    Send(session, R"({"id":11,"method":"Koan.authenticate","params":)"
                  R"({"clientId":")" +
                      client_id + R"(","signature":")" +
                      client.Sign(client_id, nonce) + R"("}})");
    return session.state() == Session::State::kAuthenticated;
  }

  base::SimpleTestTickClock ticks_;
  base::SimpleTestClock wall_;
  InMemoryClientStore store_;
  RateLimiter limiter_;
  std::set<std::string> chains_;
  Counter counter_;
  std::vector<std::unique_ptr<FakeDelegate>> delegates_;
};

}  // namespace

TEST_F(KoanAgentSessionTest, PairingAsksTheUserAboutTheVerifiedPeer) {
  Client client;
  auto session = NewSession();
  Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                     client.PublicKeyBase64() +
                     R"(","label":"Claude Code","capabilities":["observe"]}})");
  EXPECT_EQ(session->state(), Session::State::kAwaitingApproval);
  ASSERT_EQ(delegate().approvals.size(), 1u);
  const ApprovalRequest& request = delegate().approvals[0];
  EXPECT_EQ(request.kind, ApprovalKind::kNewClient);
  EXPECT_EQ(request.peer.process, Peer().process);  // from the OS, not the wire
  EXPECT_EQ(request.peer.ancestors.size(), 2u);
  EXPECT_EQ(request.claimed_label, "Claude Code");
  EXPECT_TRUE(request.requested.observe);
  EXPECT_FALSE(request.requested.drive);
  EXPECT_TRUE(
      delegate().replies.empty());  // nothing answered until the user does
}

TEST_F(KoanAgentSessionTest,
       ApprovingStoresAPinnedClientWithTheGrantedCapabilities) {
  Client client;
  ClientCapabilities granted;
  granted.observe = true;
  granted.drive = true;  // the dialog allowed more than was asked
  const std::string id = Pair(client, R"(["observe"])", granted);
  EXPECT_TRUE(IsValidClientId(id));
  const ClientRecord record = *store_.Find(id);
  EXPECT_EQ(record.label, "Claude Code");
  EXPECT_EQ(record.public_key, std::vector<uint8_t>(client.public_key.begin(),
                                                    client.public_key.end()));
  EXPECT_TRUE(record.capabilities.observe);
  EXPECT_FALSE(record.capabilities.drive);  // never more than requested
  EXPECT_EQ(record.pinned, PinPeer(Peer()));
  EXPECT_EQ(record.created, wall_.Now());
  EXPECT_EQ(*delegate().LastResult().FindList("capabilities"),
            base::ListValue().Append("space").Append("observe"));
  // Pairing does not authenticate.
  EXPECT_EQ(delegates_.back()->closed, std::nullopt);
}

TEST_F(KoanAgentSessionTest, DenyingAnswersAndClosesWithoutStoringAnything) {
  Client client;
  auto session = NewSession();
  Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                     client.PublicKeyBase64() + R"(","label":"x"}})");
  session->OnApprovalDecision(delegate().approvals[0].id, ApprovalDecision());
  EXPECT_EQ(delegate().LastError(), "Koan.pairingDenied");
  EXPECT_EQ(delegate().closed, CloseReason::kPairingDenied);
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentSessionTest, ADecisionForAnotherRequestIsIgnored) {
  Client client;
  auto session = NewSession();
  Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                     client.PublicKeyBase64() + R"(","label":"x"}})");
  ApprovalDecision yes;
  yes.approved = true;
  session->OnApprovalDecision(delegate().approvals[0].id + 1, yes);
  session->OnApprovalDecision(0, yes);
  EXPECT_EQ(session->state(), Session::State::kAwaitingApproval);
  EXPECT_EQ(store_.size(), 0u);
  session->OnApprovalDecision(delegate().approvals[0].id, yes);
  EXPECT_EQ(store_.size(), 1u);
  // And an answer after the fact does nothing.
  session->OnApprovalDecision(delegate().approvals[0].id, yes);
  EXPECT_EQ(store_.size(), 1u);
}

TEST_F(KoanAgentSessionTest, BadPairingParametersAreRefusedAndCounted) {
  Client client;
  const std::string key = client.PublicKeyBase64();
  const std::vector<std::string> bad = {
      R"({})",
      R"({"label":"x"})",
      R"({"publicKey":"AAAA","label":"x"})",  // too short
      R"({"publicKey":"not base64!","label":"x"})",
      R"({"publicKey":")" + key + R"("})",  // no label
      R"({"publicKey":")" + key + R"(","label":""})",
      R"({"publicKey":")" + key + R"(","label":"a\nb"})",  // control character
      R"({"publicKey":")" + key + R"(","label":")" + std::string(65, 'a') +
          R"("})",
      R"({"publicKey":")" + key + R"(","label":"x","capabilities":"observe"})",
      R"({"publicKey":")" + key + R"(","label":"x","capabilities":[5]})",
      R"({"publicKey":")" + key + R"(","label":"x","capabilities":["root"]})",
      R"({"publicKey":")" + key +
          R"(","label":"x","capabilities":["network"]})",
  };
  for (const std::string& params : bad) {
    // A fresh minute and a fresh block window for each, so that it is the
    // parameters that are judged, not the rate limits.
    ticks_.Advance(base::Seconds(61));
    auto session = NewSession();
    Send(*session, R"({"id":1,"method":"Koan.pair","params":)" + params + "}");
    EXPECT_EQ(delegate().LastError(), "Koan.invalidParams") << params;
    EXPECT_TRUE(delegate().approvals.empty()) << params;
  }
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentSessionTest, ThreePairingsAMinuteThenRateLimited) {
  Client client;
  const std::string pair = R"(,"method":"Koan.pair","params":{"publicKey":")" +
                           client.PublicKeyBase64() +
                           R"(","label":"x","extra":1}})";
  auto session = NewSession();
  // Each starts a prompt, so answer it to be able to start the next one.
  for (int i = 0; i < RateLimiter::kMaxPairingsPerMinute; ++i) {
    Send(*session, R"({"id":)" + std::to_string(i) + pair);
    ASSERT_EQ(session->state(), Session::State::kAwaitingApproval) << i;
    ApprovalDecision yes;
    yes.approved = true;
    session->OnApprovalDecision(delegate().approvals.back().id, yes);
  }
  Send(*session, R"({"id":9)" + pair);
  EXPECT_EQ(delegate().LastError(), "Koan.rateLimited");
  EXPECT_EQ(delegate().approvals.size(),
            static_cast<size_t>(RateLimiter::kMaxPairingsPerMinute));
  // The limit is the peer's, not the connection's, and it runs out.
  ticks_.Advance(base::Seconds(61));
  auto later = NewSession();
  Send(*later, R"({"id":10)" + pair);
  EXPECT_EQ(later->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, APairedClientAuthenticatesWithItsKey) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  EXPECT_EQ(session->client_id(), id);
  EXPECT_EQ(*delegate().LastResult().FindBool("authenticated"), true);
  EXPECT_EQ(store_.Find(id)->last_used, wall_.Now());
  // The authenticated peer may now send large frames.
  EXPECT_EQ(session->capabilities(), ClientCapabilities());
}

TEST_F(KoanAgentSessionTest,
       AnAuthenticatedSessionAnswersStatusAndForwardsNothing) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  Send(*session, R"({"id":20,"method":"Koan.status"})");
  EXPECT_EQ(*delegate().LastResult().FindString("clientId"), id);
  // Every other method, CDP or Koan, is refused: nothing reaches a tab.
  for (const char* method :
       {"Page.navigate", "Runtime.evaluate", "Input.dispatchKeyEvent",
        "Target.getTargets", "Koan.openTab", "Koan.listTabs", "Network.enable",
        "Browser.close", ""}) {
    Send(*session, std::string(R"({"id":21,"method":")") + method +
                       R"(","params":{"url":"https://example.com/"}})");
    EXPECT_EQ(delegate().LastError(), "Koan.unavailable") << method;
  }
  EXPECT_NE(session->state(), Session::State::kClosed);
}

TEST_F(KoanAgentSessionTest,
       PairingOrAuthenticatingAgainAfterAuthenticationCloses) {
  Client client;
  const std::string id = Pair(client);
  for (const char* method : {"Koan.pair", "Koan.authenticate"}) {
    auto session = NewSession();
    ASSERT_TRUE(Authenticate(*session, client, id));
    Send(*session, std::string(R"({"id":30,"method":")") + method + R"("})");
    EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation) << method;
  }
}

TEST_F(KoanAgentSessionTest, AWrongSignatureFails) {
  Client client, impostor;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  Send(*session,
       R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"(","signature":")" + impostor.Sign(id, nonce) + R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
  EXPECT_EQ(session->state(), Session::State::kUnauthenticated);
}

TEST_F(KoanAgentSessionTest, ANonceIsGoodForOneAttempt) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  const std::string good = client.Sign(id, nonce);
  const std::string proof = R"({"id":2,"method":"Koan.authenticate","params":)"
                            R"({"clientId":")" +
                            id + R"(","signature":")";
  // A wrong attempt spends it...
  Send(*session, proof + std::string(86, 'A') + "==\"}}");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
  // ...so the right signature for it no longer works.
  Send(*session, proof + good + "\"}}");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
}

TEST_F(KoanAgentSessionTest, AProofCannotBeReplayedOnAnotherConnection) {
  Client client;
  const std::string id = Pair(client);
  auto first = NewSession();
  Send(*first,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  const std::string signature = client.Sign(id, nonce);
  // A second connection gets its own nonce (the counter has moved on).
  auto second = NewSession();
  Send(*second,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  ASSERT_EQ(second->state(), Session::State::kAwaitingProof);
  Send(*second,
       R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"(","signature":")" + signature + R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
}

TEST_F(KoanAgentSessionTest, AProofAfterTheNonceExpiredFails) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  ticks_.Advance(Session::kNonceLifetime + base::Seconds(1));
  Send(*session,
       R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"(","signature":")" + client.Sign(id, nonce) + R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
}

TEST_F(KoanAgentSessionTest, ACheckOfTimeoutsExpiresTheNonce) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  ASSERT_EQ(session->state(), Session::State::kAwaitingProof);
  ticks_.Advance(Session::kNonceLifetime + base::Seconds(1));
  session->CheckTimeouts();
  EXPECT_EQ(session->state(), Session::State::kUnauthenticated);
}

TEST_F(KoanAgentSessionTest, AProofForAnotherClientIdFails) {
  Client client, other;
  const std::string id = Pair(client);
  const std::string other_id = Pair(other);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  // other's valid signature for other_id, presented for the nonce issued to id.
  Send(*session,
       R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" +
           other_id + R"(","signature":")" + other.Sign(other_id, nonce) +
           R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
}

TEST_F(KoanAgentSessionTest, UnknownAndExpiredClientsAreRefused) {
  Client client;
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" +
           std::string(32, 'a') + R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.unknownClient");

  const std::string id = Pair(client);
  wall_.Advance(kClientExpiry + base::Days(1));
  auto later = NewSession();
  Send(*later,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.clientExpired");
}

TEST_F(KoanAgentSessionTest, UsingAClientKeepsItFromExpiring) {
  Client client;
  const std::string id = Pair(client);
  for (int month = 0; month < 3; ++month) {
    wall_.Advance(base::Days(20));
    auto session = NewSession();
    ASSERT_TRUE(Authenticate(*session, client, id)) << month;
  }
}

TEST_F(KoanAgentSessionTest, ARevokedClientCannotAuthenticate) {
  Client client;
  const std::string id = Pair(client);
  ASSERT_TRUE(store_.Remove(id));
  auto session = NewSession();
  EXPECT_FALSE(Authenticate(*session, client, id));
  EXPECT_EQ(delegate().LastError(), "Koan.unknownClient");
}

TEST_F(KoanAgentSessionTest, AClientRevokedBetweenTheNonceAndTheProofFails) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  ASSERT_TRUE(store_.Remove(id));
  Send(*session,
       R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"(","signature":")" + client.Sign(id, nonce) + R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
}

TEST_F(KoanAgentSessionTest, AChangedProgramIsAskedAboutAndRepinnedOnApproval) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/koan";
  auto session = NewSession(moved);
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  const ApprovalRequest& request = delegate().approvals[0];
  EXPECT_EQ(request.kind, ApprovalKind::kPeerChanged);
  EXPECT_EQ(request.client_id, id);
  EXPECT_EQ(request.previous, PinPeer(Peer()));
  EXPECT_EQ(request.peer.process.path, "/tmp/koan");
  ApprovalDecision yes;
  yes.approved = true;
  session->OnApprovalDecision(request.id, yes);
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(moved));
  EXPECT_EQ(session->state(), Session::State::kAwaitingProof);
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  Send(*session,
       R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"(","signature":")" + client.Sign(id, nonce) + R"("}})");
  EXPECT_EQ(session->state(), Session::State::kAuthenticated);
}

TEST_F(KoanAgentSessionTest,
       RefusingAChangedProgramClosesAndLeavesThePinAlone) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.signing_identity = "EVIL";
  auto session = NewSession(moved);
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  session->OnApprovalDecision(delegate().approvals[0].id, ApprovalDecision());
  EXPECT_EQ(delegate().closed, CloseReason::kPairingDenied);
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));
}

TEST_F(KoanAgentSessionTest, AChangedChainIsAskedAboutOncePerClientAndChain) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity other_chain = Peer();
  other_chain.ancestors[1] = Process("/tmp/claude", "ANTHROPIC");

  auto first = NewSession(other_chain);
  Send(*first,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  ASSERT_EQ(delegate().approvals.size(), 1u);
  EXPECT_EQ(delegate().approvals[0].kind, ApprovalKind::kChainChanged);
  ApprovalDecision yes;
  yes.approved = true;
  first->OnApprovalDecision(delegate().approvals[0].id, yes);
  EXPECT_EQ(first->state(), Session::State::kAwaitingProof);
  // The pin itself is unchanged: the chain was accepted for now, not re-paired.
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));

  // The same chain again: no prompt.
  auto second = NewSession(other_chain);
  Send(*second,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  EXPECT_TRUE(delegate().approvals.empty());
  EXPECT_EQ(second->state(), Session::State::kAwaitingProof);

  // A third chain prompts again.
  PeerIdentity third_chain = Peer();
  third_chain.ancestors[1] = Process("/opt/claude", "ANTHROPIC");
  auto third = NewSession(third_chain);
  Send(*third,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  EXPECT_EQ(delegate().approvals.size(), 1u);
}

TEST_F(KoanAgentSessionTest, FramesSentWhileAPromptIsOpenWaitForTheAnswer) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/koan";
  auto session = NewSession(moved);
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  Send(*session, R"({"id":2,"method":"Koan.nothing"})");
  EXPECT_TRUE(delegate().replies.empty());  // neither is answered yet
  ApprovalDecision yes;
  yes.approved = true;
  session->OnApprovalDecision(delegate().approvals[0].id, yes);
  ASSERT_EQ(delegate().replies.size(), 2u);  // the nonce, then the refusal
  EXPECT_TRUE(delegate().replies[0].FindDict("result"));
  EXPECT_EQ(delegate().LastError(), "Koan.notAuthenticated");
}

TEST_F(KoanAgentSessionTest, APromptNobodyAnswersTimesOut) {
  Client client;
  auto session = NewSession();
  Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                     client.PublicKeyBase64() + R"(","label":"x"}})");
  ticks_.Advance(Session::kApprovalTimeout - base::Seconds(1));
  session->CheckTimeouts();
  EXPECT_EQ(session->state(), Session::State::kAwaitingApproval);
  ticks_.Advance(base::Seconds(2));
  session->CheckTimeouts();
  EXPECT_EQ(delegate().LastError(), "Koan.pairingTimeout");
  EXPECT_EQ(delegate().closed, CloseReason::kTimeout);
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentSessionTest, AnUnfinishedFrameTimesOut) {
  auto session = NewSession();
  const std::vector<uint8_t> frame =
      Frame(R"({"id":1,"method":"Koan.status"})");
  session->OnBytesReceived(base::span(frame).first(6u));
  ticks_.Advance(Session::kFrameTimeout - base::Seconds(1));
  session->CheckTimeouts();
  EXPECT_FALSE(delegate().closed);
  ticks_.Advance(base::Seconds(2));
  session->CheckTimeouts();
  EXPECT_EQ(delegate().closed, CloseReason::kTimeout);
}

TEST_F(KoanAgentSessionTest, AFinishedFrameDoesNotLeaveTheTimerRunning) {
  auto session = NewSession();
  const std::vector<uint8_t> frame =
      Frame(R"({"id":1,"method":"Koan.status"})");
  session->OnBytesReceived(base::span(frame).first(6u));
  session->OnBytesReceived(base::span(frame).subspan(6u));
  ticks_.Advance(base::Minutes(5));
  session->CheckTimeouts();
  EXPECT_FALSE(delegate().closed);
}

TEST_F(KoanAgentSessionTest,
       MethodsOtherThanPairAndAuthenticateAreRefusedBeforeAuthentication) {
  auto session = NewSession();
  Send(*session, R"({"id":1,"method":"Page.navigate","params":{}})");
  EXPECT_EQ(delegate().LastError(), "Koan.notAuthenticated");
  Send(*session, R"({"id":2,"method":"Koan.status"})");
  EXPECT_EQ(delegate().LastError(), "Koan.notAuthenticated");
  Send(*session, R"({"id":3,"method":"Target.getTargets"})");
  EXPECT_EQ(delegate().LastError(), "Koan.notAuthenticated");
}

TEST_F(KoanAgentSessionTest, FiveFailuresCloseAndBlockThePeerForAMinute) {
  // The decoder gives one connection four frames before it authenticates, so
  // five failures take two connections.
  auto first = NewSession();
  for (int i = 0; i < 3; ++i) {
    Send(*first, R"({"id":1,"method":"Page.navigate"})");
  }
  EXPECT_FALSE(delegate().closed);
  auto second = NewSession();
  Send(*second, R"({"id":1,"method":"Page.navigate"})");
  EXPECT_FALSE(delegate().closed);
  Send(*second, R"({"id":1,"method":"Page.navigate"})");
  EXPECT_EQ(delegate().closed, CloseReason::kTooManyFailures);

  // A new connection from the same peer is cut off at its first byte.
  auto again = NewSession();
  Send(*again, R"({"id":1,"method":"Koan.status"})");
  EXPECT_EQ(delegate().closed, CloseReason::kBlocked);
  EXPECT_TRUE(delegate().replies.empty());

  // Another peer is not affected.
  PeerIdentity other = Peer();
  other.process.path = "/other/koan";
  auto elsewhere = NewSession(other);
  Send(*elsewhere, R"({"id":1,"method":"Koan.status"})");
  EXPECT_FALSE(delegate().closed);

  ticks_.Advance(RateLimiter::kBlockDuration + base::Seconds(1));
  auto later = NewSession();
  Send(*later, R"({"id":1,"method":"Koan.status"})");
  EXPECT_FALSE(delegate().closed);
}

TEST_F(KoanAgentSessionTest, FailedProofsCountAndABlockedPeerIsCutOff) {
  Client client, impostor;
  const std::string id = Pair(client);
  // Five connections, each with a wrong proof.
  for (int i = 0; i < RateLimiter::kMaxFailures; ++i) {
    auto session = NewSession();
    EXPECT_FALSE(Authenticate(*session, impostor, id)) << i;
  }
  EXPECT_EQ(delegate().closed, CloseReason::kTooManyFailures);
  // Even the right key is refused while blocked.
  auto again = NewSession();
  EXPECT_FALSE(Authenticate(*again, client, id));
  EXPECT_EQ(delegate().closed, CloseReason::kBlocked);
  // And it works again when the block has run out.
  ticks_.Advance(RateLimiter::kBlockDuration + base::Seconds(1));
  auto later = NewSession();
  EXPECT_TRUE(Authenticate(*later, client, id));
}

TEST_F(KoanAgentSessionTest, ASuccessfulAuthenticationClearsTheFailures) {
  Client client;
  const std::string id = Pair(client);
  auto noisy = NewSession();
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    Send(*noisy, R"({"id":1,"method":"Page.navigate"})");
  }
  EXPECT_FALSE(delegate().closed);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  auto again = NewSession();
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    Send(*again, R"({"id":1,"method":"Page.navigate"})");
  }
  EXPECT_FALSE(delegate().closed);
}

TEST_F(KoanAgentSessionTest,
       AnythingPipelinedBehindTheProofClosesTheConnection) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session,
       R"({"id":1,"method":"Koan.authenticate","params":{"clientId":")" + id +
           R"("}})");
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  std::vector<uint8_t> both =
      Frame(R"({"id":2,"method":"Koan.authenticate","params":{"clientId":")" +
            id + R"(","signature":")" + client.Sign(id, nonce) + R"("}})");
  const std::vector<uint8_t> extra =
      Frame(R"({"id":3,"method":"Koan.status"})");
  both.insert(both.end(), extra.begin(), extra.end());
  session->OnBytesReceived(both);
  EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation);
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
}

TEST_F(KoanAgentSessionTest, MalformedMessagesCloseTheConnection) {
  for (const char* json :
       {"not json", "[]", "5", R"({"method":"Koan.status"})", R"({"id":1})",
        R"({"id":-1,"method":"Koan.status"})",
        R"({"id":"1","method":"Koan.status"})", R"({"id":1,"method":5})",
        R"({"id":1,"method":"Koan.status","params":5})",
        R"({"id":1,"method":"Koan.status","params":[]})"}) {
    auto session = NewSession();
    Send(*session, json);
    EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation) << json;
  }
}

TEST_F(KoanAgentSessionTest,
       AFrameOverTheLimitClosesTheConnectionBeforeItIsRead) {
  auto session = NewSession();
  const std::vector<uint8_t> header = {0, 0, 0x20, 0};  // 8192 bytes announced
  session->OnBytesReceived(header);
  EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation);
}

TEST_F(KoanAgentSessionTest, AnUnauthenticatedPeerGetsAFewFramesOnly) {
  auto session = NewSession();
  // Frames are answered one by one; the decoder allows kMaxPreAuthFrames in all
  // and the failure limit closes first (5 failures), so the session ends
  // closed either way.
  for (int i = 0; i < 20 && !delegate().closed; ++i) {
    Send(*session, R"({"id":1,"method":"Koan.nope"})");
  }
  EXPECT_TRUE(delegate().closed.has_value());
}

TEST_F(KoanAgentSessionTest, RepliesEchoTheRequestIdAndAreWellFormed) {
  auto session = NewSession();
  Send(*session, R"({"id":4242,"method":"Koan.nothing"})");
  EXPECT_EQ(*delegate().Last().FindInt("id"), 4242);
  const base::DictValue& error = *delegate().Last().FindDict("error");
  EXPECT_EQ(*error.FindInt("code"), -32000);
  EXPECT_TRUE(error.FindString("message"));
  EXPECT_EQ(*error.FindString("data"), "Koan.notAuthenticated");
}

TEST_F(KoanAgentSessionTest, ALabelIsNeverAnIdentifier) {
  // Two clients with the same label and a label that looks like a path: ids are
  // random and unrelated to the label.
  Client a, b;
  const auto pair_with_label = [&](const Client& client,
                                   const std::string& label) {
    auto session = NewSession();
    Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                       client.PublicKeyBase64() + R"(","label":")" + label +
                       R"("}})");
    ApprovalDecision yes;
    yes.approved = true;
    session->OnApprovalDecision(delegate().approvals[0].id, yes);
    return *delegate().LastResult().FindString("clientId");
  };
  const std::string id_a = pair_with_label(a, "../../etc/passwd");
  const std::string id_b = pair_with_label(b, "../../etc/passwd");
  EXPECT_NE(id_a, id_b);
  EXPECT_TRUE(IsValidClientId(id_a));
  EXPECT_TRUE(IsValidClientId(id_b));
  EXPECT_EQ(store_.Find(id_a)->label, "../../etc/passwd");
}

}  // namespace koan::agent
