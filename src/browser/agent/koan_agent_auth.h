// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_AUTH_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_AUTH_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"

// The pieces of agent control's authentication (.agent/notes/agent-control.md
// section 5), without any socket, keychain or UI: who the peer is, what a
// paired client is, the proof it signs, and the limits on guessing. The
// session state machine (koan_agent_session.h) is built from these.
namespace koan::agent {

inline constexpr size_t kPublicKeySize = 32;
inline constexpr size_t kSignatureSize = 64;
inline constexpr size_t kNonceSize = 32;
// A client id is 16 random bytes in lower-case hex.
inline constexpr size_t kClientIdLength = 32;

// How long a paired client stays valid without being used.
inline constexpr base::TimeDelta kClientExpiry = base::Days(30);

// --- Who the peer is ---------------------------------------------------------

// A process as the OS describes it: filled in by the socket layer from the
// peer's audit token (never from anything the client says).
struct ProcessInfo {
  std::string path;
  // The code-signing identity (team id or designated requirement); empty for an
  // unsigned executable.
  std::string signing_identity;

  bool operator==(const ProcessInfo&) const = default;
};

struct PeerIdentity {
  uint32_t uid = 0;
  // The process on the other end of the socket: the `koan` CLI or the relay.
  ProcessInfo process;
  // Its parents, nearest first.
  std::vector<ProcessInfo> ancestors;
};

// What a pairing records about the peer, and every later connection is compared
// with: the connecting process and the agent that started it (the first
// ancestor that is not a shell or a wrapper).
struct PinnedIdentity {
  ProcessInfo process;
  ProcessInfo agent;

  bool operator==(const PinnedIdentity&) const = default;
};

// The first ancestor that is not a shell, `koan` itself or a common wrapper;
// empty if there is none. A heuristic that makes a change visible: a process
// can start `koan` from whatever chain it likes.
ProcessInfo AgentAncestor(const PeerIdentity& peer);

PinnedIdentity PinPeer(const PeerIdentity& peer);

enum class IdentityChange {
  kNone,
  // The connecting executable, or its signature, is not the paired one.
  kProcessChanged,
  // The agent that was paired is not among the ancestors any more.
  kChainChanged,
};
// A pin with no agent in it means the chain was unknown when the client paired
// (an orphaned process, an unreadable chain); a connection that has an agent
// now is a chain change, so an empty pin never switches the chain check off.
IdentityChange CompareIdentity(const PinnedIdentity& pinned,
                               const PeerIdentity& peer);

// A key for rate limits: the same executable by the same user.
std::string PeerKey(const PeerIdentity& peer);

// A key for limits an attacker must not be able to dodge by changing the path
// of the program it runs: the user.
std::string UidKey(const PeerIdentity& peer);

// A digest of the ancestors, for remembering that the user accepted a chain.
std::string ChainDigest(const PeerIdentity& peer);

// --- What a paired client is -------------------------------------------------

struct ClientCapabilities {
  // Open and drive tabs in its own agent Space: always on.
  bool space = true;
  // List the user's tabs (titles and origins).
  bool observe = false;
  // Snapshot or screenshot the user's current tab.
  bool read = false;
  // Take over a user's tab, each with its own approval.
  bool drive = false;
  // Prevent display sleep for an unattended run.
  bool keep_awake = false;

  bool operator==(const ClientCapabilities&) const = default;
};

// {"space", "observe", "read", "drive", "keep-awake"}. nullopt for an unknown
// name, a non-string, or more than a handful of entries. `space` is always on.
std::optional<ClientCapabilities> ParseCapabilities(
    const std::vector<std::string>& names);
std::vector<std::string> CapabilityNames(
    const ClientCapabilities& capabilities);

// What the user approved at pairing: no more than was requested.
ClientCapabilities Intersect(const ClientCapabilities& requested,
                             const ClientCapabilities& granted);

struct ClientRecord {
  // Random, 32 lower-case hex characters. Never the label: the label is for
  // display, and a name must not become a path.
  std::string id;
  std::string label;
  // Ed25519, 32 bytes. The browser keeps no secret.
  std::vector<uint8_t> public_key;
  ClientCapabilities capabilities;
  PinnedIdentity pinned;
  base::Time created;
  base::Time last_used;
};

bool IsValidClientId(std::string_view id);

// Where paired clients are kept. The production store encrypts and
// authenticates the records with os_crypt (a later change); this is the
// contract the session needs.
class ClientStore {
 public:
  virtual ~ClientStore();

  virtual std::optional<ClientRecord> Find(std::string_view id) const = 0;
  // False if it could not be stored.
  virtual bool Add(ClientRecord record) = 0;
  virtual bool UpdatePinned(std::string_view id, PinnedIdentity pinned) = 0;
  virtual void Touch(std::string_view id, base::Time now) = 0;
  // Must be durable before it returns.
  virtual bool Remove(std::string_view id) = 0;
};

// For tests and fuzzers.
class InMemoryClientStore final : public ClientStore {
 public:
  InMemoryClientStore();
  ~InMemoryClientStore() override;

  std::optional<ClientRecord> Find(std::string_view id) const override;
  bool Add(ClientRecord record) override;
  bool UpdatePinned(std::string_view id, PinnedIdentity pinned) override;
  void Touch(std::string_view id, base::Time now) override;
  bool Remove(std::string_view id) override;

  size_t size() const { return records_.size(); }

 private:
  std::map<std::string, ClientRecord, std::less<>> records_;
};

// --- The proof
// ----------------------------------------------------------------

// The bytes a client signs to authenticate: "koan-agent-v1", a NUL, the client
// id, a NUL, and the 32-byte nonce the browser just gave it. (The peer's
// identity is not in it: the client cannot know how the browser sees it, and
// the browser checks it from the OS instead.)
std::vector<uint8_t> ProofMessage(std::string_view client_id,
                                  base::span<const uint8_t> nonce);

// Whether `public_key` may be paired: 32 bytes that are not one of the points
// of small order, for which anyone can forge a signature.
bool IsAcceptablePublicKey(base::span<const uint8_t> public_key);

// What a client signs when it pairs, to show it holds the key it offers:
// "koan-agent-v1-pair", a NUL, the 32-byte public key, a NUL, the label.
std::vector<uint8_t> PairingMessage(base::span<const uint8_t> public_key,
                                    std::string_view label);
bool VerifyPairingProof(base::span<const uint8_t> public_key,
                        std::string_view label,
                        base::span<const uint8_t> signature);

// Whether `signature` (64 bytes) is `public_key`'s Ed25519 signature of
// ProofMessage(client_id, nonce). False for a key IsAcceptablePublicKey()
// refuses.
bool VerifyProof(base::span<const uint8_t> public_key,
                 std::string_view client_id,
                 base::span<const uint8_t> nonce,
                 base::span<const uint8_t> signature);

// --- Limits on guessing
// --------------------------------------------------------

// Failed authentications per peer key, and the prompts shown to the user. A
// peer with kMaxFailures failures within kFailureWindow is blocked for
// kBlockDuration. A failure older than the window is forgotten. Prompts of
// every kind are limited per user (the key passed in is UidKey(), not
// something an attacker can vary) and to one open at a time. There is no
// separate cap over all users: sessions of other users are refused before they
// can ask (Session::CheckPeer), so every prompt is the browser's own user's and
// the per-user cap is the global one. Time comes from the caller. The table is
// bounded: when it is full the least recently active entry (an unblocked one
// first) is dropped, so it cannot be filled to deny service to a legitimate
// client.
//
// Accepted risk: a process of the browser's own user can use up the prompt
// budget (one open, three a minute) with pairing offers it signs itself, which
// delays or rate-limits a legitimate client's prompt and shows the user
// nuisance dialogs. It gains no access by it: every prompt names the verified
// program, every refusal and timeout counts against that program, and nothing
// is granted without the user. Without the budget the same process could ask as
// often as it likes, which is the worse case.
class RateLimiter {
 public:
  static constexpr int kMaxFailures = 5;
  static constexpr base::TimeDelta kBlockDuration = base::Seconds(60);
  static constexpr base::TimeDelta kFailureWindow = base::Minutes(5);
  static constexpr int kMaxPromptsPerMinute = 3;
  static constexpr int kMaxOpenPrompts = 1;

  RateLimiter();
  ~RateLimiter();

  bool IsBlocked(const std::string& key, base::TimeTicks now);
  // Counts a failure; returns true if it blocks the peer.
  bool RecordFailure(const std::string& key, base::TimeTicks now);
  void RecordSuccess(const std::string& key);
  // Starts a prompt for `key` (a UidKey()); false, and nothing counted, if one
  // is already open or if the user has had too many in the last minute. Every
  // true needs an EndPrompt().
  bool TryBeginPrompt(const std::string& key, base::TimeTicks now);
  void EndPrompt();

 private:
  struct Entry {
    int failures = 0;
    base::TimeTicks last_failure;
    base::TimeTicks blocked_until;
    base::TimeTicks last_activity;
  };
  // The entry for a key; makes room by dropping the least recently active one.
  Entry& EntryFor(const std::string& key, base::TimeTicks now);

  std::map<std::string, Entry> entries_;
  // Prompts shown, per user. Kept apart from the failure table: a
  // process cannot push a user's count out by making the table churn, and the
  // keys are users, which it cannot invent.
  std::map<std::string, std::vector<base::TimeTicks>> user_prompts_;
  int open_prompts_ = 0;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUTH_H_
