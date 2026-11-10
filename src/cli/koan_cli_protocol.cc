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

// An AX property's value: {"type": ..., "value": ...}.
std::string AxString(const base::DictValue& node, std::string_view key) {
  const base::DictValue* property = node.FindDict(key);
  if (!property) {
    return std::string();
  }
  if (const std::string* value = property->FindString("value")) {
    return *value;
  }
  return std::string();
}

// Roles that only group: shown only when they have a name.
bool IsQuietRole(std::string_view role) {
  return role.empty() || role == "generic" || role == "none" ||
         role == "presentation" || role == "InlineTextBox" ||
         role == "LineBreak";
}

std::string Quoted(std::string_view text) {
  constexpr size_t kMaxText = 120;
  std::string out = "\"";
  size_t count = 0;
  for (char c : text) {
    if (count++ == kMaxText) {
      out += "...";
      break;
    }
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
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
  return discovery;
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
  if (const base::DictValue* pinned = dict->FindDict("pinned")) {
    client.pinned =
        agent::PinnedIdentity{ProcessFrom(pinned->FindDict("process")),
                              ProcessFrom(pinned->FindDict("agent"))};
  }
  return client;
}

std::optional<std::string> ChooseClient(const std::vector<std::string>& paired,
                                        std::string_view wanted,
                                        std::string* error) {
  if (!wanted.empty()) {
    for (const std::string& id : paired) {
      if (id == wanted) {
        return id;
      }
    }
    *error = "no paired client " + std::string(wanted) + " in ~/.koan/clients";
    return std::nullopt;
  }
  if (paired.empty()) {
    *error = "not paired: run koan pair, and approve it in Koan";
    return std::nullopt;
  }
  if (paired.size() > 1) {
    *error =
        "several paired clients in ~/.koan/clients; pick one with --client";
    return std::nullopt;
  }
  return paired.front();
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
  if (!message.id && message.method.empty()) {
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

std::string SnapshotText(const base::DictValue& ax_tree, size_t max_lines) {
  const base::ListValue* nodes = ax_tree.FindList("nodes");
  if (!nodes || nodes->empty()) {
    return std::string();
  }
  std::map<std::string, const base::DictValue*> by_id;
  for (const base::Value& value : *nodes) {
    if (const base::DictValue* node = value.GetIfDict()) {
      if (const std::string* id = node->FindString("nodeId")) {
        by_id[*id] = node;
      }
    }
  }
  const base::DictValue* root = (*nodes)[0].GetIfDict();
  if (!root) {
    return std::string();
  }
  std::string out;
  size_t lines = 0;
  bool truncated = false;
  // Depth-first, without recursion (a hostile page can nest deeply).
  std::vector<std::pair<const base::DictValue*, int>> stack = {{root, 0}};
  std::map<const base::DictValue*, bool> seen;
  while (!stack.empty()) {
    auto [node, depth] = stack.back();
    stack.pop_back();
    if (seen[node]) {
      continue;
    }
    seen[node] = true;
    const bool ignored = node->FindBool("ignored").value_or(false);
    const std::string role = AxString(*node, "role");
    const std::string name = AxString(*node, "name");
    const std::string value = AxString(*node, "value");
    int child_depth = depth;
    if (!ignored && !(IsQuietRole(role) && name.empty() && value.empty()) &&
        role != "StaticText") {
      if (lines == max_lines) {
        truncated = true;
        break;
      }
      out.append(static_cast<size_t>(depth) * 2, ' ');
      out += role;
      if (!name.empty()) {
        out += " " + Quoted(name);
      }
      if (!value.empty()) {
        out += " = " + Quoted(value);
      }
      if (std::optional<int> backend = node->FindInt("backendDOMNodeId")) {
        out += " @" + base::NumberToString(*backend);
      }
      out += "\n";
      ++lines;
      child_depth = depth + 1;
    } else if (!ignored && role == "StaticText" && !name.empty()) {
      // Text that is not a control's own name.
      if (lines == max_lines) {
        truncated = true;
        break;
      }
      out.append(static_cast<size_t>(depth) * 2, ' ');
      out += Quoted(name) + "\n";
      ++lines;
      continue;
    }
    if (const base::ListValue* children = node->FindList("childIds")) {
      for (auto it = children->rbegin(); it != children->rend(); ++it) {
        if (const std::string* child_id = it->GetIfString()) {
          if (auto child = by_id.find(*child_id); child != by_id.end()) {
            stack.emplace_back(child->second, child_depth);
          }
        }
      }
    }
  }
  if (truncated) {
    out += "... (more; the page is long)\n";
  }
  return out;
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
