// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_PROTOCOL_H_
#define KOAN_CLI_KOAN_CLI_PROTOCOL_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"

// What the `koan` tool says to the browser and keeps on disk, as pure
// functions over JSON (.agent/notes/agent-control.md sections 4.2, 5 and 12).
// The socket, the keys' files and the holder are in koan_cli_io.h and
// koan_cli_main.cc.
namespace koan::cli {

// AgentControl/agent.json, which the browser writes while it listens.
struct Discovery {
  std::string socket;
  int pid = 0;
  std::string version;
};
std::optional<Discovery> ParseDiscovery(std::string_view json);

// The browser's profile directory: $KOAN_USER_DATA_DIR, else Koan's default
// (~/Library/Application Support/Koan on the Mac, ~/.config/koan elsewhere).
base::FilePath DefaultUserDataDir(const base::FilePath& home,
                                  const char* env_override);
base::FilePath DiscoveryFile(const base::FilePath& user_data_dir);

// ~/.koan: the paired clients (clients/<id>/, 0700, the id never the label)
// and the holders' sockets (run/<id>.sock).
base::FilePath KoanHome(const base::FilePath& home);
base::FilePath ClientDir(const base::FilePath& koan_home,
                         std::string_view client_id);
base::FilePath HolderSocket(const base::FilePath& koan_home,
                            std::string_view client_id);

// clients/<id>/client.json.
struct ClientInfo {
  std::string client_id;
  std::string label;
  // Who paired, as the OS said (the browser pins the same, section 5.5):
  // `koan` and the agent that ran it. The holder serves only that agent's
  // `koan` (koan_cli_admission.h). nullopt for a record without it.
  std::optional<agent::PinnedIdentity> pinned;
};
std::string SerializeClient(const ClientInfo& client);
std::optional<ClientInfo> ParseClient(std::string_view json);

// The client to use: `wanted` if given and paired, else the only paired one.
// nullopt (and `error`) if none, several, or not that one.
std::optional<std::string> ChooseClient(const std::vector<std::string>& paired,
                                        std::string_view wanted,
                                        std::string* error);

// One request: {"id", "method", "params", "sessionId"?}.
std::string Request(int id,
                    std::string_view method,
                    base::DictValue params,
                    std::string_view session_id = {});

// An answer or an event, as read from the wire.
struct Message {
  std::optional<int> id;
  std::string method;
  std::string session_id;
  base::DictValue params;
  std::optional<base::DictValue> result;
  // The error's Koan code (its "data", e.g. "Koan.notGranted"), or the CDP
  // code as text, and its message.
  std::string error_code;
  std::string error_message;
  bool is_error() const { return !error_code.empty(); }
};
std::optional<Message> ParseMessage(std::string_view json);

// An answer's id rewritten (the holder hands requests on with ids of its
// own). nullopt if `json` is not an object.
std::optional<std::string> WithId(std::string_view json, int id);

// Accessibility.getFullAXTree's result as an outline an agent can read: one
// line per node that says something, indented by depth, with "@<backend DOM
// node id>" to act on. At most `max_lines`.
std::string SnapshotText(const base::DictValue& ax_tree, size_t max_lines);

// The Input.dispatchKeyEvent params (keyDown, then keyUp with "type" changed)
// for a key name ("Enter", "Tab", "Escape", "Backspace", "ArrowDown", ...) or
// one character. nullopt for a name it does not know.
std::optional<base::DictValue> KeyDownFor(std::string_view key);

// The middle of the first quad of DOM.getContentQuads' result.
std::optional<std::pair<double, double>> QuadCenter(
    const base::DictValue& result);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_PROTOCOL_H_
