// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_session.h"

#include <array>
#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"

namespace koan::agent {

namespace {

constexpr size_t kMaxLabelLength = 64;

// The error code of a JSON-RPC style "server error"; the Koan code is in
// `data`.
constexpr int kServerError = -32000;

bool IsValidLabel(const std::string& label) {
  if (label.empty() || label.size() > kMaxLabelLength) {
    return false;
  }
  // No control characters: the label is shown in a dialog and written to a log.
  return std::ranges::none_of(
      label, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
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
  // A change.
  ClientRecord record;
};

SessionDelegate::~SessionDelegate() = default;

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
      delegate_(delegate) {}

Session::~Session() = default;

base::TimeTicks Session::Now() const {
  return env_.tick_clock->NowTicks();
}

void Session::OnBytesReceived(base::span<const uint8_t> bytes) {
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
  const base::DictValue none;
  if (state_ == State::kAuthenticated) {
    HandleAuthenticated(*id, *method);
  } else {
    HandleUnauthenticated(*id, *method, params ? *params : none);
  }
}

void Session::HandleUnauthenticated(int id,
                                    const std::string& method,
                                    const base::DictValue& params) {
  if (method == "Koan.pair") {
    HandlePair(id, params);
  } else if (method == "Koan.authenticate") {
    HandleAuthenticate(id, params);
  } else {
    // Before authentication nothing else is parsed or answered.
    ReplyError(id, "Koan.notAuthenticated",
               "authenticate first (Koan.pair or Koan.authenticate)");
    CountFailure();
  }
}

void Session::HandlePair(int id, const base::DictValue& params) {
  if (!env_.limiter->AllowPairing(peer_key_, Now())) {
    ReplyError(id, "Koan.rateLimited", "too many pairing attempts");
    return;
  }
  const std::string* encoded_key = params.FindString("publicKey");
  const std::string* label = params.FindString("label");
  std::optional<std::vector<uint8_t>> key;
  if (encoded_key) {
    key = base::Base64Decode(*encoded_key);
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
      !IsValidLabel(*label) || !requested) {
    Fail(id, "Koan.invalidParams",
         "publicKey must be 32 bytes in base64, label 1 to 64 printable "
         "characters, capabilities known names");
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
  Ask(std::move(request), std::move(pending));
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
  const std::optional<ClientRecord> record = env_.store->Find(*client_id);
  if (!record) {
    Fail(id, "Koan.unknownClient", "this client is not paired");
    return;
  }
  const base::Time last_used =
      record->last_used.is_null() ? record->created : record->last_used;
  if (env_.clock->Now() - last_used > kClientExpiry) {
    Fail(id, "Koan.clientExpired", "this pairing has expired; pair again");
    return;
  }
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
    Ask(std::move(request), std::move(pending));
    return;
  }
  BeginProof(id, *record);
}

void Session::BeginProof(int id, const ClientRecord& record) {
  nonce_.assign(kNonceSize, 0);
  env_.random_bytes.Run(nonce_);
  nonce_client_id_ = record.id;
  nonce_deadline_ = Now() + kNonceLifetime;
  state_ = State::kAwaitingProof;
  base::DictValue result;
  result.Set("nonce", base::Base64Encode(nonce_));
  Reply(id, std::move(result));
}

void Session::FinishProof(int id, const base::DictValue& params) {
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
  }
  if (!record || !signature ||
      !VerifyProof(record->public_key, record->id, nonce, *signature)) {
    Fail(id, "Koan.authFailed", "the proof did not verify");
    return;
  }
  // Nothing received before this point may be handled as authenticated: a peer
  // that pipelined a command behind its proof is cut off.
  if (!decoder_.OnAuthenticated()) {
    Close(CloseReason::kProtocolViolation);
    return;
  }
  env_.limiter->RecordSuccess(peer_key_);
  env_.store->Touch(record->id, env_.clock->Now());
  client_id_ = record->id;
  capabilities_ = record->capabilities;
  state_ = State::kAuthenticated;
  base::DictValue result;
  result.Set("authenticated", true);
  result.Set("clientId", client_id_);
  result.Set("capabilities", ToList(CapabilityNames(capabilities_)));
  Reply(id, std::move(result));
}

void Session::HandleAuthenticated(int id, const std::string& method) {
  if (method == "Koan.status") {
    base::DictValue result;
    result.Set("clientId", client_id_);
    result.Set("capabilities", ToList(CapabilityNames(capabilities_)));
    Reply(id, std::move(result));
  } else if (method == "Koan.pair" || method == "Koan.authenticate") {
    Close(CloseReason::kProtocolViolation);
  } else {
    // Nothing is forwarded from here yet.
    ReplyError(id, "Koan.unavailable",
               "this build does not forward commands to tabs");
  }
}

void Session::Ask(ApprovalRequest request, Pending pending) {
  pending.approval_id = next_approval_id_++;
  pending.deadline = Now() + kApprovalTimeout;
  request.id = pending.approval_id;
  state_ = State::kAwaitingApproval;
  pending_ = std::make_unique<Pending>(std::move(pending));
  delegate_->RequestApproval(request);
}

void Session::OnApprovalDecision(uint64_t request_id,
                                 ApprovalDecision decision) {
  if (state_ != State::kAwaitingApproval || !pending_ ||
      pending_->approval_id != request_id) {
    return;
  }
  std::unique_ptr<Pending> pending = std::move(pending_);
  state_ = State::kUnauthenticated;
  if (!decision.approved) {
    ReplyError(pending->message_id, "Koan.pairingDenied",
               "the user did not approve");
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
    if (pending->kind == ApprovalKind::kPeerChanged) {
      if (!env_.store->UpdatePinned(pending->record.id, PinPeer(peer_))) {
        ReplyError(pending->message_id, "Koan.storeFailed",
                   "the new identity could not be saved");
        Drain();
        return;
      }
      pending->record.pinned = PinPeer(peer_);
    } else {
      env_.accepted_chains->insert(pending->record.id + "|" +
                                   ChainDigest(peer_));
    }
    BeginProof(pending->message_id, pending->record);
  }
  Drain();
}

void Session::CheckTimeouts() {
  if (state_ == State::kClosed) {
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
               "the user did not answer in time");
    Close(CloseReason::kTimeout);
    return;
  }
  if (!partial_since_.is_null() && now - partial_since_ > kFrameTimeout) {
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
  state_ = State::kClosed;
  nonce_.clear();
  pending_.reset();
  delegate_->Close(reason);
}

std::string Session::NewClientId() {
  std::array<uint8_t, kClientIdLength / 2> bytes;
  env_.random_bytes.Run(bytes);
  return base::HexEncodeLower(bytes);
}

}  // namespace koan::agent
