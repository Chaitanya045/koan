// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_session.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "third_party/icu/source/common/unicode/uchar.h"

namespace koan::agent {

namespace {

constexpr size_t kMaxLabelCharacters = 64;
// Four bytes per character at most.
constexpr size_t kMaxLabelBytes = 4 * kMaxLabelCharacters;

// How often an authenticated session refreshes the client's last use.
constexpr base::TimeDelta kTouchInterval = base::Hours(1);

// Approval ids are unique in the process, not per session.
std::atomic<uint64_t> g_next_approval_id{1};

bool IsExpired(const ClientRecord& record, base::Time now) {
  const base::Time last_used =
      record.last_used.is_null() ? record.created : record.last_used;
  return now - last_used > kClientExpiry;
}

// The error code of a JSON-RPC style "server error"; the Koan code is in
// `data`.
constexpr int kServerError = -32000;

// What a label may be made of: printable ASCII, and beyond ASCII only letters
// (L*) and decimal digits (Nd). That leaves out everything that can hide,
// reorder or blank out text around it: controls (Cc, U+0085 included), format
// characters (Cf: bidi overrides and isolates, zero-width), line and paragraph
// separators, other spaces (the no-break space), combining marks (M*), private
// use (Co) and symbols (So), which takes in the braille blank U+2800. The
// Hangul fillers are letters, so the default-ignorable code points are refused
// first. (Look-alike letters from another script are not caught: the label is
// only ever shown as "calls itself", after the verified program.)
bool IsAllowedLabelCharacter(base_icu::UChar32 character) {
  if (character < 0x80) {
    return character > 0x20 && character < 0x7f;  // space is handled apart
  }
  if (u_hasBinaryProperty(character, UCHAR_DEFAULT_IGNORABLE_CODE_POINT)) {
    return false;
  }
  switch (u_charType(character)) {
    case U_UPPERCASE_LETTER:
    case U_LOWERCASE_LETTER:
    case U_TITLECASE_LETTER:
    case U_MODIFIER_LETTER:
    case U_OTHER_LETTER:
    case U_DECIMAL_DIGIT_NUMBER:
      return true;
    default:
      return false;
  }
}

// The label is shown in the pairing dialog next to the verified program and
// written to a log. Valid UTF-8, 1 to 64 characters of
// IsAllowedLabelCharacter() or an ASCII space, no leading or trailing space and
// no run of spaces.
bool IsValidLabel(const std::string& label) {
  if (label.empty() || label.size() > kMaxLabelBytes) {
    return false;
  }
  size_t characters = 0;
  bool previous_space = false;
  for (size_t i = 0; i < label.size(); ++i) {
    base_icu::UChar32 character = 0;
    if (!base::ReadUnicodeCharacter(label, &i, &character)) {
      return false;
    }
    ++characters;
    const bool space = character == ' ';
    if (space ? (characters == 1 || previous_space)
              : !IsAllowedLabelCharacter(character)) {
      return false;
    }
    previous_space = space;
  }
  return characters <= kMaxLabelCharacters && !previous_space;
}

base::ListValue ToList(const std::vector<std::string>& names) {
  base::ListValue list;
  for (const std::string& name : names) {
    list.Append(name);
  }
  return list;
}

void DefaultRandomBytes(base::span<uint8_t> output) {
  base::RandBytes(output);
}

}  // namespace

struct Session::Pending {
  uint64_t approval_id = 0;
  ApprovalKind kind = ApprovalKind::kNewClient;
  int message_id = 0;
  base::TimeTicks deadline;
  // A new client.
  std::vector<uint8_t> public_key;
  std::string label;
  ClientCapabilities requested;
  // A change, and why it is asked about (ReapprovalReason()).
  ClientRecord record;
  std::string reason;
};

namespace {

// A program's file name, as plain text: at most 64 printable ASCII
// characters (it goes to the client's terminal).
std::string ProgramName(const ProcessInfo& process) {
  if (process.path.empty()) {
    return "no known program";
  }
  std::string_view name = process.path;
  if (const size_t slash = name.find_last_of('/');
      slash != std::string_view::npos) {
    name.remove_prefix(slash + 1);
  }
  std::string out;
  for (char c : name.substr(0, 64)) {
    out += (c >= 0x20 && c < 0x7f) ? c : '?';
  }
  return out.empty() ? "an unnamed program" : out;
}

}  // namespace

std::string ReapprovalReason(ApprovalKind kind,
                             const PinnedIdentity& previous,
                             const PeerIdentity& peer) {
  switch (kind) {
    case ApprovalKind::kNewClient:
      return std::string();
    case ApprovalKind::kPeerChanged:
      return "this client connects from " + ProgramName(peer.process) +
             ", not the " + ProgramName(previous.process) +
             " that paired it (or its signature changed), so Koan asks the "
             "user again";
    case ApprovalKind::kChainChanged:
      return "this koan was started by " + ProgramName(AgentAncestor(peer)) +
             ", not by the agent that paired it (" +
             ProgramName(previous.agent) +
             "): the pairing pins the agent, so Koan asks the user again "
             "(once while Koan runs)";
  }
  return std::string();
}

SessionDelegate::~SessionDelegate() = default;

bool SessionDelegate::Forward(AgentMessage message) {
  return false;
}

AgentMessage::AgentMessage() = default;
AgentMessage::AgentMessage(AgentMessage&&) = default;
AgentMessage& AgentMessage::operator=(AgentMessage&&) = default;
AgentMessage::~AgentMessage() = default;

SessionEnvironment::SessionEnvironment()
    : random_bytes(base::BindRepeating(&DefaultRandomBytes)) {}
SessionEnvironment::SessionEnvironment(const SessionEnvironment&) = default;
SessionEnvironment& SessionEnvironment::operator=(const SessionEnvironment&) =
    default;
SessionEnvironment::~SessionEnvironment() = default;

Session::Session(PeerIdentity peer,
                 SessionEnvironment environment,
                 SessionDelegate* delegate)
    : peer_(std::move(peer)),
      peer_key_(PeerKey(peer_)),
      env_(std::move(environment)),
      delegate_(delegate) {
  created_ = Now();
}

Session::~Session() {
  ReleasePrompt();
}

base::TimeTicks Session::Now() const {
  return env_.tick_clock->NowTicks();
}

void Session::OnBytesReceived(base::span<const uint8_t> bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == State::kClosed) {
    return;
  }
  if (state_ != State::kAuthenticated &&
      env_.limiter->IsBlocked(peer_key_, Now())) {
    Close(CloseReason::kBlocked);
    return;
  }
  if (!decoder_.Append(bytes)) {
    Close(CloseReason::kProtocolViolation);
    return;
  }
  Drain();
}

void Session::Drain() {
  // While a prompt is open nothing else is taken: what the peer pipelined stays
  // buffered (and bounded) until the user has answered.
  while (state_ != State::kClosed && state_ != State::kAwaitingApproval) {
    std::optional<std::vector<uint8_t>> frame = decoder_.TakeFrame();
    if (!frame) {
      break;
    }
    HandleFrame(*frame);
  }
  if (state_ == State::kClosed) {
    return;
  }
  if (decoder_.failed()) {
    Close(CloseReason::kProtocolViolation);
    return;
  }
  if (decoder_.has_partial_frame()) {
    if (partial_since_.is_null()) {
      partial_since_ = Now();
    }
  } else {
    partial_since_ = base::TimeTicks();
  }
}

void Session::HandleFrame(base::span<const uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()),
                              payload.size());
  std::optional<base::DictValue> message =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  if (!message) {
    Close(CloseReason::kProtocolViolation);
    return;
  }
  const std::optional<int> id = message->FindInt("id");
  const std::string* method = message->FindString("method");
  if (!id || *id < 0 || !method) {
    Close(CloseReason::kProtocolViolation);
    return;
  }
  const base::DictValue* params = nullptr;
  if (message->contains("params")) {
    params = message->FindDict("params");
    if (!params) {
      Close(CloseReason::kProtocolViolation);
      return;
    }
  }
  const std::string* session_id = nullptr;
  if (message->contains("sessionId")) {
    session_id = message->FindString("sessionId");
    if (!session_id) {
      Close(CloseReason::kProtocolViolation);
      return;
    }
  }
  const base::DictValue none;
  if (state_ == State::kAuthenticated) {
    HandleAuthenticated(*id, *method, params, session_id);
  } else {
    HandleUnauthenticated(*id, *method, params ? *params : none);
  }
}

void Session::HandleUnauthenticated(int id,
                                    const std::string& method,
                                    const base::DictValue& params) {
  if (method == "Koan.pair" || method == "Koan.authenticate") {
    if (!CheckPeer()) {
      return;
    }
    if (method == "Koan.pair") {
      HandlePair(id, params);
    } else {
      HandleAuthenticate(id, params);
    }
  } else {
    // Before authentication nothing else is parsed or answered.
    ReplyError(id, "Koan.notAuthenticated",
               "authenticate first (Koan.pair or Koan.authenticate)");
    CountFailure();
  }
}

bool Session::CheckPeer() {
  // The socket layer fills the identity in from the OS. If it could not (a
  // process that is gone, a failed lookup) it is not a peer to pair or to
  // match against a pin, and a peer of another user is not ours to serve.
  if (!env_.browser_uid || peer_.uid != *env_.browser_uid ||
      peer_.process.path.empty()) {
    Close(CloseReason::kUnknownPeer);
    return false;
  }
  // And it must still be that peer: a dialog built for a program that exec'd
  // another since it connected would show the user the wrong one.
  if (!delegate_->PeerUnchanged()) {
    Close(CloseReason::kUnknownPeer);
    return false;
  }
  return true;
}

void Session::HandlePair(int id, const base::DictValue& params) {
  const std::string* encoded_key = params.FindString("publicKey");
  const std::string* label = params.FindString("label");
  const std::string* encoded_proof = params.FindString("proof");
  std::optional<std::vector<uint8_t>> key;
  if (encoded_key) {
    key = base::Base64Decode(*encoded_key);
  }
  std::optional<std::vector<uint8_t>> proof;
  if (encoded_proof) {
    proof = base::Base64Decode(*encoded_proof);
  }
  std::vector<std::string> names;
  if (const base::Value* value = params.Find("capabilities")) {
    const base::ListValue* list = value->GetIfList();
    if (!list) {
      Fail(id, "Koan.invalidParams", "capabilities must be a list");
      return;
    }
    for (const base::Value& entry : *list) {
      if (!entry.is_string()) {
        Fail(id, "Koan.invalidParams", "capabilities must be names");
        return;
      }
      names.push_back(entry.GetString());
    }
  }
  const std::optional<ClientCapabilities> requested = ParseCapabilities(names);
  if (!key || key->size() != kPublicKeySize || !label ||
      !IsValidLabel(*label) || !requested || !proof) {
    Fail(id, "Koan.invalidParams",
         "publicKey must be 32 bytes in base64, label 1 to 64 printable "
         "characters, capabilities known names, proof a signature");
    return;
  }
  // The client shows that it holds the private key before the user is asked
  // about it, and a key that anyone could sign for is not a key.
  if (!VerifyPairingProof(*key, *label, *proof)) {
    Fail(id, "Koan.invalidProof", "the pairing proof did not verify");
    return;
  }
  ApprovalRequest request;
  request.kind = ApprovalKind::kNewClient;
  request.peer = peer_;
  request.claimed_label = *label;
  request.requested = *requested;
  Pending pending;
  pending.kind = ApprovalKind::kNewClient;
  pending.message_id = id;
  pending.public_key = std::move(*key);
  pending.label = *label;
  pending.requested = *requested;
  Ask(id, std::move(request), std::move(pending));
}

void Session::HandleAuthenticate(int id, const base::DictValue& params) {
  const std::string* client_id = params.FindString("clientId");
  if (!client_id || !IsValidClientId(*client_id)) {
    Fail(id, "Koan.invalidParams", "clientId must be 32 lower-case hex digits");
    return;
  }
  if (params.contains("signature")) {
    FinishProof(id, params);
    return;
  }
  // A nonce for any well-formed id, known or not, paired or expired: what
  // happens to the id is decided at the proof, with one answer for every way
  // it can fail.
  BeginProof(id, *client_id);
}

void Session::BeginProof(int id, const std::string& client_id) {
  nonce_.assign(kNonceSize, 0);
  env_.random_bytes.Run(nonce_);
  nonce_client_id_ = client_id;
  nonce_deadline_ = Now() + kNonceLifetime;
  state_ = State::kAwaitingProof;
  base::DictValue result;
  result.Set("nonce", base::Base64Encode(nonce_));
  Reply(id, std::move(result));
}

void Session::FinishProof(int id, const base::DictValue& params) {
  // The socket outlives an exec of its peer: check who is there now, before
  // anything is verified or granted on the strength of who connected.
  if (!delegate_->PeerUnchanged()) {
    Close(CloseReason::kUnknownPeer);
    return;
  }
  // A nonce is good for one attempt, whatever that attempt's outcome.
  std::vector<uint8_t> nonce = std::move(nonce_);
  nonce_.clear();
  const std::string expected_client = std::move(nonce_client_id_);
  nonce_client_id_.clear();
  const bool fresh = state_ == State::kAwaitingProof && !nonce.empty() &&
                     Now() <= nonce_deadline_;
  state_ = State::kUnauthenticated;

  const std::string* client_id = params.FindString("clientId");
  const std::string* encoded = params.FindString("signature");
  std::optional<std::vector<uint8_t>> signature;
  if (encoded) {
    signature = base::Base64Decode(*encoded);
  }
  std::optional<ClientRecord> record;
  if (fresh && client_id && *client_id == expected_client) {
    record = env_.store->Find(*client_id);
    if (record && IsExpired(*record, env_.clock->Now())) {
      record.reset();
    }
  }
  if (!record || !signature ||
      !VerifyProof(record->public_key, record->id, nonce, *signature)) {
    Fail(id, "Koan.authFailed", "the proof did not verify");
    return;
  }

  // Only now, with the key proved, is the user asked about a program or a
  // chain that is not the one that paired. Nothing is pinned until they say
  // yes.
  const IdentityChange change = CompareIdentity(record->pinned, peer_);
  const std::string chain_key = record->id + "|" + ChainDigest(peer_);
  if (change == IdentityChange::kProcessChanged ||
      (change == IdentityChange::kChainChanged &&
       !env_.accepted_chains->contains(chain_key))) {
    ApprovalRequest request;
    request.kind = change == IdentityChange::kProcessChanged
                       ? ApprovalKind::kPeerChanged
                       : ApprovalKind::kChainChanged;
    request.peer = peer_;
    request.client_id = record->id;
    request.previous = record->pinned;
    Pending pending;
    pending.kind = request.kind;
    pending.message_id = id;
    pending.record = *record;
    pending.reason = ReapprovalReason(request.kind, record->pinned, peer_);
    const std::string reason = pending.reason;
    if (Ask(id, std::move(request), std::move(pending))) {
      // The client waits for the answer: it is told why, so that it can tell
      // its user where to look.
      base::DictValue why;
      why.Set("reason", change == IdentityChange::kProcessChanged
                            ? "programChanged"
                            : "chainChanged");
      why.Set("message", reason);
      base::DictValue event;
      event.Set("method", "Koan.approvalPending");
      event.Set("params", std::move(why));
      Send(event);
    }
    return;
  }
  CompleteAuthentication(id, *record);
}

void Session::CompleteAuthentication(int id, const ClientRecord& record) {
  // Nothing received before this point may be handled as authenticated: a peer
  // that pipelined a command behind its proof is cut off.
  if (!decoder_.OnAuthenticated()) {
    Close(CloseReason::kProtocolViolation);
    return;
  }
  env_.limiter->RecordSuccess(peer_key_);
  env_.store->Touch(record.id, env_.clock->Now());
  client_id_ = record.id;
  capabilities_ = record.capabilities;
  state_ = State::kAuthenticated;
  base::DictValue result;
  result.Set("authenticated", true);
  result.Set("clientId", client_id_);
  result.Set("capabilities", ToList(CapabilityNames(capabilities_)));
  Reply(id, std::move(result));
}

bool Session::StillPaired() {
  const std::optional<ClientRecord> record = env_.store->Find(client_id_);
  if (!record) {
    Close(CloseReason::kRevoked);
    return false;
  }
  const base::Time now = env_.clock->Now();
  if (IsExpired(*record, now)) {
    Close(CloseReason::kExpired);
    return false;
  }
  // What the user allows now, not what they allowed when this connected.
  capabilities_ = record->capabilities;
  // A session that stays connected keeps its pairing alive.
  const base::Time last_used =
      record->last_used.is_null() ? record->created : record->last_used;
  if (now - last_used > kTouchInterval) {
    env_.store->Touch(record->id, now);
  }
  return true;
}

void Session::OnClientRevoked(std::string_view client_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == State::kAuthenticated && client_id_ == client_id) {
    Close(CloseReason::kRevoked);
  } else if (state_ == State::kAwaitingApproval && pending_ &&
             pending_->record.id == client_id) {
    Close(CloseReason::kRevoked);
  }
}

void Session::HandleAuthenticated(int id,
                                  const std::string& method,
                                  const base::DictValue* params,
                                  const std::string* session_id) {
  if (!StillPaired()) {
    return;
  }
  if (method == "Koan.status") {
    base::DictValue result;
    result.Set("clientId", client_id_);
    result.Set("capabilities", ToList(CapabilityNames(capabilities_)));
    Reply(id, std::move(result));
  } else if (method == "Koan.pair" || method == "Koan.authenticate") {
    Close(CloseReason::kProtocolViolation);
  } else {
    AgentMessage message;
    message.id = id;
    message.method = method;
    if (params) {
      message.params = params->Clone();
    }
    if (session_id) {
      message.session_id = *session_id;
    }
    message.client_id = client_id_;
    message.capabilities = capabilities_;
    if (!delegate_->Forward(std::move(message))) {
      ReplyError(id, "Koan.unavailable",
                 "this build does not forward commands to tabs");
    }
  }
}

bool Session::Ask(int id, ApprovalRequest request, Pending pending) {
  // At most one prompt is open in the whole browser, and the user is not asked
  // more than a few times a minute, whatever the peer's path says.
  if (!env_.limiter->TryBeginPrompt(UidKey(peer_), Now())) {
    ReplyError(id, "Koan.rateLimited", "too many requests for approval");
    return false;
  }
  holds_prompt_ = true;
  prompt_open_since_ = Now();
  pending.approval_id = g_next_approval_id.fetch_add(1);
  pending.deadline = Now() + kApprovalTimeout;
  request.id = pending.approval_id;
  state_ = State::kAwaitingApproval;
  pending_ = std::make_unique<Pending>(std::move(pending));
  delegate_->RequestApproval(request);
  return true;
}

void Session::ReleasePrompt() {
  if (holds_prompt_) {
    holds_prompt_ = false;
    env_.limiter->EndPrompt();
  }
}

void Session::CountDenial() {
  // A refusal or a prompt left unanswered costs the peer a failure like a
  // wrong proof does, so asking the user over and over wears the peer out, not
  // the user.
  env_.limiter->RecordFailure(peer_key_, Now());
}

void Session::OnApprovalDecision(uint64_t request_id,
                                 ApprovalDecision decision) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kAwaitingApproval || !pending_ ||
      pending_->approval_id != request_id) {
    return;
  }
  std::unique_ptr<Pending> pending = std::move(pending_);
  state_ = State::kUnauthenticated;
  ReleasePrompt();
  prompt_total_ += Now() - prompt_open_since_;
  prompt_open_since_ = base::TimeTicks();
  // The dialog may have been open for a minute, and the peer can have become
  // another program in that time: the user answered about the one that asked.
  // An approval is applied only if that is still who is there.
  if (decision.approved && !delegate_->PeerUnchanged()) {
    ReplyError(pending->message_id, "Koan.peerChanged",
               "the program that asked is not the one connected any more");
    CountDenial();
    Close(CloseReason::kUnknownPeer);
    return;
  }
  if (!decision.approved) {
    ReplyError(pending->message_id, "Koan.pairingDenied",
               pending->reason.empty()
                   ? std::string("the user did not approve")
                   : "the user did not approve: " + pending->reason);
    CountDenial();
    Close(CloseReason::kPairingDenied);
    return;
  }
  if (pending->kind == ApprovalKind::kNewClient) {
    ClientRecord record;
    record.id = NewClientId();
    record.label = pending->label;
    record.public_key = std::move(pending->public_key);
    record.capabilities = Intersect(pending->requested, decision.granted);
    record.pinned = PinPeer(peer_);
    record.created = env_.clock->Now();
    record.last_used = record.created;
    base::DictValue result;
    result.Set("clientId", record.id);
    result.Set("capabilities", ToList(CapabilityNames(record.capabilities)));
    if (!env_.store->Add(std::move(record))) {
      ReplyError(pending->message_id, "Koan.storeFailed",
                 "the pairing could not be saved");
    } else {
      Reply(pending->message_id, std::move(result));
    }
  } else {
    // The proof was verified before the question was asked; the client may have
    // been revoked while the dialog was open.
    std::optional<ClientRecord> record = env_.store->Find(pending->record.id);
    if (!record || IsExpired(*record, env_.clock->Now())) {
      Fail(pending->message_id, "Koan.authFailed", "the proof did not verify");
      Drain();
      return;
    }
    if (pending->kind == ApprovalKind::kPeerChanged) {
      if (!env_.store->UpdatePinned(record->id, PinPeer(peer_))) {
        ReplyError(pending->message_id, "Koan.storeFailed",
                   "the new identity could not be saved");
        Drain();
        return;
      }
      record->pinned = PinPeer(peer_);
    } else {
      env_.accepted_chains->insert(record->id + "|" + ChainDigest(peer_));
    }
    CompleteAuthentication(pending->message_id, *record);
  }
  Drain();
}

void Session::CheckTimeouts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == State::kClosed) {
    return;
  }
  if (state_ == State::kAuthenticated && !StillPaired()) {
    return;
  }
  const base::TimeTicks now = Now();
  if (state_ == State::kAwaitingProof && now > nonce_deadline_) {
    nonce_.clear();
    nonce_client_id_.clear();
    state_ = State::kUnauthenticated;
  }
  if (state_ == State::kAwaitingApproval && pending_ &&
      now > pending_->deadline) {
    ReplyError(pending_->message_id, "Koan.pairingTimeout",
               pending_->reason.empty()
                   ? std::string("the user did not answer in time")
                   : "the user did not answer in time: " + pending_->reason +
                         "; ask them to approve it in Koan, or run koan "
                         "from the agent that paired");
    CountDenial();
    Close(CloseReason::kTimeout);
    return;
  }
  if (state_ != State::kAuthenticated) {
    base::TimeDelta in_prompt = prompt_total_;
    if (state_ == State::kAwaitingApproval && !prompt_open_since_.is_null()) {
      in_prompt += now - prompt_open_since_;
    }
    if (now - created_ - in_prompt > kPreAuthDeadline) {
      CountDenial();
      Close(CloseReason::kTimeout);
      return;
    }
  }
  if (!partial_since_.is_null() && now - partial_since_ > kFrameTimeout) {
    // A stall is a failure of a peer that has not shown it is anyone yet. After
    // the proof it is the client's own slow connection, not guessing.
    if (state_ != State::kAuthenticated) {
      CountDenial();
    }
    Close(CloseReason::kTimeout);
  }
}

void Session::Fail(int id,
                   const std::string& code,
                   const std::string& message) {
  ReplyError(id, code, message);
  CountFailure();
}

void Session::CountFailure() {
  if (env_.limiter->RecordFailure(peer_key_, Now())) {
    Close(CloseReason::kTooManyFailures);
  }
}

void Session::Reply(int id, base::DictValue result) {
  base::DictValue message;
  message.Set("id", id);
  message.Set("result", std::move(result));
  Send(message);
}

void Session::ReplyError(int id,
                         const std::string& code,
                         const std::string& message_text) {
  base::DictValue error;
  error.Set("code", kServerError);
  error.Set("message", message_text);
  error.Set("data", code);
  base::DictValue message;
  message.Set("id", id);
  message.Set("error", std::move(error));
  Send(message);
}

void Session::Send(const base::DictValue& message) {
  if (state_ == State::kClosed) {
    return;
  }
  const std::optional<std::string> json = base::WriteJson(message);
  if (!json) {
    return;
  }
  std::optional<std::vector<uint8_t>> frame =
      EncodeFrame(base::as_byte_span(*json));
  if (frame) {
    delegate_->Send(std::move(*frame));
  }
}

void Session::Close(CloseReason reason) {
  if (state_ == State::kClosed) {
    return;
  }
  const bool was_authenticated = state_ == State::kAuthenticated;
  state_ = State::kClosed;
  nonce_.clear();
  pending_.reset();
  ReleasePrompt();
  // Garbage, an oversize frame or a second authentication from a peer that has
  // not proved who it is costs it a failure like a wrong proof, so reconnecting
  // is not free.
  if (reason == CloseReason::kProtocolViolation && !was_authenticated) {
    CountDenial();
  }
  delegate_->Close(reason);
}

std::string Session::NewClientId() {
  std::array<uint8_t, kClientIdLength / 2> bytes;
  env_.random_bytes.Run(bytes);
  return base::HexEncodeLower(bytes);
}

}  // namespace koan::agent
