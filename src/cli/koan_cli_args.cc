// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_args.h"

#include <map>
#include <utility>

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
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
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
        if (name != "observe" && name != "read" && name != "drive" &&
            name != "keep-awake") {
          return Error("unknown capability \"" + name +
                       "\" (observe, read, drive, keep-awake)");
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

  switch (command.kind) {
    case CommandKind::kHelp:
    case CommandKind::kVersion:
    case CommandKind::kPair:
    case CommandKind::kStatus:
    case CommandKind::kTabs:
    case CommandKind::kEnd:
    case CommandKind::kWatch:
    case CommandKind::kSkill:
      if (!expect(0, 0)) {
        return Error("koan " + positional[0] + " takes no arguments");
      }
      break;
    case CommandKind::kHold:
      if (!expect(0, 0) || command.ready_fd < 0) {
        return Error("koan __hold is started by koan itself");
      }
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
            "usage: koan act <tab> click @ref | fill @ref <text> | press "
            "<key> | scroll <pixels> | navigate <url>");
      }
      command.tab = rest[0];
      const std::string& verb = rest[1];
      const std::vector<std::string> act_args(rest.begin() + 2, rest.end());
      if (verb == "click" || verb == "fill") {
        const size_t want = verb == "click" ? 1 : 2;
        if (act_args.size() != want || !ParseRef(act_args[0])) {
          return Error(verb == "click"
                           ? "usage: koan act <tab> click @ref"
                           : "usage: koan act <tab> fill @ref <text>");
        }
        command.act = verb == "click" ? ActKind::kClick : ActKind::kFill;
        command.ref = act_args[0];
        command.text = verb == "fill" ? act_args[1] : "";
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
        return Error("unknown action \"" + verb +
                     "\" (click, fill, press, scroll, navigate)");
      }
      break;
    }
  }
  return {command, ""};
}

std::string UsageText() {
  return R"(koan: let an agent use Koan, in tabs of its own

  koan pair [--label NAME] [--capabilities observe,read,drive]
                         ask Koan to let this agent in (you approve in Koan)
  koan status            Koan, the connection, tasks, tabs, what waits for you
  koan tabs              the tabs this agent may see
  koan open [url] [--task T] [--saved NAME]
                         open a background tab in the agent's Space; prints
                         its id. --saved uses a saved container you allowed
  koan snapshot <tab>    the page as an outline, with @refs for act
  koan screenshot <tab> [-o file.png]
  koan act <tab> click @ref | fill @ref <text> | press <key>
               | scroll <pixels> | navigate <url>
  koan take-over [tab]   ask to control a tab of yours (you approve in Koan)
  koan release <tab>     give a tab of yours back
  koan end [--task T]    end one task, or (no --task) every task: its tabs
                         close and its throwaway container is cleared
  koan watch             events as JSON lines (userNeeded, paused, ...)
  koan skill             the guide for agents

Options: --json, --user-data-dir DIR, --client ID. <tab> is an id from
koan tabs or open, or a unique prefix of one (4 characters or more).
)";
}

}  // namespace koan::cli
