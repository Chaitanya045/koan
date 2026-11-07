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
// Entries of the rate limiter beyond which idle ones are dropped.
constexpr size_t kMaxRateLimitEntries = 256;

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
  if (!pinned.agent.path.empty() && !ChainHas(peer.ancestors, pinned.agent)) {
    return IdentityChange::kChainChanged;
  }
  return IdentityChange::kNone;
}

std::string PeerKey(const PeerIdentity& peer) {
  return base::NumberToString(peer.uid) + ":" + peer.process.path + ":" +
         peer.process.signing_identity;
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

bool VerifyProof(base::span<const uint8_t> public_key,
                 std::string_view client_id,
                 base::span<const uint8_t> nonce,
                 base::span<const uint8_t> signature) {
  if (public_key.size() != kPublicKeySize ||
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

RateLimiter::Entry* RateLimiter::EntryFor(const std::string& key,
                                          base::TimeTicks now) {
  const auto existing = entries_.find(key);
  if (existing != entries_.end()) {
    return &existing->second;
  }
  if (entries_.size() >= kMaxRateLimitEntries) {
    // Drop the entries that are neither blocked nor recently active.
    std::erase_if(entries_, [now](const auto& item) {
      const Entry& entry = item.second;
      return entry.blocked_until <= now && entry.failures == 0 &&
             std::ranges::all_of(entry.pairings, [now](base::TimeTicks time) {
               return now - time >= base::Minutes(1);
             });
    });
    if (entries_.size() >= kMaxRateLimitEntries) {
      return nullptr;
    }
  }
  return &entries_[key];
}

bool RateLimiter::RecordFailure(const std::string& key, base::TimeTicks now) {
  Entry* entry = EntryFor(key, now);
  if (!entry) {
    // Everything is in use: a peer that cannot be tracked is blocked.
    return true;
  }
  if (now >= entry->blocked_until && entry->failures >= kMaxFailures) {
    entry->failures = 0;  // the block ran out
  }
  if (++entry->failures >= kMaxFailures) {
    entry->blocked_until = now + kBlockDuration;
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

bool RateLimiter::AllowPairing(const std::string& key, base::TimeTicks now) {
  Entry* entry = EntryFor(key, now);
  if (!entry) {
    return false;
  }
  std::erase_if(entry->pairings, [now](base::TimeTicks time) {
    return now - time >= base::Minutes(1);
  });
  if (static_cast<int>(entry->pairings.size()) >= kMaxPairingsPerMinute) {
    return false;
  }
  entry->pairings.push_back(now);
  return true;
}

}  // namespace koan::agent
