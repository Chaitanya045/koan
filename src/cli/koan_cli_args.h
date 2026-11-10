// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_ARGS_H_
#define KOAN_CLI_KOAN_CLI_ARGS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The command line of the `koan` tool (.agent/notes/agent-control.md section
// 12). Pure: no I/O, so that every way of calling it is unit tested.
namespace koan::cli {

enum class CommandKind {
  kHelp,
  kVersion,
  // Asks the browser to pair this program (the user approves in Koan).
  kPair,
  // Whether Koan and the agent's connection are up, the tasks, the tabs and
  // what waits for the user.
  kStatus,
  kTabs,
  kOpen,
  kSnapshot,
  kScreenshot,
  kAct,
  kTakeOver,
  kRelease,
  // Ends one task (--task), or the connection and every task.
  kEnd,
  // Streams events as JSON lines.
  kWatch,
  // Prints the agent guide.
  kSkill,
  // Internal: the process that holds the connection for a task's life.
  kHold,
};

enum class ActKind {
  kClick,
  kFill,
  kPress,
  kScroll,
  kNavigate,
};

struct Command {
  CommandKind kind = CommandKind::kHelp;

  // Global options.
  // --user-data-dir: the browser profile directory (default: Koan's).
  std::string user_data_dir;
  // --client: the paired client to speak as (default: the only one).
  std::string client;
  // --json: machine-readable output.
  bool json = false;

  // pair: --label, --capabilities observe,read,drive.
  std::string label;
  std::vector<std::string> capabilities;

  // open: the URL, --task, --saved (a saved container, by name).
  std::string url;
  std::string task;
  std::string saved_container;

  // snapshot, screenshot, act, take-over, release: the tab (a target id, or
  // a unique prefix of one). Empty for take-over: the tab the user is on.
  std::string tab;
  // screenshot: -o / --output.
  std::string output;

  // act.
  ActKind act = ActKind::kClick;
  // click, fill: "@123" from a snapshot.
  std::string ref;
  // fill: the text; press: the key; navigate: the URL.
  std::string text;
  // scroll: pixels down (negative: up).
  int scroll_by = 0;

  // __hold: the fd to say "ready" on.
  int ready_fd = -1;
};

struct ParseResult {
  std::optional<Command> command;
  // Why not, for the user.
  std::string error;
};

// `args` is argv without the program name.
ParseResult ParseArgs(const std::vector<std::string>& args);

// "@123" -> 123; nullopt for anything else.
std::optional<int> ParseRef(std::string_view ref);

// The tab `given` names among `ids`: an exact id, or the only one it is a
// prefix of (at least 4 characters). nullopt if none or several.
std::optional<std::string> ResolveTab(std::string_view given,
                                      const std::vector<std::string>& ids);

std::string UsageText();

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_ARGS_H_
