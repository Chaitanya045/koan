// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_ARGS_H_
#define KOAN_CLI_KOAN_CLI_ARGS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "koan/cli/koan_cli_batch.h"

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
  // wait <tab> --resumed: blocks until the user gives a paused tab back
  // (Koan.resumed), then prints a fresh snapshot.
  kWait,
  // Prints the agent guide.
  kSkill,
  // Internal: the process that holds the connection for a task's life.
  kHold,
  // Runs agent-browser against Koan through the session's relay (section
  // 11): pairs `koan-relay` once, starts it, hands it the URL.
  kAgentBrowser,
  // relay env | relay stop, for an agent-browser --session.
  kRelayEnv,
  kRelayStop,
  // Reads commands from stdin, one per line, and runs them over one
  // connection (ParseShellLine()).
  kShell,
};

struct Command {
  CommandKind kind = CommandKind::kHelp;

  // Global options.
  // --user-data-dir: the browser profile directory (default: Koan's).
  std::string user_data_dir;
  // --client: the paired client to speak as (default: ChooseClient()).
  std::string client;
  // --json: machine-readable output.
  bool json = false;

  // pair: --label, --capabilities observe,read,drive. Empty: not given
  // (PairCapabilities()).
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
  // snapshot: --full, every node of the tree instead of the compact outline.
  bool full = false;
  // snapshot, and act's snapshot after: --interactive (only what an agent
  // acts on), --scope (viewport, full, auto, or @ref; "" is auto: the whole
  // page unless it is long, then the viewport), --diff (what changed since
  // the last snapshot of the tab), --locators (a locator after each ref
  // that names one node).
  bool interactive = false;
  std::string scope;
  bool diff = false;
  bool locators = false;

  // act: its steps, in order (koan_cli_batch.h). `act`, `ref`, `text` and
  // `scroll_by` are the step being run (the first one, once parsed).
  std::vector<Step> steps;
  // --steps FILE ("-": stdin): more steps, read when the command runs.
  std::string steps_file;
  // --until: what the batch waits for after its last step.
  std::optional<Until> until;
  ActKind act = ActKind::kClick;
  // click, fill, select, choose: "@123" from a snapshot, or a locator.
  std::string ref;
  // fill: the text; select, choose: the option; press: the key; navigate:
  // the URL.
  std::string text;
  // scroll: pixels down (negative: up).
  int scroll_by = 0;
  // --snapshot: print the compact snapshot after "ok", once the page has
  // settled (also set by the snapshot options above).
  bool snapshot_after = false;
  // --timeout: how long a navigation may take to commit, and --until may
  // wait, in seconds.
  int timeout_seconds = 15;

  // wait: --resumed (what it waits for; the only condition so far), and
  // --timeout as a duration ("90s", "10m", "1h", or seconds), 1 s to 1 h.
  bool resumed = false;
  int wait_seconds = 600;

  // shell: --stop-on-error, stop at the first command that fails.
  bool stop_on_error = false;

  // __hold: the fd to say "ready" on.
  int ready_fd = -1;

  // help: the command to explain ("" for all of them).
  std::string help_topic;

  // agent-browser: its arguments, as given. relay: --session.
  std::vector<std::string> passthrough;
  std::string session;
};

struct ParseResult {
  std::optional<Command> command;
  // Why not, for the user.
  std::string error;
};

// `args` is argv without the program name.
ParseResult ParseArgs(const std::vector<std::string>& args);

// `koan wait --timeout`: "90", "90s", "10m" or "1h" -> seconds, 1 s to 1 h;
// nullopt for anything else.
std::optional<int> ParseWaitDuration(std::string_view text);

// "@123" -> 123; nullopt for anything else.
std::optional<int> ParseRef(std::string_view ref);

// The tab `given` names among `ids`: an exact id, or the only one it is a
// prefix of (at least 4 characters). nullopt if none or several.
std::optional<std::string> ResolveTab(std::string_view given,
                                      const std::vector<std::string>& ids);

std::string UsageText();
// `koan help <command>`: its exact form and every option; nullopt for a
// command it does not know.
std::optional<std::string> HelpText(std::string_view command);

// One line of `koan shell`'s input. Either a command line as a shell would
// split it ('single' and "double" quotes, \ escapes; a leading "koan" is
// dropped, "#" starts a comment line), or a JSON line: an array of
// arguments, or {"id": any, "args": [...]}, answered with one JSON line.
struct ShellLine {
  // Nothing to run (an empty line or a comment).
  bool empty = false;
  bool json = false;
  // The JSON line's "id", echoed in its answer (JSON text; "null" if none).
  std::string id = "null";
  std::vector<std::string> args;
  // Why the line could not be read.
  std::string error;
};
ShellLine ParseShellLine(std::string_view line);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_ARGS_H_
