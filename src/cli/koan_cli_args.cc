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
      {"wait", CommandKind::kWait},
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

std::optional<int> ParseWaitDuration(std::string_view text) {
  int scale = 1;
  if (text.ends_with('s')) {
    text.remove_suffix(1);
  } else if (text.ends_with('m')) {
    text.remove_suffix(1);
    scale = 60;
  } else if (text.ends_with('h')) {
    text.remove_suffix(1);
    scale = 3600;
  }
  int value = 0;
  if (text.empty() || !base::StringToInt(text, &value) || value < 1 ||
      value > 3600 / scale) {
    return std::nullopt;
  }
  return value * scale;
}

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
  bool snapshot_option = false;
  bool timeout_given = false;
  std::string timeout_text;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    // After `act <tab>`, the steps: each takes its words by count, so a
    // fill's text that looks like an option is the text.
    if (positional.size() == 2 && positional[0] == "act" &&
        !arg.starts_with("--")) {
      size_t at = i;
      std::string error;
      if (!ParseSteps(args, &at, &command.steps, &error)) {
        return Error(error);
      }
      i = at - 1;
      continue;
    }
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
             {"--steps", &command.steps_file},
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
    if (arg == "--snapshot") {
      command.snapshot_after = true;
      act_option = true;
      continue;
    }
    if (std::optional<bool> got = value_of("--timeout", &timeout_text)) {
      // Read once the command is known: act's is seconds, wait's a duration.
      if (!*got) {
        timeout_text.clear();
      }
      timeout_given = true;
      continue;
    }
    if (arg == "--resumed") {
      command.resumed = true;
      continue;
    }
    if (std::optional<bool> got = value_of("--until", &value)) {
      std::string error;
      if (!*got) {
        return Error(
            "--until needs url~TEXT, text~TEXT, @ref~TEXT or \"@ref gone\"");
      }
      command.until = ParseUntil(value, &error);
      if (!command.until) {
        return Error(error);
      }
      act_option = true;
      continue;
    }
    if (arg == "--diff" || arg == "--interactive" || arg == "--locators") {
      (arg == "--diff"          ? command.diff
       : arg == "--interactive" ? command.interactive
                                : command.locators) = true;
      snapshot_option = true;
      continue;
    }
    if (std::optional<bool> got = value_of("--scope", &command.scope)) {
      if (!*got || !(command.scope == "viewport" || command.scope == "full" ||
                     command.scope == "auto" || ParseRef(command.scope))) {
        return Error("--scope takes viewport, full, auto or @ref");
      }
      snapshot_option = true;
      continue;
    }
    if (arg == "--stop-on-error") {
      command.stop_on_error = true;
      continue;
    }
    if (arg == "--full") {
      command.full = true;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      command.kind = CommandKind::kHelp;
      command.help_topic = positional.empty() ? "" : positional[0];
      return {command, ""};
    }
    if (arg == "--") {
      positional.insert(positional.end(), args.begin() + i + 1, args.end());
      break;
    }
    if (arg.size() > 1 && arg[0] == '-') {
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
  if (timeout_given && command.kind == CommandKind::kWait) {
    std::optional<int> seconds = ParseWaitDuration(timeout_text);
    if (!seconds) {
      return Error(
          "--timeout needs a duration from 1s to 1h (90s, 10m, 1h, or "
          "seconds)");
    }
    command.wait_seconds = *seconds;
  } else if (timeout_given) {
    if (!base::StringToInt(timeout_text, &command.timeout_seconds) ||
        command.timeout_seconds < 1 || command.timeout_seconds > 600) {
      return Error("--timeout needs a number of seconds (1 to 600)");
    }
    act_option = true;
  }
  if (command.resumed && command.kind != CommandKind::kWait) {
    return Error("--resumed is koan wait's");
  }
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
    return Error("--snapshot, --timeout and --until are koan act's");
  }
  if (!command.steps_file.empty() && command.kind != CommandKind::kAct) {
    return Error("--steps is koan act's");
  }
  if (snapshot_option && command.kind != CommandKind::kAct &&
      command.kind != CommandKind::kSnapshot) {
    return Error(
        "--interactive, --scope, --diff and --locators are koan snapshot's "
        "and koan act's");
  }
  if (command.full && (command.interactive || command.diff)) {
    return Error("--full prints every node: not with --interactive or --diff");
  }
  // On act, they are what its snapshot after the steps shows.
  if (snapshot_option && command.kind == CommandKind::kAct) {
    command.snapshot_after = true;
  }
  if (command.stop_on_error && command.kind != CommandKind::kShell) {
    return Error("--stop-on-error is koan shell's");
  }

  switch (command.kind) {
    case CommandKind::kHelp:
      if (!expect(0, 1)) {
        return Error("usage: koan help [command]");
      }
      command.help_topic = rest.empty() ? "" : rest[0];
      break;
    case CommandKind::kVersion:
    case CommandKind::kPair:
    case CommandKind::kStatus:
    case CommandKind::kTabs:
    case CommandKind::kEnd:
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
    case CommandKind::kWatch:
      if (!expect(0, 1)) {
        return Error("usage: koan watch [tab]");
      }
      command.tab = rest.empty() ? "" : rest[0];
      break;
    case CommandKind::kWait:
      if (!expect(1, 1) || !command.resumed) {
        return Error("usage: koan wait <tab> --resumed [--timeout 10m]");
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
      if (rest.size() != 1 ||
          (command.steps.empty() && command.steps_file.empty())) {
        return Error(
            "usage: koan act <tab> <step> [<step> ...] [--until <condition>] "
            "[--snapshot] (koan help act)");
      }
      if (!command.steps.empty() && !command.steps_file.empty()) {
        return Error(
            "give the steps on the command line or in --steps, not "
            "both");
      }
      command.tab = rest[0];
      if (!command.steps.empty()) {
        const Step& first = command.steps.front();
        command.act = first.act;
        command.ref = first.target;
        command.text = first.text;
        command.scroll_by = first.scroll_by;
      }
      break;
    }
  }
  return {command, ""};
}

std::optional<std::string> HelpText(std::string_view command) {
  static const auto* help = new std::map<std::string_view, std::string_view>{
      {"act", R"HELP(koan act <tab> <step> [<step> ...] [options]
koan act <tab> --steps <file|-> [options]

Runs the steps in order over one connection. After each one, waits until
the page has settled (a navigation it started committed and loaded, the
network and the DOM quiet for 250 ms, at most 3 s), then runs the next.
Before each step it checks the tab: if the user has taken it (Koan.paused)
or the page waits for them (Koan.userNeeded), it stops there; after a
Koan.paused, `koan wait <tab> --resumed` waits for the tab to come back.
It stops at the first step that fails. One line per step ("2/4 fill @13:
ok"); a batch that stopped says where and why, and which steps did not
run.

Steps (at most 50):
  click <target>
  fill <target> <text>         fill @12=<text> @13=<text> ... fills several
  select <target> <option>     a native <select>, by label or value
  choose <target> <option>     any dropdown: opens it, clicks the option
  press <key>                  Enter, Tab, Escape, Backspace, Arrow..., or
                               one character
  scroll <pixels>              down (negative: up)
  navigate <url>               http, https or about:blank
<target> is @ref, from a snapshot of the page as it is now (a ref from
before a navigation fails: Koan.staleRef), or a locator,
loc:role=<role>[name="<name>"], which must name exactly one element (it
fails listing the candidates otherwise).

Options:
  --until url~TEXT | text~TEXT | @ref~TEXT | "@ref gone"
                               after the last step, wait until the URL,
                               the page's text or that element's text
                               contains TEXT, or the element is gone (up to
                               --timeout); Koan.untilTimeout if not
  --snapshot                   the snapshot after the steps
  --interactive, --diff, --scope S, --locators
                               that snapshot's options (koan help snapshot);
                               each implies --snapshot
  --timeout <seconds>          for a navigation to commit, and for --until
                               (15)
  --steps <file>               steps from a file, one or more a line ("-":
                               stdin; not in koan shell)
  --json                       one JSON object: steps, stopped, snapshot
Exit 0 if every step was done (and --until met), 1 if not.)HELP"},
      {"snapshot", R"HELP(koan snapshot <tab> [options]

The page as an outline: one line per node, role "name" = "value" [states]
@ref. Refs are the page's own node ids: they stay the same while the node
stays (typing, a click, a change elsewhere on the page), and a node a page
re-creates gets a new one. Open popups (a dropdown's list, a menu, a
dialog) come first.

Options:
  --interactive     only what can be acted on (with names, values and
                    states), headings and dialogs, and the text of alerts
                    and status messages; a closed dropdown's options are
                    left out (choose finds one by its text)
  --scope auto      (the default) the whole page, unless it is long (over
                    250 lines): then what the viewport shows
  --scope viewport  only what the viewport shows, with [more above: N
                    nodes] and [more below: N nodes]
  --scope full      the whole page
  --scope @ref      only that element and what is in it
  --diff            only the lines added (+), changed (~, as they are now)
                    and removed (-) since this agent's last snapshot of
                    the tab; "baseline reset (why)" and the whole snapshot
                    when there is nothing to compare with (a new page)
  --locators        a locator (loc:role=...[name="..."]) after each ref
                    whose role and name no other element has
  --full            every node of the tree (inline text boxes, each text
                    run), a ref on each
  --json            the raw accessibility tree)HELP"},
      {"open", R"HELP(koan open [url] [--task T] [--saved NAME]

Opens a background tab in the agent's Space and prints its id (4 or more
of its first characters name it later). --task groups tabs into a task
(koan end --task T closes them); --saved uses a saved container the user
allowed (Koan.notGranted if not).)HELP"},
      {"shell", R"HELP(koan shell [--stop-on-error]

Reads commands from stdin, one a line, as a shell splits them (a leading
"koan" is fine), or JSON lines (["act","ab12","click","@4"] or {"id": 1,
"args": [...]}), and runs them over one connection. Each answer ends with
a line "--- exit N"; a JSON line gets one JSON line {"id", "exit", "out",
"err"}. --stop-on-error stops at the first command that fails.)HELP"},
      {"screenshot", R"HELP(koan screenshot <tab> [-o file.png]

Saves what the tab shows as a PNG and prints its path.)HELP"},
      {"status", R"HELP(koan status [--json]

Whether Koan runs, the pairing, the connection, the tasks and tabs, and
the latest events (Koan.userNeeded, Koan.paused).)HELP"},
      {"pair", R"HELP(koan pair [--label NAME] [--capabilities LIST]

Asks Koan to let this agent in; the user approves in Koan. LIST is some of
space, observe, read, drive, keep-awake (default: all).)HELP"},
      {"take-over", R"HELP(koan take-over [tab]

Asks the user to let this agent control the tab in front (they approve in
Koan); prints its id.)HELP"},
      {"release", R"HELP(koan release <tab>

Gives a tab of the user's back.)HELP"},
      {"end", R"HELP(koan end [--task T]

Ends one task, or every task and the connection: their tabs close and
their throwaway containers are cleared.)HELP"},
      {"watch", R"HELP(koan watch [tab]

Streams this agent's events as JSON lines until stopped. With a tab, keeps
that tab rendering (animations, timers, a screencast) while it is watched,
even when Koan's window is covered.)HELP"},
      {"wait", R"HELP(koan wait <tab> --resumed [--timeout 10m]

Waits for a tab the user paused (Koan.paused, reason user-input) to come
back: when they stop using it, Koan gives it back by itself (Koan.resumed,
user-idle), or they press Hand back. Then prints a fresh interactive
snapshot of the page as the user left it: go on from that, not from the
plan you had. Keeps nothing awake while it waits.

Exits 1 with the reason if the tab will not come back by itself: the user
is handling it (Take over, Keep paused, a password or card field, a
question the page asks: tell the user and wait for them), the agent was
stopped, the tab closed, or --timeout (a duration: 90s, 10m, 1h; default
10m) passed. Exits 0 at once, with the snapshot, if the tab is not paused.)HELP"},
      {"skill", R"HELP(koan skill

Prints the guide for agents.)HELP"},
  };
  auto it = help->find(command == "hand-back" ? "release" : command);
  if (it == help->end()) {
    return std::nullopt;
  }
  return std::string(it->second) + "\n";
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
  koan snapshot <tab> [--interactive] [--diff] [--scope S] [--full]
                         the page as an outline, with @refs for act;
                         --interactive: only controls, headings, messages;
                         --diff: only what changed since the last snapshot
  koan screenshot <tab> [-o file.png]
  koan act <tab> <step> [<step> ...] [--until C] [--snapshot] [--diff]
                         steps: click @ref | fill @ref <text> | select @ref
                         <option> | choose @ref <option> | press <key> |
                         scroll <pixels> | navigate <url>, in order, each
                         once the page settled; stops at the first failure
                         and when the user takes the tab. A locator
                         (loc:role=button[name="Next"]) works for @ref
  koan help <command>    a command's exact form and options
  koan take-over [tab]   ask to control a tab of yours (you approve in Koan)
  koan release <tab>     give a tab of yours back
  koan end [--task T]    end one task, or (no --task) every task: its tabs
                         close and its throwaway container is cleared
  koan watch [tab]       events as JSON lines (userNeeded, paused, ...);
                         with a tab, keeps it rendering while watched
  koan wait <tab> --resumed [--timeout 10m]
                         wait for a paused tab to come back, then snapshot
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
