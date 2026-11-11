// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_auth.h"

#include <stdint.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
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
                    Process("/usr/local/bin/claude", "ANTHROPIC"),
                    Process("/Applications/Terminal.app/x", "apple")};
  return peer;
}

struct KeyPair {
  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> public_key;
  std::array<uint8_t, ED25519_PRIVATE_KEY_LEN> private_key;
};

KeyPair NewKey() {
  KeyPair key;
  ED25519_keypair(key.public_key.data(), key.private_key.data());
  return key;
}

std::array<uint8_t, kSignatureSize> Sign(const KeyPair& key,
                                         std::string_view client_id,
                                         const std::vector<uint8_t>& nonce) {
  const std::vector<uint8_t> message = ProofMessage(client_id, nonce);
  std::array<uint8_t, kSignatureSize> signature;
  EXPECT_EQ(ED25519_sign(signature.data(), message.data(), message.size(),
                         key.private_key.data()),
            1);
  return signature;
}

constexpr std::string_view kId = "0123456789abcdef0123456789abcdef";

}  // namespace

TEST(KoanAgentAuthTest, TheAgentIsTheFirstAncestorThatIsNotAShellOrAWrapper) {
  PeerIdentity peer = Peer();
  EXPECT_EQ(AgentAncestor(peer).path, "/usr/local/bin/claude");
  peer.ancestors = {Process("-zsh"), Process("/usr/bin/env"),
                    Process("/opt/homebrew/bin/koan"), Process("/usr/bin/sudo"),
                    Process("/opt/x/codex", "OPENAI")};
  EXPECT_EQ(AgentAncestor(peer).path, "/opt/x/codex");
  peer.ancestors = {Process("/bin/bash"), Process("/usr/bin/tmux")};
  EXPECT_TRUE(AgentAncestor(peer).path.empty());
  peer.ancestors.clear();
  EXPECT_TRUE(AgentAncestor(peer).path.empty());
}

TEST(KoanAgentAuthTest, ThePinIsTheProcessAndTheAgent) {
  const PinnedIdentity pin = PinPeer(Peer());
  EXPECT_EQ(pin.process, Peer().process);
  EXPECT_EQ(pin.agent.path, "/usr/local/bin/claude");
  EXPECT_EQ(pin.agent.signing_identity, "ANTHROPIC");
}

TEST(KoanAgentAuthTest, ChangesInTheProgramAndTheChainAreTold) {
  const PinnedIdentity pin = PinPeer(Peer());
  EXPECT_EQ(CompareIdentity(pin, Peer()), IdentityChange::kNone);

  PeerIdentity other_path = Peer();
  other_path.process.path = "/tmp/koan";
  EXPECT_EQ(CompareIdentity(pin, other_path), IdentityChange::kProcessChanged);

  PeerIdentity unsigned_now = Peer();
  unsigned_now.process.signing_identity.clear();
  EXPECT_EQ(CompareIdentity(pin, unsigned_now),
            IdentityChange::kProcessChanged);

  PeerIdentity other_signature = Peer();
  other_signature.process.signing_identity = "EVIL";
  EXPECT_EQ(CompareIdentity(pin, other_signature),
            IdentityChange::kProcessChanged);

  PeerIdentity other_agent = Peer();
  other_agent.ancestors[1] = Process("/tmp/claude", "ANTHROPIC");
  EXPECT_EQ(CompareIdentity(pin, other_agent), IdentityChange::kChainChanged);

  PeerIdentity impostor = Peer();
  impostor.ancestors[1].signing_identity = "EVIL";
  EXPECT_EQ(CompareIdentity(pin, impostor), IdentityChange::kChainChanged);

  // The agent anywhere in the chain is the same agent.
  PeerIdentity deeper = Peer();
  deeper.ancestors.insert(deeper.ancestors.begin(), Process("/usr/bin/node"));
  EXPECT_EQ(CompareIdentity(pin, deeper), IdentityChange::kNone);

  // A pairing that found no agent has nothing to compare, but an empty pin
  // does not switch the check off: a chain that has an agent now is a change.
  PeerIdentity bare = Peer();
  bare.ancestors = {Process("/bin/zsh")};
  EXPECT_EQ(CompareIdentity(PinPeer(bare), bare), IdentityChange::kNone);
  PeerIdentity moved = bare;
  moved.ancestors = {Process("/usr/local/bin/claude")};
  EXPECT_EQ(CompareIdentity(PinPeer(bare), moved),
            IdentityChange::kChainChanged);
}

TEST(KoanAgentAuthTest, PeerKeysAndChainDigestsTellPeersApart) {
  const PeerIdentity peer = Peer();
  PeerIdentity other_user = peer;
  other_user.uid = 502;
  PeerIdentity other_path = peer;
  other_path.process.path += "x";
  EXPECT_NE(PeerKey(peer), PeerKey(other_user));
  EXPECT_NE(PeerKey(peer), PeerKey(other_path));
  EXPECT_EQ(PeerKey(peer), PeerKey(Peer()));

  EXPECT_EQ(ChainDigest(peer), ChainDigest(Peer()));
  EXPECT_EQ(ChainDigest(peer).size(), 64u);
  PeerIdentity other_chain = peer;
  other_chain.ancestors.pop_back();
  EXPECT_NE(ChainDigest(peer), ChainDigest(other_chain));
  // path "a" + identity "bc" is not path "ab" + identity "c"
  PeerIdentity p1, p2;
  p1.ancestors = {Process("a", "bc")};
  p2.ancestors = {Process("ab", "c")};
  EXPECT_NE(ChainDigest(p1), ChainDigest(p2));
}

TEST(KoanAgentAuthTest, CapabilitiesParseFromKnownNamesOnly) {
  auto caps = ParseCapabilities({});
  ASSERT_TRUE(caps.has_value());
  EXPECT_TRUE(caps->space);
  EXPECT_FALSE(caps->observe || caps->read || caps->drive || caps->keep_awake);

  caps = ParseCapabilities({"observe", "keep-awake"});
  ASSERT_TRUE(caps.has_value());
  EXPECT_TRUE(caps->observe && caps->keep_awake);
  EXPECT_FALSE(caps->read || caps->drive);

  for (const std::vector<std::string>& bad :
       std::vector<std::vector<std::string>>{{"root"},
                                             {"Observe"},
                                             {""},
                                             {"network"},
                                             {"cookies"},
                                             {"incognito"},
                                             {"observe", "nope"},
                                             {"keep_awake"}}) {
    EXPECT_FALSE(ParseCapabilities(bad).has_value()) << bad[0];
  }
  EXPECT_FALSE(ParseCapabilities(std::vector<std::string>(9, "space")));
}

TEST(KoanAgentAuthTest, CapabilityNamesRoundTrip) {
  ClientCapabilities all;
  all.observe = all.read = all.drive = all.keep_awake = true;
  EXPECT_EQ(CapabilityNames(all),
            (std::vector<std::string>{"space", "observe", "read", "drive",
                                      "keep-awake"}));
  EXPECT_EQ(*ParseCapabilities(CapabilityNames(all)), all);
  EXPECT_EQ(CapabilityNames(ClientCapabilities()),
            std::vector<std::string>{"space"});
}

TEST(KoanAgentAuthTest, TheUserCanOnlyGrantWhatWasRequested) {
  ClientCapabilities everything;
  everything.observe = everything.read = everything.drive =
      everything.keep_awake = true;
  ClientCapabilities requested;
  requested.observe = true;
  const ClientCapabilities result = Intersect(requested, everything);
  EXPECT_TRUE(result.observe);
  EXPECT_FALSE(result.read || result.drive || result.keep_awake);
  // And no request is not granted just because the dialog allowed it.
  EXPECT_EQ(Intersect(ClientCapabilities(), everything), ClientCapabilities());
  // `space` is on whatever the dialog said.
  ClientCapabilities none;
  none.space = false;
  EXPECT_TRUE(Intersect(requested, none).space);
}

TEST(KoanAgentAuthTest, ClientIdsAre32LowerCaseHexDigits) {
  EXPECT_TRUE(IsValidClientId(kId));
  for (std::string_view bad :
       {"", "0123", "0123456789ABCDEF0123456789ABCDEF",
        "0123456789abcdef0123456789abcdeg", "0123456789abcdef0123456789abcde",
        "0123456789abcdef0123456789abcdef0", "../../etc/passwd",
        "0123456789abcdef0123456789abcde/"}) {
    EXPECT_FALSE(IsValidClientId(bad)) << bad;
  }
}

TEST(KoanAgentAuthTest, TheInMemoryStoreKeepsFindsAndForgets) {
  InMemoryClientStore store;
  ClientRecord record;
  record.id = std::string(kId);
  record.label = "Claude Code";
  record.pinned = PinPeer(Peer());
  ASSERT_TRUE(store.Add(record));
  EXPECT_FALSE(store.Add(record));  // already there
  ClientRecord bad = record;
  bad.id = "../x";
  EXPECT_FALSE(store.Add(bad));
  EXPECT_EQ(store.size(), 1u);

  ASSERT_TRUE(store.Find(kId).has_value());
  EXPECT_EQ(store.Find(kId)->label, "Claude Code");
  EXPECT_FALSE(store.Find("nope").has_value());

  PinnedIdentity pin;
  pin.process = Process("/x");
  EXPECT_TRUE(store.UpdatePinned(kId, pin));
  EXPECT_EQ(store.Find(kId)->pinned, pin);
  EXPECT_FALSE(store.UpdatePinned("nope", pin));

  const base::Time now = base::Time::FromSecondsSinceUnixEpoch(1000);
  store.Touch(kId, now);
  EXPECT_EQ(store.Find(kId)->last_used, now);

  EXPECT_TRUE(store.Remove(kId));
  EXPECT_FALSE(store.Remove(kId));
  EXPECT_FALSE(store.Find(kId).has_value());
}

TEST(KoanAgentAuthTest, TheProofMessageIsPrefixClientIdAndNonceWithNuls) {
  const std::vector<uint8_t> nonce(kNonceSize, 7);
  const std::vector<uint8_t> message = ProofMessage(kId, nonce);
  std::vector<uint8_t> expected;
  for (char c : std::string("koan-agent-v1")) {
    expected.push_back(static_cast<uint8_t>(c));
  }
  expected.push_back(0);
  for (char c : kId) {
    expected.push_back(static_cast<uint8_t>(c));
  }
  expected.push_back(0);
  expected.insert(expected.end(), nonce.begin(), nonce.end());
  EXPECT_EQ(message, expected);
}

TEST(KoanAgentAuthTest, AProofVerifiesOnlyForItsKeyClientAndNonce) {
  const KeyPair key = NewKey();
  const std::vector<uint8_t> nonce(kNonceSize, 3);
  const auto signature = Sign(key, kId, nonce);
  EXPECT_TRUE(VerifyProof(key.public_key, kId, nonce, signature));

  const KeyPair other = NewKey();
  EXPECT_FALSE(VerifyProof(other.public_key, kId, nonce, signature));
  EXPECT_FALSE(VerifyProof(key.public_key,
                           std::string("f") + std::string(kId.substr(1)), nonce,
                           signature));
  std::vector<uint8_t> other_nonce = nonce;
  other_nonce[0] ^= 1;
  EXPECT_FALSE(VerifyProof(key.public_key, kId, other_nonce, signature));
  auto flipped = signature;
  flipped[10] ^= 1;
  EXPECT_FALSE(VerifyProof(key.public_key, kId, nonce, flipped));
}

TEST(KoanAgentAuthTest, ProofsOfTheWrongSizeAreRefused) {
  const KeyPair key = NewKey();
  const std::vector<uint8_t> nonce(kNonceSize, 3);
  const auto signature = Sign(key, kId, nonce);
  EXPECT_FALSE(VerifyProof(base::span(key.public_key).first(31u), kId, nonce,
                           signature));
  EXPECT_FALSE(VerifyProof(key.public_key, kId, nonce,
                           base::span(signature).first(63u)));
  EXPECT_FALSE(VerifyProof(key.public_key, kId, base::span(nonce).first(31u),
                           signature));
  EXPECT_FALSE(VerifyProof({}, kId, nonce, signature));
  EXPECT_FALSE(VerifyProof(key.public_key, kId, nonce, {}));
}

TEST(KoanAgentAuthTest, FiveFailuresBlockAPeerForAMinute) {
  RateLimiter limiter;
  base::TimeTicks now = base::TimeTicks() + base::Seconds(100);
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    EXPECT_FALSE(limiter.RecordFailure("a", now));
    EXPECT_FALSE(limiter.IsBlocked("a", now));
  }
  EXPECT_TRUE(limiter.RecordFailure("a", now));
  EXPECT_TRUE(limiter.IsBlocked("a", now));
  EXPECT_FALSE(limiter.IsBlocked("b", now));  // others are not
  EXPECT_TRUE(limiter.IsBlocked("a", now + base::Seconds(59)));
  EXPECT_FALSE(limiter.IsBlocked("a", now + base::Seconds(61)));
  // After the block, the count starts again.
  EXPECT_FALSE(limiter.RecordFailure("a", now + base::Seconds(70)));
}

TEST(KoanAgentAuthTest, ASuccessClearsTheFailures) {
  RateLimiter limiter;
  const base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    limiter.RecordFailure("a", now);
  }
  limiter.RecordSuccess("a");
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    EXPECT_FALSE(limiter.RecordFailure("a", now));
  }
}

TEST(KoanAgentAuthTest, PeerKeysDifferByPathButUidKeysDoNot) {
  PeerIdentity a = Peer();
  PeerIdentity b = Peer();
  b.process.path = "/tmp/other/koan";
  EXPECT_NE(PeerKey(a), PeerKey(b));
  EXPECT_EQ(UidKey(a),
            UidKey(b));  // changing the path does not change the user
  b.uid = 502;
  EXPECT_NE(UidKey(a), UidKey(b));
}

TEST(KoanAgentAuthTest, AUserGetsThreePromptsAMinute) {
  RateLimiter limiter;
  base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  for (int i = 0; i < RateLimiter::kMaxPromptsPerMinute; ++i) {
    ASSERT_TRUE(limiter.TryBeginPrompt("uid:1", now)) << i;
    limiter.EndPrompt();
  }
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:1", now + base::Seconds(30)));
  EXPECT_TRUE(limiter.TryBeginPrompt("uid:2", now));
  limiter.EndPrompt();
  EXPECT_TRUE(limiter.TryBeginPrompt("uid:1", now + base::Seconds(61)));
}

// T-limits-3: the user's numbers apply from the next failure or prompt; a
// block already running keeps its length.
TEST(KoanAgentAuthTest, TheLimitsCanBeChanged) {
  RateLimiter limiter({.max_failures = 2,
                       .block_duration = base::Minutes(5),
                       .max_prompts_per_minute = 1});
  const base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  EXPECT_FALSE(limiter.RecordFailure("a", now));
  EXPECT_TRUE(limiter.RecordFailure("a", now));
  EXPECT_TRUE(limiter.IsBlocked("a", now + base::Minutes(4)));
  EXPECT_FALSE(
      limiter.IsBlocked("a", now + base::Minutes(5) + base::Seconds(1)));
  ASSERT_TRUE(limiter.TryBeginPrompt("uid:1", now));
  limiter.EndPrompt();
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:1", now));

  limiter.SetLimits({.max_failures = 3,
                     .block_duration = base::Seconds(60),
                     .max_prompts_per_minute = 2});
  ASSERT_TRUE(limiter.TryBeginPrompt("uid:1", now));
  limiter.EndPrompt();
  const base::TimeTicks later = now + base::Minutes(10);
  EXPECT_FALSE(limiter.RecordFailure("b", later));
  EXPECT_FALSE(limiter.RecordFailure("b", later));
  EXPECT_TRUE(limiter.RecordFailure("b", later));
  // Still one prompt open at a time, whatever the numbers.
  ASSERT_TRUE(limiter.TryBeginPrompt("uid:2", later));
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:3", later));
  limiter.EndPrompt();
}

TEST(KoanAgentAuthTest, OnlyOnePromptIsOpenAtATime) {
  RateLimiter limiter;
  const base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  ASSERT_TRUE(limiter.TryBeginPrompt("uid:1", now));
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:1", now));
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:2", now));
  limiter.EndPrompt();
  limiter.EndPrompt();  // an extra end does no harm
  EXPECT_TRUE(limiter.TryBeginPrompt("uid:2", now));
  limiter.EndPrompt();
  // A refused prompt is not counted against the user.
  ASSERT_TRUE(limiter.TryBeginPrompt("uid:1", now));
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:1", now));
  limiter.EndPrompt();
  EXPECT_TRUE(limiter.TryBeginPrompt("uid:1", now));
  limiter.EndPrompt();
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:1", now));  // that was three
}

TEST(KoanAgentAuthTest, FailuresOlderThanTheWindowAreForgotten) {
  RateLimiter limiter;
  base::TimeTicks now = base::TimeTicks() + base::Seconds(100);
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    EXPECT_FALSE(limiter.RecordFailure("a", now));
  }
  // A long time later, one more failure is the first of a new count.
  now += RateLimiter::kFailureWindow + base::Seconds(1);
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    EXPECT_FALSE(limiter.RecordFailure("a", now)) << i;
  }
  EXPECT_TRUE(limiter.RecordFailure("a", now));
  // Failures inside the window still add up, however they are spread.
  RateLimiter spread;
  now = base::TimeTicks() + base::Seconds(100);
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    EXPECT_FALSE(spread.RecordFailure("a", now));
    now += RateLimiter::kFailureWindow / 2;
  }
  EXPECT_TRUE(spread.RecordFailure("a", now));
}

TEST(KoanAgentAuthTest, ATableFilledWithFailuresDoesNotLockOutOtherPeers) {
  RateLimiter limiter;
  const base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  // One process, many paths, one failure each: the table fills and then keeps
  // turning over...
  for (int i = 0; i < 1000; ++i) {
    limiter.RecordFailure("path" + std::to_string(i), now + base::Seconds(i));
  }
  // ...and a peer that was not part of it is still tracked like any other: its
  // first failure does not block it, its fifth does.
  const base::TimeTicks later = now + base::Seconds(1000);
  for (int i = 0; i < RateLimiter::kMaxFailures - 1; ++i) {
    EXPECT_FALSE(limiter.RecordFailure("legitimate", later)) << i;
  }
  EXPECT_FALSE(limiter.IsBlocked("legitimate", later));
  EXPECT_TRUE(limiter.RecordFailure("legitimate", later));
  // Prompts still work for a user.
  EXPECT_TRUE(limiter.TryBeginPrompt("uid:1", later));
}

TEST(KoanAgentAuthTest, ABlockSurvivesAFloodOfOtherPeers) {
  RateLimiter limiter;
  base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  for (int i = 0; i < RateLimiter::kMaxFailures; ++i) {
    limiter.RecordFailure("blocked", now);
  }
  ASSERT_TRUE(limiter.IsBlocked("blocked", now));
  for (int i = 0; i < 1000; ++i) {
    limiter.RecordFailure("other" + std::to_string(i), now);
  }
  EXPECT_TRUE(limiter.IsBlocked("blocked", now));
}

TEST(KoanAgentAuthTest, AUsersPromptCountIsNotPushedOutByTheFailureTable) {
  RateLimiter limiter;
  const base::TimeTicks now = base::TimeTicks() + base::Seconds(1);
  for (int i = 0; i < RateLimiter::kMaxPromptsPerMinute; ++i) {
    ASSERT_TRUE(limiter.TryBeginPrompt("uid:1", now));
    limiter.EndPrompt();
  }
  for (int i = 0; i < 1000; ++i) {
    limiter.RecordFailure("path" + std::to_string(i), now);
  }
  EXPECT_FALSE(limiter.TryBeginPrompt("uid:1", now));
}

TEST(KoanAgentAuthTest, PointsOfSmallOrderAreNotKeys) {
  const std::array<uint8_t, 32> identity = {1};
  const std::array<uint8_t, 32> zero = {};
  std::array<uint8_t, 32> minus_one;
  minus_one.fill(0xff);
  minus_one[0] = 0xec;
  minus_one[31] = 0x7f;
  std::array<uint8_t, 32> minus_one_sign = minus_one;
  minus_one_sign[31] = 0xff;  // the same y with the sign of x set
  std::array<uint8_t, 32> noncanonical = minus_one;
  noncanonical[0] = 0xed;  // y = p, which is 0
  const std::array<uint8_t, 32> order8 = {
      0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4,
      0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6,
      0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05};
  for (const auto& key :
       {identity, zero, minus_one, minus_one_sign, noncanonical, order8}) {
    EXPECT_FALSE(IsAcceptablePublicKey(key));
    // The forgery they allow: R = the point itself, S = 0.
    std::array<uint8_t, kSignatureSize> forged = {1};
    EXPECT_FALSE(
        VerifyProof(key, kId, std::vector<uint8_t>(kNonceSize, 1), forged));
    EXPECT_FALSE(VerifyPairingProof(key, "x", forged));
  }
  EXPECT_TRUE(IsAcceptablePublicKey(NewKey().public_key));
  EXPECT_FALSE(IsAcceptablePublicKey(std::vector<uint8_t>(31, 5)));
  EXPECT_FALSE(IsAcceptablePublicKey({}));
}

TEST(KoanAgentAuthTest, ThePairingMessageIsPrefixKeyAndLabelWithNuls) {
  const std::array<uint8_t, 32> key = {9};
  const std::vector<uint8_t> message = PairingMessage(key, "Claude");
  std::vector<uint8_t> expected;
  for (char c : std::string("koan-agent-v1-pair")) {
    expected.push_back(static_cast<uint8_t>(c));
  }
  expected.push_back(0);
  expected.insert(expected.end(), key.begin(), key.end());
  expected.push_back(0);
  for (char c : std::string("Claude")) {
    expected.push_back(static_cast<uint8_t>(c));
  }
  EXPECT_EQ(message, expected);
  // Not the same bytes as a login proof, so one cannot stand for the other.
  EXPECT_NE(message[13], ProofMessage(kId, std::vector<uint8_t>(32))[13]);
}

TEST(KoanAgentAuthTest, APairingProofVerifiesOnlyForItsKeyAndLabel) {
  const KeyPair key = NewKey();
  const KeyPair other = NewKey();
  const auto sign = [](const KeyPair& pair, std::string_view label) {
    const std::vector<uint8_t> message = PairingMessage(pair.public_key, label);
    std::array<uint8_t, kSignatureSize> signature;
    EXPECT_EQ(ED25519_sign(signature.data(), message.data(), message.size(),
                           pair.private_key.data()),
              1);
    return signature;
  };
  const auto signature = sign(key, "Claude");
  EXPECT_TRUE(VerifyPairingProof(key.public_key, "Claude", signature));
  EXPECT_FALSE(VerifyPairingProof(key.public_key, "Claud", signature));
  EXPECT_FALSE(VerifyPairingProof(other.public_key, "Claude", signature));
  EXPECT_FALSE(VerifyPairingProof(key.public_key, "Claude",
                                  base::span(signature).first(63u)));
  // A login proof is not a pairing proof.
  const auto login = Sign(key, kId, std::vector<uint8_t>(kNonceSize, 3));
  EXPECT_FALSE(VerifyPairingProof(key.public_key, "Claude", login));
}

}  // namespace koan::agent
