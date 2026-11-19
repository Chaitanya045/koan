// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_protocol.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"

namespace koan::cli {

namespace {

std::optional<base::DictValue> ParseDict(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(*value).TakeDict();
}

std::string Write(const base::DictValue& dict) {
  return base::WriteJson(dict).value_or("{}");
}

}  // namespace

std::optional<Discovery> ParseDiscovery(std::string_view json) {
  std::optional<base::DictValue> dict = ParseDict(json);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* socket = dict->FindString("socket");
  const std::optional<int> pid = dict->FindInt("pid");
  if (!socket || socket->empty() || (*socket)[0] != '/' || !pid || *pid <= 0) {
    return std::nullopt;
  }
  Discovery discovery;
  discovery.socket = *socket;
  discovery.pid = *pid;
  if (const std::string* version = dict->FindString("version")) {
    discovery.version = *version;
  }
  discovery.agent_version = dict->FindInt("agentVersion").value_or(0);
  return discovery;
}

std::string VersionNotice(const std::optional<Discovery>& discovery, int ours) {
  if (!discovery || discovery->agent_version == ours) {
    return std::string();
  }
  const std::string theirs =
      discovery->agent_version
          ? "v" + base::NumberToString(discovery->agent_version)
          : "an older one";
  return "[koan:notice] this koan speaks agent interface v" +
         base::NumberToString(ours) + ", the running Koan (" +
         (discovery->version.empty() ? std::string("version unknown")
                                     : discovery->version) +
         ") speaks " + theirs +
         ": this koan is not the one that came with it (an old "
         "~/.local/bin/koan, or another Koan.app). Some commands may fail; "
         "run the koan inside the running Koan.app, or tell the user to "
         "update Koan or set up agents again (Settings > Agent Control).\n";
}

base::FilePath DefaultUserDataDir(const base::FilePath& home,
                                  const char* env_override) {
  if (env_override && *env_override) {
    return base::FilePath(env_override);
  }
#if BUILDFLAG(IS_MAC)
  return home.Append("Library").Append("Application Support").Append("Koan");
#else
  return home.Append(".config").Append("koan");
#endif
}

base::FilePath DiscoveryFile(const base::FilePath& user_data_dir) {
  return user_data_dir.Append("AgentControl").Append("agent.json");
}

base::FilePath KoanHome(const base::FilePath& home) {
  return home.Append(".koan");
}

base::FilePath ClientDir(const base::FilePath& koan_home,
                         std::string_view client_id) {
  return koan_home.Append("clients").Append(std::string(client_id));
}

base::FilePath HolderSocket(const base::FilePath& koan_home,
                            std::string_view client_id) {
  // A short name: a unix socket path is at most 103 characters on the Mac.
  return koan_home.Append("run").Append(
      base::StrCat({client_id.substr(0, 16), ".sock"}));
}

namespace {

base::DictValue ProcessDict(const agent::ProcessInfo& process) {
  base::DictValue dict;
  dict.Set("path", process.path);
  dict.Set("signingIdentity", process.signing_identity);
  return dict;
}

agent::ProcessInfo ProcessFrom(const base::DictValue* dict) {
  agent::ProcessInfo process;
  if (dict) {
    if (const std::string* path = dict->FindString("path")) {
      process.path = *path;
    }
    if (const std::string* identity = dict->FindString("signingIdentity")) {
      process.signing_identity = *identity;
    }
  }
  return process;
}

}  // namespace

std::string SerializeClient(const ClientInfo& client) {
  base::DictValue dict;
  dict.Set("clientId", client.client_id);
  dict.Set("label", client.label);
  if (!client.user_data_dir.empty()) {
    dict.Set("userDataDir", client.user_data_dir.value());
  }
  if (!client.paired_at.is_null()) {
    dict.Set("pairedAt", static_cast<double>(
                             client.paired_at.InMillisecondsSinceUnixEpoch()));
  }
  if (client.pinned) {
    base::DictValue pinned;
    pinned.Set("process", ProcessDict(client.pinned->process));
    pinned.Set("agent", ProcessDict(client.pinned->agent));
    dict.Set("pinned", std::move(pinned));
  }
  return Write(dict);
}

std::optional<ClientInfo> ParseClient(std::string_view json) {
  std::optional<base::DictValue> dict = ParseDict(json);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString("clientId");
  if (!id || id->size() != 32) {
    return std::nullopt;
  }
  for (char c : *id) {
    if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) {
      return std::nullopt;
    }
  }
  ClientInfo client;
  client.client_id = *id;
  if (const std::string* label = dict->FindString("label")) {
    client.label = *label;
  }
  if (const std::string* dir = dict->FindString("userDataDir")) {
    client.user_data_dir = base::FilePath(*dir);
  }
  if (std::optional<double> at = dict->FindDouble("pairedAt")) {
    client.paired_at = base::Time::FromMillisecondsSinceUnixEpoch(*at);
  }
  if (const base::DictValue* pinned = dict->FindDict("pinned")) {
    client.pinned =
        agent::PinnedIdentity{ProcessFrom(pinned->FindDict("process")),
                              ProcessFrom(pinned->FindDict("agent"))};
  }
  return client;
}

std::vector<std::string> PairCapabilities(
    const std::vector<std::string>& given) {
  if (!given.empty()) {
    return given;
  }
  return {"space", "observe", "read", "drive", "keep-awake"};
}

std::optional<std::string> ChooseClient(
    const std::vector<ClientInfo>& paired,
    std::string_view wanted,
    const base::FilePath& user_data_dir,
    const std::optional<agent::ProcessInfo>& agent,
    std::string* error) {
  if (!wanted.empty()) {
    for (const ClientInfo& client : paired) {
      if (client.client_id == wanted) {
        return client.client_id;
      }
    }
    *error = "no paired client " + std::string(wanted) + " in ~/.koan/clients";
    return std::nullopt;
  }
  std::vector<const ClientInfo*> candidates;
  for (const ClientInfo& client : paired) {
    if (client.user_data_dir.empty() || client.user_data_dir == user_data_dir) {
      candidates.push_back(&client);
    }
  }
  if (candidates.empty()) {
    *error = paired.empty()
                 ? "not paired: run koan pair, and approve it in Koan"
                 : "not paired with the Koan at " + user_data_dir.value() +
                       ": run koan pair, and approve it in Koan";
    return std::nullopt;
  }
  // This agent's own pairings first: another agent's would be turned away by
  // its holder (koan_cli_admission.h).
  if (agent) {
    std::vector<const ClientInfo*> own;
    for (const ClientInfo* client : candidates) {
      if (client->pinned && client->pinned->agent == *agent) {
        own.push_back(client);
      }
    }
    if (!own.empty()) {
      candidates = std::move(own);
    }
  }
  const ClientInfo* newest = candidates.front();
  for (const ClientInfo* client : candidates) {
    if (client->paired_at > newest->paired_at ||
        (client->paired_at == newest->paired_at &&
         client->client_id > newest->client_id)) {
      newest = client;
    }
  }
  return newest->client_id;
}

bool ShouldForget(const ClientInfo& client,
                  const base::FilePath& dialled,
                  const base::FilePath& default_user_data_dir) {
  return client.user_data_dir.empty() ? dialled == default_user_data_dir
                                      : client.user_data_dir == dialled;
}

std::string Request(int id,
                    std::string_view method,
                    base::DictValue params,
                    std::string_view session_id) {
  base::DictValue message;
  message.Set("id", id);
  message.Set("method", method);
  message.Set("params", std::move(params));
  if (!session_id.empty()) {
    message.Set("sessionId", session_id);
  }
  return Write(message);
}

std::string Refusal(std::string_view code, std::string_view message) {
  base::DictValue error;
  error.Set("code", -32000);
  error.Set("message", message);
  error.Set("data", code);
  base::DictValue root;
  root.Set("error", std::move(error));
  return Write(root);
}

std::optional<Message> ParseMessage(std::string_view json) {
  std::optional<base::DictValue> dict = ParseDict(json);
  if (!dict) {
    return std::nullopt;
  }
  Message message;
  message.id = dict->FindInt("id");
  if (const std::string* method = dict->FindString("method")) {
    message.method = *method;
  }
  if (const std::string* session = dict->FindString("sessionId")) {
    message.session_id = *session;
  }
  if (base::DictValue* params = dict->FindDict("params")) {
    message.params = std::move(*params);
  }
  if (base::DictValue* result = dict->FindDict("result")) {
    message.result = std::move(*result);
  }
  if (const base::DictValue* error = dict->FindDict("error")) {
    if (const std::string* data = error->FindString("data")) {
      message.error_code = *data;
    } else {
      message.error_code =
          base::NumberToString(error->FindInt("code").value_or(-1));
    }
    if (const std::string* text = error->FindString("message")) {
      message.error_message = *text;
    }
  }
  // An error without an id is a refusal (the connection is being closed).
  if (!message.id && message.method.empty() && !message.is_error()) {
    return std::nullopt;
  }
  return message;
}

std::optional<std::string> WithId(std::string_view json, int id) {
  std::optional<base::DictValue> dict = ParseDict(json);
  if (!dict) {
    return std::nullopt;
  }
  dict->Set("id", id);
  return Write(*dict);
}

std::optional<base::DictValue> KeyDownFor(std::string_view key) {
  struct Named {
    std::string_view name;
    std::string_view code;
    int key_code;
    std::string_view text;
  };
  static constexpr Named kNamed[] = {
      {"Enter", "Enter", 13, "\r"},
      {"Tab", "Tab", 9, ""},
      {"Escape", "Escape", 27, ""},
      {"Backspace", "Backspace", 8, ""},
      {"Delete", "Delete", 46, ""},
      {"ArrowUp", "ArrowUp", 38, ""},
      {"ArrowDown", "ArrowDown", 40, ""},
      {"ArrowLeft", "ArrowLeft", 37, ""},
      {"ArrowRight", "ArrowRight", 39, ""},
      {"Home", "Home", 36, ""},
      {"End", "End", 35, ""},
      {"PageUp", "PageUp", 33, ""},
      {"PageDown", "PageDown", 34, ""},
      {"Space", "Space", 32, " "},
  };
  base::DictValue params;
  params.Set("type", "keyDown");
  for (const Named& named : kNamed) {
    if (named.name == key) {
      params.Set("key", named.name == "Space" ? " " : named.name);
      params.Set("code", named.code);
      params.Set("windowsVirtualKeyCode", named.key_code);
      if (!named.text.empty()) {
        params.Set("text", named.text);
      }
      return params;
    }
  }
  // One printable ASCII character.
  if (key.size() == 1 && key[0] >= 0x20 && key[0] < 0x7f) {
    params.Set("key", key);
    params.Set("text", key);
    return params;
  }
  return std::nullopt;
}

std::optional<std::pair<double, double>> QuadCenter(
    const base::DictValue& result) {
  const base::ListValue* quads = result.FindList("quads");
  if (!quads || quads->empty()) {
    return std::nullopt;
  }
  const base::ListValue* quad = (*quads)[0].GetIfList();
  if (!quad || quad->size() != 8) {
    return std::nullopt;
  }
  double x = 0;
  double y = 0;
  for (size_t i = 0; i < 8; ++i) {
    const std::optional<double> v = (*quad)[i].GetIfDouble();
    if (!v) {
      return std::nullopt;
    }
    (i % 2 ? y : x) += *v / 4;
  }
  return std::make_pair(x, y);
}

}  // namespace koan::cli
