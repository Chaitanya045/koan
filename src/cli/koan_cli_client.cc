// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_client.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/cli/koan_cli_actions.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace koan::cli {

base::FilePath HomeDir() {
  const char* home = getenv("HOME");
  return base::FilePath(home && *home ? home : "/");
}

base::FilePath KoanDir() {
  return KoanHome(HomeDir());
}

base::FilePath UserDataDir(const std::string& given) {
  return given.empty()
             ? DefaultUserDataDir(HomeDir(), getenv("KOAN_USER_DATA_DIR"))
             : base::FilePath(given);
}

bool PrivateDir(const base::FilePath& dir) {
  if (mkdir(dir.value().c_str(), 0700) != 0 && errno != EEXIST) {
    return false;
  }
  struct stat info;
  return lstat(dir.value().c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
         info.st_uid == geteuid() && chmod(dir.value().c_str(), 0700) == 0;
}

bool WritePrivateFile(const base::FilePath& path, const std::string& data) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(path.value().c_str(),
           O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600)));
  return fd.is_valid() && base::WriteFileDescriptor(fd.get(), data) &&
         fchmod(fd.get(), 0600) == 0;
}

std::vector<std::string> PairedClients(const base::FilePath& home) {
  std::vector<std::string> ids;
  base::FileEnumerator dirs(home.Append("clients"), false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
    std::string json;
    if (!base::ReadFileToString(dir.Append("client.json"), &json)) {
      continue;
    }
    if (std::optional<ClientInfo> client = ParseClient(json);
        client && client->client_id == dir.BaseName().value()) {
      ids.push_back(client->client_id);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::optional<ClientInfo> LoadClient(const base::FilePath& home,
                                     const std::string& client_id) {
  std::string json;
  if (!base::ReadFileToString(ClientDir(home, client_id).Append("client.json"),
                              &json)) {
    return std::nullopt;
  }
  return ParseClient(json);
}

std::optional<agent::PeerIdentity> ResolveSelf(
    const agent::PeerResolver& resolver) {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return std::nullopt;
  }
  base::ScopedFD a(fds[0]);
  base::ScopedFD b(fds[1]);
  std::optional<agent::PeerToken> token = resolver.TokenForSocket(a.get());
  std::optional<agent::ResolvedPeer> peer =
      token ? resolver.Resolve(*token) : std::nullopt;
  if (!peer) {
    return std::nullopt;
  }
  return peer->identity;
}

namespace {

struct Key {
  uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
  uint8_t private_key[ED25519_PRIVATE_KEY_LEN];
};

std::optional<Key> LoadKey(const base::FilePath& home,
                           const std::string& client_id) {
  std::string encoded;
  if (!base::ReadFileToString(ClientDir(home, client_id).Append("key"),
                              &encoded)) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> seed = base::Base64Decode(
      std::string(base::TrimWhitespaceASCII(encoded, base::TRIM_ALL)));
  if (!seed || seed->size() != 32) {
    return std::nullopt;
  }
  Key key;
  ED25519_keypair_from_seed(key.public_key, key.private_key, seed->data());
  return key;
}

std::string Sign(const Key& key, base::span<const uint8_t> message) {
  uint8_t signature[ED25519_SIGNATURE_LEN];
  ED25519_sign(signature, message.data(), message.size(), key.private_key);
  return base::Base64Encode(signature);
}

}  // namespace

Link::Link(std::unique_ptr<FrameChannel> channel)
    : channel_(std::move(channel)) {}

Link::~Link() = default;

std::optional<Message> Link::Call(
    std::string_view method,
    base::DictValue params,
    std::string_view session_id,
    base::TimeDelta wait,
    base::RepeatingCallback<void(const Message&)> on_event) {
  const int id = next_id_++;
  // If the other end has closed already, it may have said why first: that is
  // read below (a closed socket answers at once).
  const bool sent =
      channel_->Write(Request(id, method, std::move(params), session_id));
  const base::TimeTicks deadline =
      base::TimeTicks::Now() + (sent ? wait : std::min(wait, base::Seconds(1)));
  while (true) {
    const base::TimeDelta left = deadline - base::TimeTicks::Now();
    if (left <= base::TimeDelta()) {
      return std::nullopt;
    }
    std::optional<std::string> frame = channel_->Read(left);
    if (!frame) {
      return std::nullopt;
    }
    std::optional<Message> message = ParseMessage(*frame);
    if (!message) {
      continue;
    }
    // A refusal: the browser or the holder says why it closes the
    // connection, in an error without an id (nothing was read to answer).
    if (message->id == id || (!message->id && message->is_error())) {
      return message;
    }
    if (!message->id && on_event) {
      on_event.Run(*message);
    }
  }
}

std::string Describe(const std::optional<Message>& answer) {
  if (!answer) {
    return "no answer (Koan quit, or it took too long)";
  }
  const AgentError error = MapError(answer->error_code, answer->error_message);
  return error.code + ": " + error.message;
}

std::unique_ptr<FrameChannel> DialBrowser(const base::FilePath& user_data_dir,
                                          std::string* error) {
  const base::FilePath file = DiscoveryFile(user_data_dir);
  std::string json;
  if (!base::ReadFileToString(file, &json)) {
    *error =
        "Koan is not running, or Agent ready is off (Settings > Agent "
        "Control); no " +
        file.value();
    return nullptr;
  }
  std::optional<Discovery> discovery = ParseDiscovery(json);
  if (!discovery) {
    *error = file.value() + " is not readable";
    return nullptr;
  }
  // A discovery file left by a browser that is gone.
  if (kill(discovery->pid, 0) != 0 && errno == ESRCH) {
    *error = "Koan is not running (its discovery file is stale)";
    return nullptr;
  }
  base::ScopedFD fd = ConnectUnix(discovery->socket, error);
  if (!fd.is_valid()) {
    return nullptr;
  }
  return std::make_unique<FrameChannel>(std::move(fd), agent::kMaxPayload);
}

bool Authenticate(Link& link,
                  const base::FilePath& home,
                  const std::string& client_id,
                  std::string* error,
                  base::RepeatingCallback<void(const std::string&)> on_asked) {
  std::optional<Key> key = LoadKey(home, client_id);
  if (!key) {
    *error = "the key of client " + client_id + " is missing or unreadable";
    return false;
  }
  base::DictValue ask;
  ask.Set("clientId", client_id);
  std::optional<Message> nonce_answer =
      link.Call("Koan.authenticate", std::move(ask));
  const std::string* nonce_text =
      nonce_answer && nonce_answer->result
          ? nonce_answer->result->FindString("nonce")
          : nullptr;
  std::optional<std::vector<uint8_t>> nonce =
      nonce_text ? base::Base64Decode(*nonce_text) : std::nullopt;
  if (!nonce || nonce->size() != agent::kNonceSize) {
    *error = "Koan did not start authentication: " + Describe(nonce_answer);
    return false;
  }
  base::DictValue proof;
  proof.Set("clientId", client_id);
  proof.Set("signature", Sign(*key, agent::ProofMessage(client_id, *nonce)));
  // A changed program or chain is asked about in Koan: wait for the user. The
  // browser says why it asks (Koan.approvalPending) before it waits.
  std::optional<Message> answer = link.Call(
      "Koan.authenticate", std::move(proof), {}, kApprovalWait,
      base::BindRepeating(
          [](const base::RepeatingCallback<void(const std::string&)>& on_asked,
             const Message& event) {
            const std::string* why = event.params.FindString("message");
            if (on_asked && event.method == "Koan.approvalPending" && why) {
              on_asked.Run(*why);
            }
          },
          on_asked));
  if (!answer || answer->is_error() || !answer->result) {
    *error = answer && answer->error_code == "Koan.authFailed"
                 ? "Koan does not know this client any more: pair it again"
                 : "authentication failed: " + Describe(answer);
    return false;
  }
  return true;
}

std::optional<base::DictValue> PairNewClient(
    Link& link,
    const base::FilePath& home,
    const std::string& label,
    const std::vector<std::string>& capabilities,
    std::string* error) {
  if (!PrivateDir(home) || !PrivateDir(home.Append("clients"))) {
    *error = home.value() + " is not a private directory";
    return std::nullopt;
  }
  // Who is pairing, as the browser pins it: the browser looks at this process
  // when it accepts the connection, so this looks now, not after the user has
  // answered. A chain read after the dialog can differ (a launcher that has
  // exited by then leaves no ancestors) and the holder would then turn away
  // the paired agent's own commands (koan_cli_admission.h).
  ClientInfo record{std::string(), label, std::nullopt};
  if (std::unique_ptr<agent::PeerResolver> resolver =
          agent::CreatePlatformPeerResolver()) {
    if (std::optional<agent::PeerIdentity> self = ResolveSelf(*resolver)) {
      record.pinned = agent::PinPeer(*self);
    }
  }
  uint8_t seed[32];
  RAND_bytes(seed, sizeof(seed));
  Key key;
  ED25519_keypair_from_seed(key.public_key, key.private_key, seed);
  base::DictValue params;
  params.Set("publicKey", base::Base64Encode(key.public_key));
  params.Set("label", label);
  base::ListValue names;
  for (const std::string& name : capabilities) {
    names.Append(name);
  }
  params.Set("capabilities", std::move(names));
  params.Set("proof", Sign(key, agent::PairingMessage(key.public_key, label)));
  std::optional<Message> answer =
      link.Call("Koan.pair", std::move(params), {}, kApprovalWait);
  if (!answer || answer->is_error() || !answer->result) {
    *error = Describe(answer);
    return std::nullopt;
  }
  const std::string* client_id = answer->result->FindString("clientId");
  if (!client_id || !agent::IsValidClientId(*client_id)) {
    *error = "Koan answered without a client id";
    return std::nullopt;
  }
  record.client_id = *client_id;
  const base::FilePath dir = ClientDir(home, *client_id);
  if (!PrivateDir(dir) ||
      !WritePrivateFile(dir.Append("key"), base::Base64Encode(seed)) ||
      !WritePrivateFile(dir.Append("client.json"), SerializeClient(record))) {
    *error = "cannot write " + dir.value();
    return std::nullopt;
  }
  return answer->result->Clone();
}

}  // namespace koan::cli
