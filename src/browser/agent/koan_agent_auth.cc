// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_auth.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace koan::agent {

namespace {

constexpr size_t kMaxCapabilityNames = 8;
// Entries of the rate limiter beyond which the least recently active is
// dropped.
constexpr size_t kMaxRateLimitEntries = 256;

// The encodings of the points of order 1, 2, 4 and 8 on edwards25519 (their y
// coordinates, 0, 1, -1 and the four of order 8, and the non-canonical forms
// y + p that fit in 255 bits), with the sign bit cleared. A public key that is
// one of these makes every signature verify for some message.
constexpr std::array<std::array<uint8_t, 32>, 7> kSmallOrderPoints = {{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4,
     0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6,
     0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05},
    {0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b,
     0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39,
     0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a},
    {0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
    {0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
    {0xee, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
}};

// Programs that stand between an agent and `koan` without being the agent.
constexpr std::array<std::string_view, 17> kWrappers = {
    "sh",     "bash",   "zsh",   "fish",  "dash",  "ksh",
    "tcsh",   "csh",    "env",   "sudo",  "login", "tmux",
    "screen", "script", "nohup", "xargs", "koan",
};

std::string_view Basename(std::string_view path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool IsWrapper(const ProcessInfo& process) {
  std::string_view name = Basename(process.path);
  // `-zsh` is how a login shell names itself.
  if (name.starts_with('-')) {
    name.remove_prefix(1);
  }
  return std::ranges::contains(kWrappers, name) || name == "koan-relay";
}

bool ChainHas(const std::vector<ProcessInfo>& ancestors,
              const ProcessInfo& wanted) {
  return std::ranges::contains(ancestors, wanted);
}

}  // namespace

ProcessInfo AgentAncestor(const PeerIdentity& peer) {
  for (const ProcessInfo& ancestor : peer.ancestors) {
    if (!IsWrapper(ancestor)) {
      return ancestor;
    }
  }
  return ProcessInfo();
}

PinnedIdentity PinPeer(const PeerIdentity& peer) {
  return PinnedIdentity{peer.process, AgentAncestor(peer)};
}

IdentityChange CompareIdentity(const PinnedIdentity& pinned,
                               const PeerIdentity& peer) {
  if (!(pinned.process == peer.process)) {
    return IdentityChange::kProcessChanged;
  }
  if (pinned.agent.path.empty()) {
    // The chain was unknown when this client paired. Nothing to compare, so a
    // chain that has an agent now is a change, not a match.
    return AgentAncestor(peer).path.empty() ? IdentityChange::kNone
                                            : IdentityChange::kChainChanged;
  }
  if (!ChainHas(peer.ancestors, pinned.agent)) {
    return IdentityChange::kChainChanged;
  }
  return IdentityChange::kNone;
}

std::string PeerKey(const PeerIdentity& peer) {
  return base::NumberToString(peer.uid) + ":" + peer.process.path + ":" +
         peer.process.signing_identity;
}

std::string UidKey(const PeerIdentity& peer) {
  return "uid:" + base::NumberToString(peer.uid);
}

std::string ChainDigest(const PeerIdentity& peer) {
  std::string chain;
  for (const ProcessInfo& ancestor : peer.ancestors) {
    chain += ancestor.path;
    chain += '\0';
    chain += ancestor.signing_identity;
    chain += '\n';
  }
  return base::HexEncodeLower(crypto::hash::Sha256(chain));
}

std::optional<ClientCapabilities> ParseCapabilities(
    const std::vector<std::string>& names) {
  if (names.size() > kMaxCapabilityNames) {
    return std::nullopt;
  }
  ClientCapabilities capabilities;
  for (const std::string& name : names) {
    if (name == "space") {
      capabilities.space = true;
    } else if (name == "observe") {
      capabilities.observe = true;
    } else if (name == "read") {
      capabilities.read = true;
    } else if (name == "drive") {
      capabilities.drive = true;
    } else if (name == "keep-awake") {
      capabilities.keep_awake = true;
    } else {
      return std::nullopt;
    }
  }
  return capabilities;
}

std::vector<std::string> CapabilityNames(
    const ClientCapabilities& capabilities) {
  std::vector<std::string> names;
  if (capabilities.space) {
    names.push_back("space");
  }
  if (capabilities.observe) {
    names.push_back("observe");
  }
  if (capabilities.read) {
    names.push_back("read");
  }
  if (capabilities.drive) {
    names.push_back("drive");
  }
  if (capabilities.keep_awake) {
    names.push_back("keep-awake");
  }
  return names;
}

ClientCapabilities Intersect(const ClientCapabilities& requested,
                             const ClientCapabilities& granted) {
  ClientCapabilities result;
  result.space = true;  // always on
  result.observe = requested.observe && granted.observe;
  result.read = requested.read && granted.read;
  result.drive = requested.drive && granted.drive;
  result.keep_awake = requested.keep_awake && granted.keep_awake;
  return result;
}

bool IsValidClientId(std::string_view id) {
  return id.size() == kClientIdLength && std::ranges::all_of(id, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

ClientStore::~ClientStore() = default;

InMemoryClientStore::InMemoryClientStore() = default;
InMemoryClientStore::~InMemoryClientStore() = default;

std::optional<ClientRecord> InMemoryClientStore::Find(
    std::string_view id) const {
  const auto it = records_.find(id);
  if (it == records_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool InMemoryClientStore::Add(ClientRecord record) {
  if (!IsValidClientId(record.id) || records_.contains(record.id)) {
    return false;
  }
  std::string id = record.id;
  records_.emplace(std::move(id), std::move(record));
  return true;
}

bool InMemoryClientStore::UpdatePinned(std::string_view id,
                                       PinnedIdentity pinned) {
  const auto it = records_.find(id);
  if (it == records_.end()) {
    return false;
  }
  it->second.pinned = std::move(pinned);
  return true;
}

void InMemoryClientStore::Touch(std::string_view id, base::Time now) {
  const auto it = records_.find(id);
  if (it != records_.end()) {
    it->second.last_used = now;
  }
}

bool InMemoryClientStore::Remove(std::string_view id) {
  const auto it = records_.find(id);
  if (it == records_.end()) {
    return false;
  }
  records_.erase(it);
  return true;
}

std::vector<uint8_t> ProofMessage(std::string_view client_id,
                                  base::span<const uint8_t> nonce) {
  constexpr std::string_view kPrefix = "koan-agent-v1";
  std::vector<uint8_t> message(kPrefix.begin(), kPrefix.end());
  message.push_back(0);
  message.insert(message.end(), client_id.begin(), client_id.end());
  message.push_back(0);
  message.insert(message.end(), nonce.begin(), nonce.end());
  return message;
}

bool IsAcceptablePublicKey(base::span<const uint8_t> public_key) {
  if (public_key.size() != kPublicKeySize) {
    return false;
  }
  std::array<uint8_t, kPublicKeySize> y;
  std::ranges::copy(public_key, y.begin());
  y[31] &= 0x7f;  // the sign of x is not part of the point's order
  return !std::ranges::contains(kSmallOrderPoints, y);
}

std::vector<uint8_t> PairingMessage(base::span<const uint8_t> public_key,
                                    std::string_view label) {
  constexpr std::string_view kPrefix = "koan-agent-v1-pair";
  std::vector<uint8_t> message(kPrefix.begin(), kPrefix.end());
  message.push_back(0);
  message.insert(message.end(), public_key.begin(), public_key.end());
  message.push_back(0);
  message.insert(message.end(), label.begin(), label.end());
  return message;
}

bool VerifyPairingProof(base::span<const uint8_t> public_key,
                        std::string_view label,
                        base::span<const uint8_t> signature) {
  if (!IsAcceptablePublicKey(public_key) ||
      signature.size() != kSignatureSize) {
    return false;
  }
  const std::vector<uint8_t> message = PairingMessage(public_key, label);
  return ED25519_verify(message.data(), message.size(), signature.data(),
                        public_key.data()) == 1;
}

bool VerifyProof(base::span<const uint8_t> public_key,
                 std::string_view client_id,
                 base::span<const uint8_t> nonce,
                 base::span<const uint8_t> signature) {
  if (!IsAcceptablePublicKey(public_key) ||
      signature.size() != kSignatureSize || nonce.size() != kNonceSize) {
    return false;
  }
  const std::vector<uint8_t> message = ProofMessage(client_id, nonce);
  return ED25519_verify(message.data(), message.size(), signature.data(),
                        public_key.data()) == 1;
}

RateLimiter::RateLimiter() = default;
RateLimiter::~RateLimiter() = default;

bool RateLimiter::IsBlocked(const std::string& key, base::TimeTicks now) {
  const auto it = entries_.find(key);
  return it != entries_.end() && now < it->second.blocked_until;
}

RateLimiter::Entry& RateLimiter::EntryFor(const std::string& key,
                                          base::TimeTicks now) {
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    if (entries_.size() >= kMaxRateLimitEntries) {
      // Full: drop the least recently active entry, one that is not blocked
      // before one that is. A table that fails closed when full could be filled
      // by one process to deny every other peer.
      const auto victim = std::ranges::min_element(
          entries_, [now](const auto& a, const auto& b) {
            const bool a_blocked = now < a.second.blocked_until;
            const bool b_blocked = now < b.second.blocked_until;
            if (a_blocked != b_blocked) {
              return b_blocked;
            }
            return a.second.last_activity < b.second.last_activity;
          });
      entries_.erase(victim);
    }
    it = entries_.try_emplace(key).first;
  }
  it->second.last_activity = now;
  return it->second;
}

bool RateLimiter::RecordFailure(const std::string& key, base::TimeTicks now) {
  Entry& entry = EntryFor(key, now);
  if (now >= entry.blocked_until &&
      (entry.failures >= kMaxFailures ||
       now - entry.last_failure > kFailureWindow)) {
    entry.failures = 0;  // the block ran out, or the failures are old
  }
  entry.last_failure = now;
  if (++entry.failures >= kMaxFailures) {
    entry.blocked_until = now + kBlockDuration;
    return true;
  }
  return false;
}

void RateLimiter::RecordSuccess(const std::string& key) {
  const auto it = entries_.find(key);
  if (it != entries_.end()) {
    it->second.failures = 0;
  }
}

bool RateLimiter::TryBeginPrompt(const std::string& key, base::TimeTicks now) {
  const auto old = [now](base::TimeTicks time) {
    return now - time >= base::Minutes(1);
  };
  if (open_prompts_ >= kMaxOpenPrompts) {
    return false;
  }
  for (auto& item : user_prompts_) {
    std::erase_if(item.second, old);
  }
  std::erase_if(user_prompts_,
                [](const auto& item) { return item.second.empty(); });
  if (user_prompts_.size() >= kMaxRateLimitEntries &&
      !user_prompts_.contains(key)) {
    return false;
  }
  std::vector<base::TimeTicks>& times = user_prompts_[key];
  if (static_cast<int>(times.size()) >= kMaxPromptsPerMinute) {
    return false;
  }
  times.push_back(now);
  ++open_prompts_;
  return true;
}

void RateLimiter::EndPrompt() {
  if (open_prompts_ > 0) {
    --open_prompts_;
  }
}

}  // namespace koan::agent
