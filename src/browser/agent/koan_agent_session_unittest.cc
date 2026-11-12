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
#include "base/strings/string_number_conversions.h"
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
  bool PeerUnchanged() override {
    ++rechecks;
    return peer_unchanged;
  }
  bool Forward(AgentMessage message) override {
    if (!forwards) {
      return false;
    }
    forwarded.push_back(std::move(message));
    return true;
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
  // What the gateway's second look at the peer finds, and how often it looked.
  bool peer_unchanged = true;
  int rechecks = 0;
  // Whether Forward() takes messages, and what it took.
  bool forwards = false;
  std::vector<AgentMessage> forwarded;

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

  // The signature that goes with a pairing: proof of holding the key.
  std::string PairingProof(std::string_view label) const {
    const std::vector<uint8_t> message = PairingMessage(public_key, label);
    std::array<uint8_t, kSignatureSize> signature;
    EXPECT_EQ(ED25519_sign(signature.data(), message.data(), message.size(),
                           private_key.data()),
              1);
    return base::Base64Encode(signature);
  }

  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> public_key;
  std::array<uint8_t, ED25519_PRIVATE_KEY_LEN> private_key;
};

// A JSON string literal for `text`.
std::string JsonString(const std::string& text) {
  return *base::WriteJson(base::Value(text));
}

// 64 bytes of base64 that are not a signature of anything.
constexpr char kJunkProof[] =
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAA==";

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
    env.browser_uid = Peer().uid;
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

  // A Koan.pair request with a proof made by `client`. `label` is the text, not
  // JSON; `capabilities` is a JSON list.
  std::string PairFrame(const Client& client,
                        const std::string& label = "Claude Code",
                        const std::string& capabilities = "[]",
                        int id = 1) {
    return R"({"id":)" + base::NumberToString(id) +
           R"(,"method":"Koan.pair","params":{"publicKey":")" +
           client.PublicKeyBase64() + R"(","label":)" + JsonString(label) +
           R"(,"capabilities":)" + capabilities + R"(,"proof":")" +
           client.PairingProof(label) + R"("}})";
  }

  std::string AuthenticateFrame(const std::string& client_id, int id = 10) {
    return R"({"id":)" + base::NumberToString(id) +
           R"(,"method":"Koan.authenticate","params":{"clientId":")" +
           client_id + R"("}})";
  }

  // Asks for a nonce and answers with `signer`'s signature of it. Returns
  // whether a nonce was given; the outcome is in the session and the delegate.
  bool SendProof(Session& session,
                 const Client& signer,
                 const std::string& client_id) {
    Send(session, AuthenticateFrame(client_id));
    if (session.state() != Session::State::kAwaitingProof) {
      return false;
    }
    const std::vector<uint8_t> nonce =
        *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
    Send(session, R"({"id":11,"method":"Koan.authenticate","params":)"
                  R"({"clientId":")" +
                      client_id + R"(","signature":")" +
                      signer.Sign(client_id, nonce) + R"("}})");
    return true;
  }

  static ApprovalDecision Yes() {
    ApprovalDecision yes;
    yes.approved = true;
    return yes;
  }

  // Pairs `client` and returns its id, approving with `granted`.
  std::string Pair(const Client& client,
                   const std::string& capabilities = "[]",
                   ClientCapabilities granted = ClientCapabilities()) {
    auto session = NewSession();
    Send(*session, PairFrame(client, "Claude Code", capabilities));
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
    return SendProof(session, client, client_id) &&
           session.state() == Session::State::kAuthenticated;
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
  Send(*session, PairFrame(client, "Claude Code", R"(["observe"])"));
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
  Send(*session, PairFrame(client, "x"));
  session->OnApprovalDecision(delegate().approvals[0].id, ApprovalDecision());
  EXPECT_EQ(delegate().LastError(), "Koan.pairingDenied");
  EXPECT_EQ(delegate().closed, CloseReason::kPairingDenied);
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentSessionTest, ADecisionForAnotherRequestIsIgnored) {
  Client client;
  auto session = NewSession();
  Send(*session, PairFrame(client, "x"));
  const ApprovalDecision yes = Yes();
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
  const std::string proof = std::string(R"(,"proof":")") + kJunkProof + R"("})";
  const std::vector<std::string> bad = {
      R"({})",
      R"({"label":"x"})",
      R"({"publicKey":"AAAA","label":"x"})" /* too short */,
      R"({"publicKey":"not base64!","label":"x"})",
      R"({"publicKey":")" + key + R"("})",              // no label
      R"({"publicKey":")" + key + R"(","label":"x"})",  // no proof
      R"({"publicKey":")" + key + R"(","label":"x","proof":5})",
      R"({"publicKey":")" + key + R"(","label":""})",
      R"({"publicKey":")" + key + R"(","label":"a\nb"})",  // control character
      R"({"publicKey":")" + key + R"(","label":")" + std::string(65, 'a') +
          R"("})",
      R"({"publicKey":")" + key + R"(","label":"x","capabilities":"observe")" +
          proof,
      R"({"publicKey":")" + key + R"(","label":"x","capabilities":[5])" + proof,
      R"({"publicKey":")" + key + R"(","label":"x","capabilities":["root"])" +
          proof,
      R"({"publicKey":")" + key +
          R"(","label":"x","capabilities":["network"])" + proof,
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
  auto session = NewSession();
  // Each starts a prompt, so answer it to be able to start the next one.
  for (int i = 0; i < RateLimiter::kMaxPromptsPerMinute; ++i) {
    Send(*session, PairFrame(client, "x", "[]", i));
    ASSERT_EQ(session->state(), Session::State::kAwaitingApproval) << i;
    session->OnApprovalDecision(delegate().approvals.back().id, Yes());
  }
  Send(*session, PairFrame(client, "x", "[]", 9));
  EXPECT_EQ(delegate().LastError(), "Koan.rateLimited");
  EXPECT_EQ(delegate().approvals.size(),
            static_cast<size_t>(RateLimiter::kMaxPromptsPerMinute));
  // The limit is the user's, not the connection's, and it runs out.
  ticks_.Advance(base::Seconds(61));
  auto later = NewSession();
  Send(*later, PairFrame(client, "x", "[]", 10));
  EXPECT_EQ(later->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, ThePromptLimitIsNotDodgedByChangingThePath) {
  Client client;
  // Four copies of the program at four paths, each asking once: the user is
  // asked three times, not four, because the key is the user.
  for (int i = 0; i < RateLimiter::kMaxPromptsPerMinute + 1; ++i) {
    PeerIdentity peer = Peer();
    peer.process.path = "/tmp/copy" + base::NumberToString(i) + "/koan";
    auto session = NewSession(peer);
    Send(*session, PairFrame(client, "x"));
    if (i < RateLimiter::kMaxPromptsPerMinute) {
      ASSERT_EQ(session->state(), Session::State::kAwaitingApproval) << i;
      session->OnApprovalDecision(delegate().approvals.back().id, Yes());
    } else {
      EXPECT_EQ(delegate().LastError(), "Koan.rateLimited");
      EXPECT_TRUE(delegate().approvals.empty());
    }
  }
}

TEST_F(KoanAgentSessionTest, OnlyOnePromptIsOpenAtATime) {
  Client client;
  auto first = NewSession();
  Send(*first, PairFrame(client, "x"));
  ASSERT_EQ(first->state(), Session::State::kAwaitingApproval);
  auto second = NewSession();
  Send(*second, PairFrame(client, "y"));
  EXPECT_EQ(delegate().LastError(), "Koan.rateLimited");
  EXPECT_TRUE(delegate().approvals.empty());
  EXPECT_NE(second->state(), Session::State::kAwaitingApproval);
  // The slot comes back when the first is answered...
  first->OnApprovalDecision(delegates_[0]->approvals[0].id, Yes());
  auto third = NewSession();
  Send(*third, PairFrame(client, "z"));
  EXPECT_EQ(third->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, AnOpenPromptIsGivenBackWhenItsConnectionEnds) {
  Client client;
  {
    auto dropped = NewSession();
    Send(*dropped, PairFrame(client, "x"));
    ASSERT_EQ(dropped->state(), Session::State::kAwaitingApproval);
  }  // the connection went away with the dialog open
  auto next = NewSession();
  Send(*next, PairFrame(client, "y"));
  EXPECT_EQ(next->state(), Session::State::kAwaitingApproval);
  // And a timeout gives it back too.
  ticks_.Advance(Session::kApprovalTimeout + base::Seconds(1));
  next->CheckTimeouts();
  ASSERT_EQ(delegate().closed, CloseReason::kTimeout);
  ticks_.Advance(base::Seconds(61));
  auto after = NewSession();
  Send(*after, PairFrame(client, "z"));
  EXPECT_EQ(after->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, DenialsAndTimeoutsCountAsFailures) {
  Client client;
  // Five refusals within the failure window block the peer, though each was
  // "just" a user saying no. Three prompts a minute, so it takes two minutes.
  for (int i = 0; i < RateLimiter::kMaxFailures; ++i) {
    if (i == RateLimiter::kMaxPromptsPerMinute) {
      ticks_.Advance(base::Seconds(61));
    }
    auto session = NewSession();
    Send(*session, PairFrame(client, "x"));
    ASSERT_EQ(session->state(), Session::State::kAwaitingApproval) << i;
    if (i % 2 == 0) {
      session->OnApprovalDecision(delegate().approvals.back().id,
                                  ApprovalDecision());
    } else {
      ticks_.Advance(Session::kApprovalTimeout + base::Seconds(1));
      session->CheckTimeouts();
      EXPECT_EQ(delegate().closed, CloseReason::kTimeout);
    }
  }
  EXPECT_TRUE(limiter_.IsBlocked(PeerKey(Peer()), ticks_.NowTicks()));
}

TEST_F(KoanAgentSessionTest, AnUnfinishedFrameCountsAsAFailure) {
  auto session = NewSession();
  const std::vector<uint8_t> frame =
      Frame(R"({"id":1,"method":"Koan.status"})");
  session->OnBytesReceived(base::span(frame).first(6u));
  ticks_.Advance(Session::kFrameTimeout + base::Seconds(1));
  session->CheckTimeouts();
  ASSERT_EQ(delegate().closed, CloseReason::kTimeout);
  // Four more are enough to block the peer.
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    auto again = NewSession();
    again->OnBytesReceived(base::span(frame).first(6u));
    ticks_.Advance(Session::kFrameTimeout + base::Seconds(1));
    again->CheckTimeouts();
  }
  EXPECT_TRUE(limiter_.IsBlocked(PeerKey(Peer()), ticks_.NowTicks()));
}

TEST_F(KoanAgentSessionTest, TwoSessionsNeverShareAnApprovalId) {
  Client client;
  auto first = NewSession();
  Send(*first, PairFrame(client, "x"));
  const uint64_t first_id = delegate().approvals[0].id;
  first->OnApprovalDecision(first_id, Yes());
  auto second = NewSession();
  Send(*second, PairFrame(client, "x"));
  EXPECT_NE(delegate().approvals[0].id, first_id);
  // An answer meant for the first session does nothing to the second.
  second->OnApprovalDecision(first_id, Yes());
  EXPECT_EQ(second->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, PairingNeedsProofOfHoldingTheKey) {
  Client client, other;
  const std::string key = client.PublicKeyBase64();
  const auto frame = [&](const std::string& label, const std::string& proof) {
    return R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" + key +
           R"(","label":)" + JsonString(label) + R"(,"proof":")" + proof +
           R"("}})";
  };
  // Someone else's signature, a signature over another label, junk.
  for (const std::string& proof :
       {other.PairingProof("x"), client.PairingProof("y"),
        std::string(kJunkProof)}) {
    ticks_.Advance(base::Seconds(61));
    auto session = NewSession();
    Send(*session, frame("x", proof));
    EXPECT_EQ(delegate().LastError(), "Koan.invalidProof");
    EXPECT_TRUE(delegate().approvals.empty());
  }
  ticks_.Advance(base::Seconds(61));
  auto good = NewSession();
  Send(*good, frame("x", client.PairingProof("x")));
  EXPECT_EQ(good->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, APointOfSmallOrderIsNotAKey) {
  // The identity point: with it, a signature "of" anything verifies, so the
  // pairing would protect nothing.
  std::array<uint8_t, 32> identity = {1};
  const std::string key = base::Base64Encode(identity);
  auto session = NewSession();
  Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                     key + R"(","label":"x","proof":")" + kJunkProof +
                     R"("}})");
  EXPECT_EQ(delegate().LastError(), "Koan.invalidProof");
  EXPECT_TRUE(delegate().approvals.empty());
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentSessionTest, LabelsThatCouldSpoofTheDialogAreRefused) {
  Client client;
  const std::vector<std::string> bad = {
      "Claude \xE2\x80\xAE"
      "edoC",  // U+202E right-to-left override
      "a\xE2\x81\xA6"
      "b",  // U+2066 left-to-right isolate
      "a\xE2\x80\x8B"
      "b",  // U+200B zero-width space
      "a\xC2\x85"
      "b",  // U+0085 next line
      "a\xE2\x80\xA8"
      "b",  // U+2028 line separator
      "a\xE2\x80\xA9"
      "b",     // U+2029 paragraph separator
      "a\tb",  // control
      "a\x7f"
      "b",          // DEL
      "e\xCC\x81",  // U+0301 combining acute accent
      "a\xE0\xA4\x82"
      "b",  // U+0902 a spacing/nonspacing mark
      "a\xE3\x85\xA4"
      "b",  // U+3164 Hangul filler
      "a\xE1\x85\x9F"
      "b",  // U+115F Hangul choseong filler
      "a\xE1\x85\xA0"
      "b",  // U+1160 Hangul jungseong filler
      "a\xEF\xBE\xA0"
      "b",  // U+FFA0 halfwidth Hangul filler
      "a\xE2\xA0\x80"
      "b",  // U+2800 braille pattern blank
      "a\xEE\x80\x80"
      "b",  // U+E000 private use
      "a\xF3\xB0\x80\x80"
      "b",  // U+F0000 supplementary private use
      "a\xEF\xB8\x8F"
      "b",  // U+FE0F variation selector
      "a\xF3\xA0\x80\x81"
      "b",  // U+E0001 language tag (Cf)
      "a\xE2\x98\x95"
      "b",  // U+2615 a symbol (hot beverage)
      "a\xE2\x80\xA6"
      "b",             // U+2026 punctuation beyond ASCII
      " Claude",       // leading space
      "Claude ",       // trailing space
      "Claude  Code",  // a run of spaces
      "Claude\xC2\xA0"
      "Code",  // no-break space
      "Claude\xE3\x80\x80"
      "Code",                 // ideographic space
      "Claude\xC2\xA0 Code",  // no-break space then space
      "\xC2\xA0"
      "Claude",                           // leading no-break space
      std::string(65, 'a'),               // 65 characters
      std::string(65, 'a') + "\xC3\xA9",  // 66
  };
  for (const std::string& label : bad) {
    ticks_.Advance(base::Seconds(61));
    auto session = NewSession();
    Send(*session, R"({"id":1,"method":"Koan.pair","params":{"publicKey":")" +
                       client.PublicKeyBase64() + R"(","label":)" +
                       JsonString(label) + R"(,"proof":")" + kJunkProof +
                       R"("}})");
    EXPECT_TRUE(delegate().approvals.empty()) << label;
    EXPECT_EQ(delegate().LastError(), "Koan.invalidParams") << label;
  }
}

TEST_F(KoanAgentSessionTest, OrdinaryLabelsAreAccepted) {
  Client client;
  // The limit is characters, not bytes: 64 two-byte characters are fine.
  std::string accents;
  for (int i = 0; i < 64; ++i) {
    accents += "\xC3\xA9";
  }
  for (const std::string& label :
       {std::string("Claude Code"), std::string("Caf\xC3\xA9"),
        std::string("\xE3\x82\xAF\xE3\x83\xAD\xE3\x83\xBC\xE3\x83\x89"),
        std::string(
            "\xD0\x9A\xD0\xBB\xD0\xBE\xD0\xB4 2"),  // Cyrillic and a digit
        std::string(
            "\xED\x81\xB4\xEB\xA1\x9C\xEB\x93\x9C"),  // Hangul syllables
        accents, std::string(64, 'a'), std::string("../../etc/passwd"),
        std::string("Claude Code (work) #2 - v1.0_beta, \"x\" & 'y'")}) {
    ticks_.Advance(base::Seconds(61));
    auto session = NewSession();
    Send(*session, PairFrame(client, label));
    EXPECT_EQ(session->state(), Session::State::kAwaitingApproval) << label;
  }
}

TEST_F(KoanAgentSessionTest, ChangedProgramPromptsAreRateLimitedToo) {
  Client client;
  const std::string id = Pair(client);  // one prompt of the three
  // Two connections from other places, each proved and each asked about.
  for (const char* path : {"/tmp/a/koan", "/tmp/b/koan"}) {
    PeerIdentity moved = Peer();
    moved.process.path = path;
    auto session = NewSession(moved);
    ASSERT_TRUE(SendProof(*session, client, id));
    ASSERT_EQ(session->state(), Session::State::kAwaitingApproval) << path;
    EXPECT_EQ(delegate().approvals.back().kind, ApprovalKind::kPeerChanged);
    session->OnApprovalDecision(delegate().approvals.back().id, Yes());
    EXPECT_EQ(session->state(), Session::State::kAuthenticated);
  }
  // The third is within the same minute: the proof verifies, the user is not
  // asked, nothing is pinned, and the connection is not authenticated.
  PeerIdentity third = Peer();
  third.process.path = "/tmp/c/koan";
  auto session = NewSession(third);
  ASSERT_TRUE(SendProof(*session, client, id));
  EXPECT_EQ(delegate().LastError(), "Koan.rateLimited");
  EXPECT_TRUE(delegate().approvals.empty());
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
  EXPECT_NE(session->state(), Session::State::kAwaitingApproval);
  PeerIdentity b = Peer();
  b.process.path = "/tmp/b/koan";
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(b));
  // A minute later it can be asked.
  ticks_.Advance(base::Seconds(61));
  auto later = NewSession(third);
  ASSERT_TRUE(SendProof(*later, client, id));
  EXPECT_EQ(later->state(), Session::State::kAwaitingApproval);
}

TEST_F(KoanAgentSessionTest, ChangedChainPromptsAreRateLimitedToo) {
  Client client;
  const std::string id = Pair(client);  // one prompt of the three
  int asked = 0;
  for (const char* agent :
       {"/tmp/a/claude", "/tmp/b/claude", "/tmp/c/claude"}) {
    PeerIdentity other_chain = Peer();
    other_chain.ancestors[1] = Process(agent, "ANTHROPIC");
    auto session = NewSession(other_chain);
    ASSERT_TRUE(SendProof(*session, client, id));
    if (!delegate().approvals.empty()) {
      ++asked;
      EXPECT_EQ(delegate().approvals.back().kind, ApprovalKind::kChainChanged);
      session->OnApprovalDecision(delegate().approvals.back().id, Yes());
    } else {
      EXPECT_EQ(delegate().LastError(), "Koan.rateLimited") << agent;
      EXPECT_NE(session->state(), Session::State::kAuthenticated);
    }
  }
  EXPECT_EQ(asked, 2);
  EXPECT_EQ(chains_.size(), 2u);  // the refused chain is not remembered
}

TEST_F(KoanAgentSessionTest, AStalledFrameAfterAuthenticationIsNotAFailure) {
  Client client;
  const std::string id = Pair(client);
  const std::vector<uint8_t> frame =
      Frame(R"({"id":1,"method":"Koan.status"})");
  for (int i = 0; i < RateLimiter::kMaxFailures + 1; ++i) {
    auto session = NewSession();
    ASSERT_TRUE(Authenticate(*session, client, id)) << i;
    session->OnBytesReceived(base::span(frame).first(6u));
    ticks_.Advance(Session::kFrameTimeout + base::Seconds(1));
    session->CheckTimeouts();
    EXPECT_EQ(delegate().closed, CloseReason::kTimeout) << i;
  }
  // Six stalls and the peer is still not blocked.
  EXPECT_FALSE(limiter_.IsBlocked(PeerKey(Peer()), ticks_.NowTicks()));
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
       WithNothingToForwardToAnAuthenticatedSessionRefusesAllButStatus) {
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

TEST_F(KoanAgentSessionTest, AnAuthenticatedSessionForwardsTheParsedMessage) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  delegate().forwards = true;
  const size_t replies = delegate().replies.size();
  Send(*session, R"({"id":22,"method":"Page.navigate","sessionId":"S1",)"
                 R"("params":{"url":"https://example.com/"}})");
  ASSERT_EQ(delegate().forwarded.size(), 1u);
  const AgentMessage& message = delegate().forwarded[0];
  EXPECT_EQ(message.id, 22);
  EXPECT_EQ(message.method, "Page.navigate");
  EXPECT_EQ(message.session_id, "S1");
  EXPECT_EQ(*message.params.FindString("url"), "https://example.com/");
  EXPECT_EQ(message.client_id, id);
  // The forwarder answers, not the session.
  EXPECT_EQ(delegate().replies.size(), replies);

  // A session id that is not a string is a protocol violation.
  Send(*session, R"({"id":23,"method":"Page.reload","sessionId":7})");
  EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation);
  EXPECT_EQ(delegate().forwarded.size(), 1u);
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

TEST_F(KoanAgentSessionTest, UnknownExpiredAndForgedAreTheSameAnswer) {
  Client client, impostor;
  const std::string id = Pair(client);
  const std::string expired_id = Pair(client);
  const std::string unknown_id(32, 'a');

  // The same error, word for word, at the same step, whatever the reason.
  const auto failure = [&](const Client& signer, const std::string& client_id) {
    ticks_.Advance(base::Seconds(61));  // keep clear of the failure limit
    auto session = NewSession();
    EXPECT_TRUE(SendProof(*session, signer, client_id)) << client_id;
    return delegate().Last().Clone();
  };
  const base::DictValue forged = failure(impostor, id);
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
  const base::DictValue unknown = failure(client, unknown_id);
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
  // An expired pairing: leave one record unused for too long.
  wall_.Advance(kClientExpiry + base::Days(1));
  const base::DictValue expired = failure(client, expired_id);
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");

  EXPECT_EQ(forged, unknown);
  EXPECT_EQ(forged, expired);
}

TEST_F(KoanAgentSessionTest, ANonceIsGivenForAnyWellFormedId) {
  auto session = NewSession();
  Send(*session, AuthenticateFrame(std::string(32, 'a')));
  EXPECT_EQ(session->state(), Session::State::kAwaitingProof);
  EXPECT_TRUE(delegate().LastResult().FindString("nonce"));
  // Malformed ids are not ids.
  auto other = NewSession();
  Send(*other, AuthenticateFrame("not-an-id"));
  EXPECT_EQ(delegate().LastError(), "Koan.invalidParams");
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
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
}

TEST_F(KoanAgentSessionTest, ARevokedClientIsDroppedAtOnceByOnClientRevoked) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  const size_t replies = delegate().replies.size();
  ASSERT_TRUE(store_.Remove(id));
  session->OnClientRevoked("ffffffffffffffffffffffffffffffff");  // another
  EXPECT_EQ(session->state(), Session::State::kAuthenticated);
  session->OnClientRevoked(id);
  EXPECT_EQ(delegate().closed, CloseReason::kRevoked);
  EXPECT_EQ(session->state(), Session::State::kClosed);
  Send(*session, R"({"id":20,"method":"Koan.status"})");
  EXPECT_EQ(delegate().replies.size(), replies);  // nothing more is answered
}

TEST_F(KoanAgentSessionTest, ARevokedClientIsDroppedAtItsNextRequest) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  const size_t replies = delegate().replies.size();
  ASSERT_TRUE(store_.Remove(id));  // nobody called OnClientRevoked
  Send(*session, R"({"id":20,"method":"Koan.status"})");
  EXPECT_EQ(delegate().closed, CloseReason::kRevoked);
  EXPECT_EQ(delegate().replies.size(), replies);  // not even an error
}

TEST_F(KoanAgentSessionTest, ARevokedClientIsDroppedByTheNextCheckOfTimeouts) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  session->CheckTimeouts();
  EXPECT_EQ(session->state(), Session::State::kAuthenticated);
  ASSERT_TRUE(store_.Remove(id));
  session->CheckTimeouts();
  EXPECT_EQ(delegate().closed, CloseReason::kRevoked);
}

TEST_F(KoanAgentSessionTest, AClientRevokedWhileItsPromptIsOpenIsDropped) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/koan";
  auto session = NewSession(moved);
  ASSERT_TRUE(SendProof(*session, client, id));
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  ASSERT_TRUE(store_.Remove(id));
  session->OnClientRevoked(id);
  EXPECT_EQ(delegate().closed, CloseReason::kRevoked);
  // And if nobody told it, the answer finds the client gone.
  ticks_.Advance(base::Seconds(61));
  const std::string id2 = Pair(client);
  auto other = NewSession(moved);
  ASSERT_TRUE(SendProof(*other, client, id2));
  ASSERT_EQ(other->state(), Session::State::kAwaitingApproval);
  ASSERT_TRUE(store_.Remove(id2));
  other->OnApprovalDecision(delegate().approvals.back().id, Yes());
  EXPECT_NE(other->state(), Session::State::kAuthenticated);
  EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
}

TEST_F(KoanAgentSessionTest, AnExpiredPairingEndsALiveSessionOnlyIfNotUsed) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  // A session in use keeps its pairing alive for as long as it stays...
  for (int day = 0; day < 40; day += 2) {
    wall_.Advance(base::Days(2));
    Send(*session, R"({"id":20,"method":"Koan.status"})");
    ASSERT_NE(session->state(), Session::State::kClosed) << day;
  }
  // ...but if the store says it has expired (it was last used too long ago),
  // the session goes.
  ClientRecord record = *store_.Find(id);
  record.last_used = wall_.Now() - kClientExpiry - base::Days(1);
  ASSERT_TRUE(store_.Remove(id));
  ASSERT_TRUE(store_.Add(record));
  Send(*session, R"({"id":21,"method":"Koan.status"})");
  EXPECT_EQ(delegate().closed, CloseReason::kExpired);
}

TEST_F(KoanAgentSessionTest, ChangedCapabilitiesReachALiveSession) {
  Client client;
  ClientCapabilities granted;
  granted.observe = true;
  const std::string id = Pair(client, R"(["observe"])", granted);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  EXPECT_TRUE(session->capabilities().observe);
  ClientRecord record = *store_.Find(id);
  record.capabilities.observe = false;
  ASSERT_TRUE(store_.Remove(id));
  ASSERT_TRUE(store_.Add(record));
  Send(*session, R"({"id":20,"method":"Koan.status"})");
  EXPECT_FALSE(session->capabilities().observe);
  EXPECT_EQ(*delegate().LastResult().FindList("capabilities"),
            base::ListValue().Append("space"));
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

TEST_F(KoanAgentSessionTest,
       AChangedProgramIsAskedAboutAfterTheProofAndRepinnedOnApproval) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/koan";
  auto session = NewSession(moved);
  // The nonce comes without a question: nothing is proved yet.
  Send(*session, AuthenticateFrame(id));
  ASSERT_EQ(session->state(), Session::State::kAwaitingProof);
  EXPECT_TRUE(delegate().approvals.empty());
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  Send(*session, R"({"id":2,"method":"Koan.authenticate","params":)"
                 R"({"clientId":")" +
                     id + R"(","signature":")" + client.Sign(id, nonce) +
                     R"("}})");
  // The key is proved, so now the user is asked; the pin has not moved.
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  const ApprovalRequest& request = delegate().approvals[0];
  EXPECT_EQ(request.kind, ApprovalKind::kPeerChanged);
  EXPECT_EQ(request.client_id, id);
  EXPECT_EQ(request.previous, PinPeer(Peer()));
  EXPECT_EQ(request.peer.process.path, "/tmp/koan");
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));
  session->OnApprovalDecision(request.id, Yes());
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(moved));
  EXPECT_EQ(session->state(), Session::State::kAuthenticated);
  EXPECT_EQ(*delegate().LastResult().FindBool("authenticated"), true);
}

TEST_F(KoanAgentSessionTest, AWrongProofFromAChangedProgramNeverAsksOrPins) {
  Client client, impostor;
  const std::string id = Pair(client);
  // Anything of the same user can read the client id and start a connection
  // from wherever it likes. Without the key it gets no question put to the user
  // and no change to the pin, however often it tries.
  for (const char* path : {"/tmp/evil/koan", "/home/u/.cache/x/koan"}) {
    PeerIdentity attacker = Peer();
    attacker.process.path = path;
    attacker.ancestors[1] = Process("/tmp/evil/agent", "EVIL");
    ticks_.Advance(base::Seconds(61));
    auto session = NewSession(attacker);
    ASSERT_TRUE(SendProof(*session, impostor, id));
    EXPECT_EQ(delegate().LastError(), "Koan.authFailed");
    EXPECT_TRUE(delegate().approvals.empty());
    EXPECT_NE(session->state(), Session::State::kAwaitingApproval);
  }
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));
  EXPECT_TRUE(chains_.empty());
  // The real client from its real place is not affected.
  auto real = NewSession();
  EXPECT_TRUE(Authenticate(*real, client, id));
}

TEST_F(KoanAgentSessionTest,
       RefusingAChangedProgramClosesAndLeavesThePinAlone) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.signing_identity = "EVIL";
  auto session = NewSession(moved);
  ASSERT_TRUE(SendProof(*session, client, id));
  ASSERT_EQ(delegate().approvals.size(), 1u);
  session->OnApprovalDecision(delegate().approvals[0].id, ApprovalDecision());
  EXPECT_EQ(delegate().closed, CloseReason::kPairingDenied);
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
}

// The client waits while the user is asked again: it is told why (the issue
// #340 pass: it saw only "the user did not answer in time").
TEST_F(KoanAgentSessionTest, AChangedChainSaysWhyWhileAskingAndWhenItTimesOut) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity other_chain = Peer();
  other_chain.ancestors[1] = Process("/opt/tools/codex", "OPENAI");
  auto session = NewSession(other_chain);
  ASSERT_TRUE(SendProof(*session, client, id));
  ASSERT_EQ(delegate().approvals.size(), 1u);
  const base::DictValue& event = delegate().Last();
  EXPECT_FALSE(event.Find("id"));
  EXPECT_EQ(*event.FindString("method"), "Koan.approvalPending");
  EXPECT_EQ(*event.FindStringByDottedPath("params.reason"), "chainChanged");
  const std::string why = *event.FindStringByDottedPath("params.message");
  EXPECT_NE(why.find("started by codex"), std::string::npos) << why;
  EXPECT_NE(why.find("(claude)"), std::string::npos) << why;
  // File names only, never paths.
  EXPECT_EQ(why.find('/'), std::string::npos) << why;

  ticks_.Advance(Session::kApprovalTimeout + base::Seconds(1));
  session->CheckTimeouts();
  EXPECT_EQ(delegate().LastError(), "Koan.pairingTimeout");
  const std::string timeout =
      *delegate().Last().FindStringByDottedPath("error.message");
  EXPECT_NE(timeout.find("started by codex"), std::string::npos) << timeout;
  EXPECT_NE(timeout.find("approve it in Koan"), std::string::npos) << timeout;
}

TEST(KoanAgentReapprovalTest, ReasonsNameTheProgramsOnly) {
  PinnedIdentity paired = PinPeer(Peer());
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/x/koan\x1b[31m";
  const std::string program =
      ReapprovalReason(ApprovalKind::kPeerChanged, paired, moved);
  EXPECT_NE(program.find("from koan?[31m, not the koan"), std::string::npos)
      << program;
  // A pairing whose chain was unknown, and a chain with no agent now.
  PeerIdentity orphan = Peer();
  orphan.ancestors.clear();
  PinnedIdentity unknown = paired;
  unknown.agent = ProcessInfo();
  EXPECT_NE(ReapprovalReason(ApprovalKind::kChainChanged, paired, orphan)
                .find("started by no known program"),
            std::string::npos);
  EXPECT_NE(ReapprovalReason(ApprovalKind::kChainChanged, unknown, Peer())
                .find("(no known program)"),
            std::string::npos);
  EXPECT_EQ(ReapprovalReason(ApprovalKind::kNewClient, paired, Peer()), "");
}

TEST_F(KoanAgentSessionTest, AChangedChainIsAskedAboutOncePerClientAndChain) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity other_chain = Peer();
  other_chain.ancestors[1] = Process("/tmp/claude", "ANTHROPIC");

  auto first = NewSession(other_chain);
  ASSERT_TRUE(SendProof(*first, client, id));
  ASSERT_EQ(delegate().approvals.size(), 1u);
  EXPECT_EQ(delegate().approvals[0].kind, ApprovalKind::kChainChanged);
  // Nothing is recorded as accepted until the user accepts.
  EXPECT_TRUE(chains_.empty());
  first->OnApprovalDecision(delegate().approvals[0].id, Yes());
  EXPECT_EQ(first->state(), Session::State::kAuthenticated);
  // The pin itself is unchanged: the chain was accepted for now, not re-paired.
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));
  EXPECT_EQ(chains_.size(), 1u);

  // The same chain again: no prompt.
  ticks_.Advance(base::Seconds(61));
  auto second = NewSession(other_chain);
  EXPECT_TRUE(Authenticate(*second, client, id));
  EXPECT_TRUE(delegate().approvals.empty());

  // A third chain prompts again.
  PeerIdentity third_chain = Peer();
  third_chain.ancestors[1] = Process("/opt/claude", "ANTHROPIC");
  auto third = NewSession(third_chain);
  ASSERT_TRUE(SendProof(*third, client, id));
  EXPECT_EQ(delegate().approvals.size(), 1u);
}

TEST_F(KoanAgentSessionTest, ADeniedChainIsNotRemembered) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity other_chain = Peer();
  other_chain.ancestors[1] = Process("/tmp/claude", "ANTHROPIC");
  auto session = NewSession(other_chain);
  ASSERT_TRUE(SendProof(*session, client, id));
  session->OnApprovalDecision(delegate().approvals[0].id, ApprovalDecision());
  EXPECT_TRUE(chains_.empty());
  EXPECT_EQ(delegate().closed, CloseReason::kPairingDenied);
}

TEST_F(KoanAgentSessionTest,
       AClientPairedFromAnUnknownChainIsAskedAboutAChain) {
  Client client;
  // Paired from an orphaned process: no agent could be found in its chain.
  PeerIdentity bare = Peer();
  bare.ancestors = {Process("/bin/zsh", "apple")};
  auto pairing = NewSession(bare);
  Send(*pairing, PairFrame(client));
  pairing->OnApprovalDecision(delegate().approvals[0].id, Yes());
  const std::string id = *delegate().LastResult().FindString("clientId");
  ASSERT_TRUE(store_.Find(id)->pinned.agent.path.empty());

  // From the same kind of chain, nothing has changed.
  ticks_.Advance(base::Seconds(61));
  auto same = NewSession(bare);
  EXPECT_TRUE(Authenticate(*same, client, id));
  // From a chain that has an agent in it, the empty pin does not wave it
  // through: it is asked about, once.
  auto agent = NewSession(Peer());
  ASSERT_TRUE(SendProof(*agent, client, id));
  ASSERT_EQ(delegate().approvals.size(), 1u);
  EXPECT_EQ(delegate().approvals[0].kind, ApprovalKind::kChainChanged);
  EXPECT_TRUE(delegate().approvals[0].previous.agent.path.empty());
}

TEST_F(KoanAgentSessionTest, APeerTheOSDidNotNameIsRefusedBeforeAnything) {
  Client client;
  const std::string id = Pair(client);
  const auto expect_refused = [&](PeerIdentity peer, const std::string& frame) {
    auto session = NewSession(std::move(peer));
    Send(*session, frame);
    EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer) << frame;
    EXPECT_TRUE(delegate().replies.empty());
    EXPECT_TRUE(delegate().approvals.empty());
  };
  PeerIdentity unnamed = Peer();
  unnamed.process = ProcessInfo();  // the lookup failed
  PeerIdentity stranger = Peer();
  stranger.uid = Peer().uid + 1;  // another user of the machine
  for (const PeerIdentity& peer : {unnamed, stranger}) {
    expect_refused(peer, PairFrame(client));
    expect_refused(peer, AuthenticateFrame(id));
  }
  // A session built without the browser's uid refuses everyone.
  SessionEnvironment env = Env();
  env.browser_uid.reset();
  delegates_.push_back(std::make_unique<FakeDelegate>());
  Session blind(Peer(), env, delegates_.back().get());
  Send(blind, PairFrame(client));
  EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer);
  // Nothing was pinned or stored for the unnamed.
  EXPECT_EQ(store_.size(), 1u);
}

TEST_F(KoanAgentSessionTest, FramesSentWhileAPromptIsOpenWaitForTheAnswer) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/koan";
  auto session = NewSession(moved);
  ASSERT_TRUE(SendProof(*session, client, id));
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  const size_t replies = delegate().replies.size();
  Send(*session, R"({"id":2,"method":"Koan.status"})");
  EXPECT_EQ(delegate().replies.size(), replies);  // not answered yet
  // What a peer sent behind its proof is not handled as authenticated: the
  // answer authenticates it and then cuts it off.
  session->OnApprovalDecision(delegate().approvals[0].id, Yes());
  EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation);
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
}

TEST_F(KoanAgentSessionTest, APromptNobodyAnswersTimesOut) {
  Client client;
  auto session = NewSession();
  Send(*session, PairFrame(client, "x"));
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
  // Twice the frame timeout, still inside the deadline for authenticating.
  ticks_.Advance(Session::kFrameTimeout * 2);
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
    ticks_.Advance(base::Seconds(61));  // each is judged on its own
    auto session = NewSession();
    Send(*session, json);
    EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation) << json;
  }
}

TEST_F(KoanAgentSessionTest,
       MalformedClosesCountAsFailuresBeforeAuthentication) {
  // Reconnecting after garbage is not free: five of them block the peer.
  for (int i = 0; i < RateLimiter::kMaxFailures; ++i) {
    auto session = NewSession();
    Send(*session, "not json");
    EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation) << i;
  }
  auto next = NewSession();
  Send(*next, R"({"id":1,"method":"Koan.status"})");
  EXPECT_EQ(delegate().closed, CloseReason::kBlocked);
  // An oversize announcement is the same.
  ticks_.Advance(RateLimiter::kBlockDuration + base::Seconds(1));
  for (int i = 0; i < RateLimiter::kMaxFailures; ++i) {
    auto session = NewSession();
    session->OnBytesReceived(std::vector<uint8_t>{0, 0, 0x20, 0});
    EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation) << i;
  }
  EXPECT_TRUE(limiter_.IsBlocked(PeerKey(Peer()), ticks_.NowTicks()));
}

TEST_F(KoanAgentSessionTest,
       AViolationAfterAuthenticationIsNotAGuessingFailure) {
  Client client;
  const std::string id = Pair(client);
  for (int i = 0; i < RateLimiter::kMaxFailures + 1; ++i) {
    auto session = NewSession();
    ASSERT_TRUE(Authenticate(*session, client, id)) << i;
    Send(*session, R"({"id":30,"method":"Koan.pair"})");
    EXPECT_EQ(delegate().closed, CloseReason::kProtocolViolation) << i;
  }
  EXPECT_FALSE(limiter_.IsBlocked(PeerKey(Peer()), ticks_.NowTicks()));
}

TEST_F(KoanAgentSessionTest,
       AConnectionThatDoesNotAuthenticateIsClosedAfter30Seconds) {
  auto session = NewSession();
  ticks_.Advance(Session::kPreAuthDeadline - base::Seconds(1));
  session->CheckTimeouts();
  EXPECT_FALSE(delegate().closed);
  ticks_.Advance(base::Seconds(2));
  session->CheckTimeouts();
  EXPECT_EQ(delegate().closed, CloseReason::kTimeout);
}

TEST_F(KoanAgentSessionTest, KeepingTheConnectionBusyDoesNotExtendTheDeadline) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  // A nonce every few seconds, never a proof: each frame is valid, none is
  // progress. (A nonce expires after 10 s, so it asks again.)
  for (int i = 0; i < 4 && !delegate().closed; ++i) {
    Send(*session, AuthenticateFrame(id, i));
    ticks_.Advance(base::Seconds(9));
    session->CheckTimeouts();
  }
  EXPECT_EQ(delegate().closed, CloseReason::kTimeout);
}

TEST_F(KoanAgentSessionTest,
       TheTimeAPromptIsOpenDoesNotCountAgainstTheDeadline) {
  Client client;
  auto session = NewSession();
  ticks_.Advance(base::Seconds(5));
  Send(*session, PairFrame(client));
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  ticks_.Advance(base::Seconds(50));  // the user takes a while
  session->CheckTimeouts();
  EXPECT_FALSE(delegate().closed);
  session->OnApprovalDecision(delegate().approvals[0].id, Yes());
  ASSERT_FALSE(delegate().closed);
  // 55 s since the connection was made, 50 of them in the prompt: 5 s used.
  ticks_.Advance(base::Seconds(24));
  session->CheckTimeouts();
  EXPECT_FALSE(delegate().closed);  // 29 s
  ticks_.Advance(base::Seconds(2));
  session->CheckTimeouts();
  EXPECT_EQ(delegate().closed, CloseReason::kTimeout);  // 31 s
}

TEST_F(KoanAgentSessionTest,
       TheDeadlineCountsAsAFailureAndNeverAppliesAfterAuthentication) {
  Client client;
  const std::string id = Pair(client);
  auto idle = NewSession();
  ticks_.Advance(Session::kPreAuthDeadline + base::Seconds(1));
  idle->CheckTimeouts();
  EXPECT_EQ(delegate().closed, CloseReason::kTimeout);
  // The failure was counted against the peer.
  for (int i = 1; i < RateLimiter::kMaxFailures; ++i) {
    auto more = NewSession();
    ticks_.Advance(Session::kPreAuthDeadline + base::Seconds(1));
    more->CheckTimeouts();
  }
  EXPECT_TRUE(limiter_.IsBlocked(PeerKey(Peer()), ticks_.NowTicks()));
  // An authenticated session is not subject to it.
  ticks_.Advance(base::Minutes(10));
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  ticks_.Advance(base::Hours(2));
  session->CheckTimeouts();
  EXPECT_NE(session->state(), Session::State::kClosed);
}

TEST_F(KoanAgentSessionTest, ThePeerIsLookedAtAgainBeforeAProofIsVerified) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  Send(*session, AuthenticateFrame(id));
  ASSERT_EQ(session->state(), Session::State::kAwaitingProof);
  const std::vector<uint8_t> nonce =
      *base::Base64Decode(*delegate().LastResult().FindString("nonce"));
  delegate().peer_unchanged = false;  // the peer exec'd another program
  const size_t replies = delegate().replies.size();
  Send(*session, R"({"id":11,"method":"Koan.authenticate","params":)"
                 R"({"clientId":")" +
                     id + R"(","signature":")" + client.Sign(id, nonce) +
                     R"("}})");
  EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer);
  EXPECT_EQ(delegate().replies.size(), replies);
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
  EXPECT_GE(delegate().rechecks, 1);
}

TEST_F(KoanAgentSessionTest, ThePeerIsLookedAtAgainBeforeAPairingIsStored) {
  Client client;
  auto session = NewSession();
  Send(*session, PairFrame(client));
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  delegate().peer_unchanged = false;  // another program while the dialog is up
  session->OnApprovalDecision(delegate().approvals[0].id, Yes());
  EXPECT_EQ(delegate().LastError(), "Koan.peerChanged");
  EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer);
  EXPECT_EQ(store_.size(), 0u);  // the "yes" was for the program that asked
}

TEST_F(KoanAgentSessionTest, ThePeerIsLookedAtAgainBeforeAChangeIsPinned) {
  Client client;
  const std::string id = Pair(client);
  PeerIdentity moved = Peer();
  moved.process.path = "/tmp/koan";
  auto session = NewSession(moved);
  ASSERT_TRUE(SendProof(*session, client, id));
  ASSERT_EQ(session->state(), Session::State::kAwaitingApproval);
  delegate().peer_unchanged = false;
  session->OnApprovalDecision(delegate().approvals[0].id, Yes());
  EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer);
  EXPECT_EQ(store_.Find(id)->pinned, PinPeer(Peer()));
  EXPECT_NE(session->state(), Session::State::kAuthenticated);
  // A chain accepted this way is not remembered either.
  EXPECT_TRUE(chains_.empty());
}

TEST_F(KoanAgentSessionTest, ADenialDoesNotNeedTheSecondLook) {
  Client client;
  auto session = NewSession();
  Send(*session, PairFrame(client));
  delegate().peer_unchanged = false;
  session->OnApprovalDecision(delegate().approvals[0].id, ApprovalDecision());
  EXPECT_EQ(delegate().closed, CloseReason::kPairingDenied);
}

TEST_F(KoanAgentSessionTest, NoDialogIsBuiltForAPeerThatIsNoLongerThere) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  delegate().peer_unchanged = false;  // it exec'd after connecting
  Send(*session, PairFrame(client));
  EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer);
  EXPECT_TRUE(delegate().approvals.empty());
  EXPECT_TRUE(delegate().replies.empty());
  // Nor is a nonce given to it.
  auto other = NewSession();
  delegate().peer_unchanged = false;
  Send(*other, AuthenticateFrame(id));
  EXPECT_EQ(delegate().closed, CloseReason::kUnknownPeer);
  EXPECT_TRUE(delegate().replies.empty());
}

TEST_F(KoanAgentSessionTest, AnAuthenticatedSessionIsNotLookedAtAgain) {
  Client client;
  const std::string id = Pair(client);
  auto session = NewSession();
  ASSERT_TRUE(Authenticate(*session, client, id));
  const int rechecks = delegate().rechecks;
  delegate().peer_unchanged = false;
  Send(*session, R"({"id":20,"method":"Koan.status"})");
  EXPECT_EQ(delegate().rechecks, rechecks);
  EXPECT_NE(session->state(), Session::State::kClosed);
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
    Send(*session, PairFrame(client, label));
    session->OnApprovalDecision(delegate().approvals[0].id, Yes());
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
