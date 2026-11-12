// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_CLIENT_H_
#define KOAN_CLI_KOAN_CLI_CLIENT_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"

// A paired client of agent control, as the `koan` tool and `koan-relay` both
// are (.agent/notes/agent-control.md sections 5 and 11): its key and record on
// disk, pairing, and proving itself to the browser. Each program pairs as
// itself, so the browser's dialog and pin name the program that connects:
// `koan` keeps its clients in ~/.koan/clients, `koan-relay` in
// ~/.koan/relay/clients (the `home` passed in).
namespace koan::cli {

// The user may take a while over the approval dialog (60 s in the browser).
inline constexpr base::TimeDelta kApprovalWait = base::Seconds(75);
inline constexpr base::TimeDelta kAnswerWait = base::Seconds(30);

base::FilePath HomeDir();
// ~/.koan.
base::FilePath KoanDir();
// The browser profile: `given`, else $KOAN_USER_DATA_DIR, else Koan's.
base::FilePath UserDataDir(const std::string& given);

// A directory of the user's alone: made 0700 if missing, refused if it is a
// link or another user's.
bool PrivateDir(const base::FilePath& dir);
bool WritePrivateFile(const base::FilePath& path, const std::string& data);

std::vector<std::string> PairedClients(const base::FilePath& home);
std::optional<ClientInfo> LoadClient(const base::FilePath& home,
                                     const std::string& client_id);

// This process as the OS describes it (its executable, signature and
// ancestors), through the gateway's own resolver: the peer of one end of a
// socket pair is the process that holds both ends.
std::optional<agent::PeerIdentity> ResolveSelf(
    const agent::PeerResolver& resolver);

// Requests and their answers over a framed connection.
class Link {
 public:
  explicit Link(std::unique_ptr<FrameChannel> channel);
  ~Link();

  FrameChannel& channel() { return *channel_; }

  // Sends a request and waits for its answer; events that come meanwhile are
  // passed to `on_event` if given.
  std::optional<Message> Call(
      std::string_view method,
      base::DictValue params,
      std::string_view session_id = {},
      base::TimeDelta wait = kAnswerWait,
      base::RepeatingCallback<void(const Message&)> on_event = {});

 private:
  std::unique_ptr<FrameChannel> channel_;
  int next_id_ = 1;
};

std::string Describe(const std::optional<Message>& answer);

// Dials the browser's socket from its discovery file.
std::unique_ptr<FrameChannel> DialBrowser(const base::FilePath& user_data_dir,
                                          std::string* error);

// Proves the client to the browser (design section 5.2). If the browser asks
// the user first (the program or the agent that started it is not the one
// that paired), `on_asked` gets the browser's reason while the user decides.
bool Authenticate(
    Link& link,
    const base::FilePath& home,
    const std::string& client_id,
    std::string* error,
    base::RepeatingCallback<void(const std::string&)> on_asked = {});

// Pairs a new client (the user approves it in Koan) and stores its key and
// record, with the pin of this process, under `home`. The browser's answer,
// or nullopt with `error`.
std::optional<base::DictValue> PairNewClient(
    Link& link,
    const base::FilePath& home,
    const std::string& label,
    const std::vector<std::string>& capabilities,
    std::string* error);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_CLIENT_H_
