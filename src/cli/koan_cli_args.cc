// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_args.h"

#include <map>
#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace koan::cli {

namespace {

constexpr size_t kMinTabPrefix = 4;

const std::map<std::string_view, CommandKind>& Commands() {
  static const auto* commands = new std::map<std::string_view, CommandKind>{
      {"help", CommandKind::kHelp},
      {"version", CommandKind::kVersion},
      {"pair", CommandKind::kPair},
      {"status", CommandKind::kStatus},
      {"tabs", CommandKind::kTabs},
      {"open", CommandKind::kOpen},
      {"snapshot", CommandKind::kSnapshot},
      {"screenshot", CommandKind::kScreenshot},
      {"act", CommandKind::kAct},
      {"take-over", CommandKind::kTakeOver},
      {"release", CommandKind::kRelease},
      // The design's name for release (section 12).
      {"hand-back", CommandKind::kRelease},
      {"end", CommandKind::kEnd},
      {"watch", CommandKind::kWatch},
      {"skill", CommandKind::kSkill},
      {"__hold", CommandKind::kHold},
      {"relay", CommandKind::kRelayEnv},
      {"shell", CommandKind::kShell},
  };
  return *commands;
}

ParseResult Error(std::string text) {
  return {std::nullopt, std::move(text)};
}

// A name the browser accepts: 1 to 64 characters.
bool IsName(std::string_view name) {
  return !name.empty() && name.size() <= 64 && base::IsStringUTF8(name);
}

}  // namespace

std::optional<int> ParseRef(std::string_view ref) {
  int value = 0;
  if (ref.size() < 2 || ref[0] != '@' ||
      !base::StringToInt(ref.substr(1), &value) || value <= 0) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> ResolveTab(std::string_view given,
                                      const std::vector<std::string>& ids) {
  std::optional<std::string> found;
  for (const std::string& id : ids) {
    if (id == given) {
      return id;
    }
    if (given.size() >= kMinTabPrefix && id.starts_with(given)) {
      if (found) {
        return std::nullopt;
      }
      found = id;
    }
  }
  return found;
}

ParseResult ParseArgs(const std::vector<std::string>& args) {
  Command command;
  std::vector<std::string> positional;
  bool act_option = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    // Where an act's text goes (fill's text, a key, a URL), an argument is
    // the text even if it looks like an option.
    const bool act_text =
        positional.size() >= 3 && positional[0] == "act" &&
        (positional.size() == 4
             ? (positional[2] == "fill" || positional[2] == "select" ||
                positional[2] == "choose")
             : (positional.size() == 3 &&
                (positional[2] == "press" || positional[2] == "navigate" ||
                 positional[2] == "scroll")));
    // Everything after `agent-browser` is agent-browser's, untouched.
    if (arg == "agent-browser" && positional.empty()) {
      command.kind = CommandKind::kAgentBrowser;
      command.passthrough.assign(args.begin() + i + 1, args.end());
      if (!command.task.empty() || !command.saved_container.empty()) {
        return Error("--task and --saved are koan open's");
      }
      return {command, ""};
    }
    // An option's value: the next argument, or after '='.
    auto value_of = [&](std::string_view name,
                        std::string* out) -> std::optional<bool> {
      if (arg == name) {
        if (i + 1 >= args.size()) {
          return false;
        }
        *out = args[++i];
        return true;
      }
      if (arg.starts_with(std::string(name) + "=")) {
        *out = arg.substr(name.size() + 1);
        return true;
      }
      return std::nullopt;
    };
    std::string value;
    bool matched = false;
    for (auto [name, out] :
         std::initializer_list<std::pair<std::string_view, std::string*>>{
             {"--user-data-dir", &command.user_data_dir},
             {"--client", &command.client},
             {"--label", &command.label},
             {"--task", &command.task},
             {"--saved", &command.saved_container},
             {"--output", &command.output},
             {"--session", &command.session},
             {"-o", &command.output},
         }) {
      if (std::optional<bool> got = value_of(name, out)) {
        if (!*got || out->empty()) {
          return Error(std::string(name) + " needs a value");
        }
        matched = true;
        break;
      }
    }
    if (matched) {
      continue;
    }
    if (std::optional<bool> got = value_of("--capabilities", &value)) {
      if (!*got) {
        return Error("--capabilities needs a value");
      }
      for (std::string& name : base::SplitString(
               value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
        if (name != "space" && name != "observe" && name != "read" &&
            name != "drive" && name != "keep-awake") {
          return Error("unknown capability \"" + name +
                       "\" (space, observe, read, drive, keep-awake)");
        }
        command.capabilities.push_back(std::move(name));
      }
      continue;
    }
    if (std::optional<bool> got = value_of("--ready-fd", &value)) {
      if (!*got || !base::StringToInt(value, &command.ready_fd) ||
          command.ready_fd < 0) {
        return Error("--ready-fd needs a descriptor");
      }
      continue;
    }
    if (arg == "--json") {
      command.json = true;
      continue;
    }
    if (!act_text && arg == "--snapshot") {
      command.snapshot_after = true;
      act_option = true;
      continue;
    }
    if (!act_text) {
      if (std::optional<bool> got = value_of("--timeout", &value)) {
        if (!*got || !base::StringToInt(value, &command.timeout_seconds) ||
            command.timeout_seconds < 1 || command.timeout_seconds > 600) {
          return Error("--timeout needs a number of seconds (1 to 600)");
        }
        act_option = true;
        continue;
      }
    }
    if (arg == "--stop-on-error") {
      command.stop_on_error = true;
      continue;
    }
    // (An act's text may be "--full".)
    if (arg == "--full" &&
        !(positional.size() >= 3 && positional[0] == "act")) {
      command.full = true;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      command.kind = CommandKind::kHelp;
      return {command, ""};
    }
    if (arg == "--") {
      positional.insert(positional.end(), args.begin() + i + 1, args.end());
      break;
    }
    if (arg.size() > 1 && arg[0] == '-' &&
        !(positional.size() >= 3 && positional[0] == "act")) {
      // A negative number is a scroll amount, not an option.
      int number = 0;
      if (!base::StringToInt(arg, &number)) {
        return Error("unknown option " + arg);
      }
    }
    positional.push_back(arg);
  }

  if (positional.empty()) {
    command.kind = CommandKind::kHelp;
    return {command, ""};
  }
  auto it = Commands().find(positional[0]);
  if (it == Commands().end()) {
    return Error("unknown command \"" + positional[0] + "\"; try koan help");
  }
  command.kind = it->second;
  const std::vector<std::string> rest(positional.begin() + 1, positional.end());
  auto expect = [&](size_t min, size_t max) {
    return rest.size() >= min && rest.size() <= max;
  };

  if (!command.task.empty() && !IsName(command.task)) {
    return Error("--task must be 1 to 64 characters");
  }
  if (!command.saved_container.empty() && !IsName(command.saved_container)) {
    return Error("--saved must be 1 to 64 characters");
  }
  if (!command.label.empty() && command.label.size() > 64) {
    return Error("--label must be at most 64 characters");
  }
  if (command.full && command.kind != CommandKind::kSnapshot) {
    return Error("--full is koan snapshot's");
  }
  if (act_option && command.kind != CommandKind::kAct) {
    return Error("--snapshot and --timeout are koan act's");
  }
  if (command.stop_on_error && command.kind != CommandKind::kShell) {
    return Error("--stop-on-error is koan shell's");
  }

  switch (command.kind) {
    case CommandKind::kHelp:
    case CommandKind::kVersion:
    case CommandKind::kPair:
    case CommandKind::kStatus:
    case CommandKind::kTabs:
    case CommandKind::kEnd:
    case CommandKind::kWatch:
    case CommandKind::kSkill:
    case CommandKind::kShell:
      if (!expect(0, 0)) {
        return Error("koan " + positional[0] + " takes no arguments");
      }
      break;
    case CommandKind::kHold:
      if (!expect(0, 0) || command.ready_fd < 0) {
        return Error("koan __hold is started by koan itself");
      }
      break;
    case CommandKind::kRelayEnv:
    case CommandKind::kRelayStop:
      if (!expect(1, 1) || (rest[0] != "env" && rest[0] != "stop")) {
        return Error("usage: koan relay env|stop [--session S]");
      }
      command.kind =
          rest[0] == "env" ? CommandKind::kRelayEnv : CommandKind::kRelayStop;
      if (command.session.empty()) {
        command.session = "default";
      }
      break;
    case CommandKind::kAgentBrowser:
      break;
    case CommandKind::kOpen:
      if (!expect(0, 1)) {
        return Error("usage: koan open [url] [--task T] [--saved NAME]");
      }
      command.url = rest.empty() ? "about:blank" : rest[0];
      break;
    case CommandKind::kSnapshot:
    case CommandKind::kScreenshot:
    case CommandKind::kRelease:
      if (!expect(1, 1)) {
        return Error("usage: koan " + positional[0] + " <tab>");
      }
      command.tab = rest[0];
      break;
    case CommandKind::kTakeOver:
      if (!expect(0, 1)) {
        return Error("usage: koan take-over [tab]");
      }
      command.tab = rest.empty() ? "" : rest[0];
      break;
    case CommandKind::kAct: {
      if (rest.size() < 2) {
        return Error(
            "usage: koan act <tab> click @ref | fill @ref <text> | select "
            "@ref <option> | choose @ref <option> | press <key> | scroll "
            "<pixels> | navigate <url> [--snapshot] [--timeout <seconds>]");
      }
      command.tab = rest[0];
      const std::string& verb = rest[1];
      const std::vector<std::string> act_args(rest.begin() + 2, rest.end());
      if (verb == "click" || verb == "fill" || verb == "select" ||
          verb == "choose") {
        const size_t want = verb == "click" ? 1 : 2;
        const bool option = verb == "select" || verb == "choose";
        if (act_args.size() != want || !ParseRef(act_args[0]) ||
            (option && act_args[1].empty())) {
          return Error(verb == "click" ? "usage: koan act <tab> click @ref"
                       : verb == "fill"
                           ? "usage: koan act <tab> fill @ref <text>"
                           : "usage: koan act <tab> " + verb +
                                 " @ref <option text>");
        }
        command.act = verb == "click"    ? ActKind::kClick
                      : verb == "fill"   ? ActKind::kFill
                      : verb == "select" ? ActKind::kSelect
                                         : ActKind::kChoose;
        command.ref = act_args[0];
        command.text = verb == "click" ? "" : act_args[1];
      } else if (verb == "press") {
        if (act_args.size() != 1 || act_args[0].empty()) {
          return Error("usage: koan act <tab> press <key>");
        }
        command.act = ActKind::kPress;
        command.text = act_args[0];
      } else if (verb == "scroll") {
        if (act_args.size() != 1 ||
            !base::StringToInt(act_args[0], &command.scroll_by) ||
            command.scroll_by == 0) {
          return Error("usage: koan act <tab> scroll <pixels>");
        }
        command.act = ActKind::kScroll;
      } else if (verb == "navigate") {
        if (act_args.size() != 1 || act_args[0].empty()) {
          return Error("usage: koan act <tab> navigate <url>");
        }
        command.act = ActKind::kNavigate;
        command.text = act_args[0];
      } else {
        return Error(
            "unknown action \"" + verb +
            "\" (click, fill, select, choose, press, scroll, navigate)");
      }
      break;
    }
  }
  return {command, ""};
}

std::string UsageText() {
  return R"(koan: let an agent use Koan, in tabs of its own

  koan pair [--label NAME] [--capabilities space,observe,read,drive,keep-awake]
                         ask Koan to let this agent in (you approve in Koan);
                         asks for all of these unless --capabilities narrows
                         it (space alone: only tabs of its own)
  koan status            Koan, the connection, tasks, tabs, what waits for you
  koan tabs              the tabs this agent may see
  koan open [url] [--task T] [--saved NAME]
                         open a background tab in the agent's Space; prints
                         its id. --saved uses a saved container you allowed
  koan snapshot <tab> [--full]
                         the page as an outline, with @refs for act; --full
                         shows every node of the tree (same refs)
  koan screenshot <tab> [-o file.png]
  koan act <tab> click @ref | fill @ref <text> | select @ref <option>
               | choose @ref <option> | press <key> | scroll <pixels>
               | navigate <url>   [--snapshot] [--timeout <seconds>]
                         waits until the page settles, then prints ok
                         (ok (HTTP 504 ...) for a server's error page);
                         --snapshot prints the new snapshot with it.
                         choose opens a custom dropdown and clicks the
                         option. --timeout: for a navigation (15)
  koan take-over [tab]   ask to control a tab of yours (you approve in Koan)
  koan release <tab>     give a tab of yours back
  koan end [--task T]    end one task, or (no --task) every task: its tabs
                         close and its throwaway container is cleared
  koan watch             events as JSON lines (userNeeded, paused, ...)
  koan agent-browser <args...>
                         run agent-browser against Koan (its own relay,
                         approved once in Koan as koan-relay)
  koan relay env|stop [--session S]
                         where the relay's env file is / stop the relay
  koan shell [--stop-on-error]
                         run commands from stdin, one per line, over one
                         connection; each answer ends with a line
                         "--- exit N" (JSON lines in, JSON lines out)
  koan skill             the guide for agents

Options: --json, --user-data-dir DIR, --client ID (default: the newest
pairing of this agent with this Koan). <tab> is an id from
koan tabs or open, or a unique prefix of one (4 characters or more).
)";
}

ShellLine ParseShellLine(std::string_view line) {
  ShellLine parsed;
  std::string_view text = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
  if (text.empty() || text.front() == '#') {
    parsed.empty = true;
    return parsed;
  }
  if (text.front() == '[' || text.front() == '{') {
    parsed.json = true;
    std::optional<base::Value> value =
        base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    const base::ListValue* args = nullptr;
    if (value && value->is_list()) {
      args = &value->GetList();
    } else if (value && value->is_dict()) {
      if (const base::Value* id = value->GetDict().Find("id")) {
        parsed.id = base::WriteJson(*id).value_or("null");
      }
      args = value->GetDict().FindList("args");
    }
    if (!args) {
      parsed.error =
          "a JSON line is an array of arguments, or {\"id\", \"args\": [...]}";
      return parsed;
    }
    for (const base::Value& arg : *args) {
      if (!arg.is_string()) {
        parsed.error = "every argument is a string";
        return parsed;
      }
      parsed.args.push_back(arg.GetString());
    }
  } else {
    std::string word;
    bool in_word = false;
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      if (quote == '\'') {
        if (c == '\'') {
          quote = 0;
        } else {
          word += c;
        }
      } else if (c == '\\' && i + 1 < text.size() &&
                 (quote == 0 || std::string_view("\"\\$`").find(text[i + 1]) !=
                                    std::string_view::npos)) {
        word += text[++i];
        in_word = true;
      } else if (quote == '"') {
        if (c == '"') {
          quote = 0;
        } else {
          word += c;
        }
      } else if (c == '\'' || c == '"') {
        quote = c;
        in_word = true;
      } else if (base::IsAsciiWhitespace(c)) {
        if (in_word) {
          parsed.args.push_back(std::move(word));
          word.clear();
          in_word = false;
        }
      } else {
        word += c;
        in_word = true;
      }
    }
    if (quote) {
      parsed.error = std::string("unclosed ") +
                     (quote == '"' ? "double" : "single") + " quote";
      return parsed;
    }
    if (in_word) {
      parsed.args.push_back(std::move(word));
    }
  }
  // As typed in a terminal.
  if (!parsed.args.empty() && parsed.args.front() == "koan") {
    parsed.args.erase(parsed.args.begin());
  }
  if (parsed.args.empty()) {
    parsed.error = "no command";
  }
  return parsed;
}

}  // namespace koan::cli
