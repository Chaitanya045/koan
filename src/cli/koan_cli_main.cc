// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The `koan` tool (.agent/notes/agent-control.md section 12): pairs with the
// browser, and runs an agent's commands over ONE connection that lives as
// long as its tasks. The browser ends a client's tasks (closes their tabs,
// clears their throwaway containers) when its last connection goes, so the
// first command that needs a task starts a holder, `koan __hold`, which keeps
// the connection, and every command talks to the holder over a socket in
// ~/.koan/run. `koan end` (or 30 minutes with no command) ends it.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/cli/koan_cli_actions.h"
#include "koan/cli/koan_cli_admission.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_autofill.h"
#include "koan/cli/koan_cli_batch.h"
#include "koan/cli/koan_cli_client.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_keepalive.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
#include "koan/cli/koan_cli_screenshot.h"
#include "koan/cli/koan_cli_settle.h"
#include "koan/cli/koan_cli_skill.h"
#include "koan/cli/koan_cli_snapshot.h"
#include "koan/cli/koan_cli_upload.h"
#include "koan/cli/koan_relay_gate.h"

#if BUILDFLAG(IS_LINUX)
#include "koan/browser/agent/koan_agent_setup.h"  // nogncheck
#endif

namespace koan::cli {
namespace {

// A holder with no command for this long ends its tasks and exits.
constexpr base::TimeDelta kHolderIdle = base::Minutes(30);
// How many lines a snapshot prints at most: compact, and --full.
constexpr size_t kSnapshotLines = 1500;
constexpr size_t kFullSnapshotLines = 5000;
// --scope auto: a page whose outline is longer than this is cut to the
// viewport (about 10 tokens a line: 250 lines are ~2.5k tokens, a long
// page's 1500 are ~15k). The outline the viewport is cut from may be this
// long.
constexpr size_t kAutoScopeLines = 250;
constexpr size_t kScopedSnapshotLines = 20000;
// How long --until looks again after.
constexpr base::TimeDelta kUntilPoll = base::Milliseconds(200);

enum Exit {
  kOk = 0,
  kFailed = 1,
  kUsage = 2,
  kNotConnected = 3,
};

// While a step of `koan act` runs, its failure is kept for the step's
// receipt instead of printed (ScopedFailure).
std::string* g_failure = nullptr;

// How long Koan.autofill may take: suggestions, then Chrome's fill.
constexpr base::TimeDelta kAutofillWait = base::Seconds(15);
// How long Koan.upload may take: the wait for a chooser, then reading the
// files' metadata.
constexpr base::TimeDelta kUploadAnswerWait = kUploadWait + base::Seconds(15);

int Fail(const std::string& text, int code = kFailed) {
  if (g_failure) {
    *g_failure = text;
    return code;
  }
  fprintf(stderr, "koan: %s\n", text.c_str());
  return code;
}

class ScopedFailure {
 public:
  explicit ScopedFailure(std::string* text) : previous_(g_failure) {
    g_failure = text;
  }
  ScopedFailure(const ScopedFailure&) = delete;
  ScopedFailure& operator=(const ScopedFailure&) = delete;
  ~ScopedFailure() { g_failure = previous_; }

 private:
  const raw_ptr<std::string> previous_;
};

// --- The holder
// ---------------------------------------------------------------

std::string HolderPath(const std::string& client_id) {
  return HolderSocket(KoanDir(), client_id).value();
}

void SayReady(int fd, const std::string& line) {
  if (fd >= 0) {
    base::WriteFileDescriptor(fd, line + "\n");
    close(fd);
  }
}

// Says why a command is turned away, and closes its connection. Best effort:
// a fresh socket has room for one small frame.
void RefuseCommand(base::ScopedFD fd,
                   std::string_view code,
                   std::string_view message) {
  FrameChannel(std::move(fd), agent::kMaxPayload).Write(Refusal(code, message));
}

std::string RefusalText(LocalClientVerdict verdict,
                        const base::FilePath& log_path) {
  std::string why;
  switch (verdict) {
    case LocalClientVerdict::kAgentMissing:
      why =
          "this koan was not started by the agent that this client's "
          "connection serves; run it from that agent, or pair this one";
      break;
    case LocalClientVerdict::kOtherProgram:
      why = "this is not the koan that holds the connection";
      break;
    default:
      why = "the connection could not tell which program this is";
      break;
  }
  return why + " (" + std::string(VerdictName(verdict)) + ", see " +
         log_path.value() + ")";
}

int RunHolder(const Command& command) {
  std::string error;
  // The koan that starts a holder names its client (Holder()).
  const base::FilePath user_data_dir = UserDataDir(command.user_data_dir);
  std::optional<std::string> client_id =
      ChooseClient(PairedClients(KoanDir()), command.client, user_data_dir,
                   ThisAgent(), &error);
  int ready_fd = command.ready_fd;
  if (!client_id) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  // Every command that connects is checked as the browser checks `koan`
  // (koan_cli_admission.h): no resolver, or no identity of its own, and the
  // holder serves nobody. Read before connecting, as the browser reads it, so
  // that the chain is the one the browser lets in.
  const std::unique_ptr<agent::PeerResolver> resolver =
      agent::CreatePlatformPeerResolver();
  const std::optional<agent::PeerIdentity> self =
      resolver ? ResolveSelf(*resolver) : std::nullopt;
  if (!self) {
    SayReady(ready_fd,
             "error this system cannot say which program connects to koan, "
             "so koan cannot keep a connection open");
    return kFailed;
  }
  std::unique_ptr<FrameChannel> browser = DialBrowser(user_data_dir, &error);
  if (!browser) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  Link link(std::move(browser));
  // If Koan asks the user first, the koan that started this says why while
  // they decide.
  if (!Authenticate(link, KoanDir(), *client_id, user_data_dir, &error,
                    base::BindRepeating(
                        [](int fd, const std::string& why) {
                          std::string line;
                          base::ReplaceChars(why, "\r\n", " ", &line);
                          if (fd >= 0) {
                            base::WriteFileDescriptor(fd,
                                                      "wait " + line + "\n");
                          }
                        },
                        ready_fd))) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  if (!PrivateDir(KoanDir()) || !PrivateDir(KoanDir().Append("run"))) {
    SayReady(ready_fd, "error ~/.koan/run is not a private directory");
    return kFailed;
  }
  // The agent the pairing recorded, and the one that started this holder if
  // the browser let the holder in without that one in its chain.
  const std::optional<ClientInfo> client = LoadClient(KoanDir(), *client_id);
  const std::vector<agent::ProcessInfo> agents =
      AgentsToServe(client ? client->pinned : std::nullopt, *self);
  const base::FilePath log_path =
      KoanDir().Append("run").Append(client_id->substr(0, 16) + ".log");
  auto log = [&](const std::string& line) {
    base::ScopedFD fd(HANDLE_EINTR(
        open(log_path.value().c_str(),
             O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600)));
    if (fd.is_valid()) {
      base::WriteFileDescriptor(
          fd.get(), base::NumberToString(
                        base::Time::Now().InMillisecondsSinceUnixEpoch()) +
                        " " + line + "\n");
    }
  };
  base::ScopedFD listener = ListenUnix(HolderPath(*client_id), &error);
  if (!listener.is_valid()) {
    SayReady(ready_fd, "error " + error);
    return kFailed;
  }
  SayReady(ready_fd, "ok");
  ready_fd = -1;

  Router router;
  // Which page each tab's refs belong to, and its last snapshot
  // (koan_cli_snapshot.h).
  RefBook refs;
  Baselines baselines;
  std::map<Router::ConnectionId, std::unique_ptr<FrameChannel>> commands;
  // Who each command is, to look at again before each request (a socket
  // survives an exec of its peer).
  std::map<Router::ConnectionId, agent::ResolvedPeer> peers;
  constexpr size_t kMaxCommands = 16;
  Router::ConnectionId next_connection = 1;
  base::TimeTicks last_command = base::TimeTicks::Now();
  bool running = true;
  FrameChannel& to_browser = link.channel();

  auto send_to = [&](Router::ConnectionId id, const std::string& json) {
    auto it = commands.find(id);
    if (it != commands.end() && !it->second->Write(json)) {
      router.RemoveConnection(id);
      commands.erase(it);
      peers.erase(id);
    }
  };

  while (running) {
    std::vector<pollfd> fds = {{to_browser.fd(), POLLIN, 0},
                               {listener.get(), POLLIN, 0}};
    std::vector<Router::ConnectionId> ids;
    for (const auto& [id, channel] : commands) {
      fds.push_back({channel->fd(), POLLIN, 0});
      ids.push_back(id);
    }
    const base::TimeDelta idle_left =
        kHolderIdle - (base::TimeTicks::Now() - last_command);
    if (idle_left <= base::TimeDelta() && commands.empty() &&
        !router.has_pending()) {
      break;
    }
    const int timeout_ms =
        static_cast<int>(std::max<int64_t>(1000, idle_left.InMilliseconds()));
    if (HANDLE_EINTR(poll(fds.data(), fds.size(), timeout_ms)) < 0) {
      break;
    }
    // The browser: answers to commands, events to watchers.
    if (fds[0].revents) {
      if (!to_browser.ReadAvailable()) {
        break;
      }
      while (std::optional<std::string> frame = to_browser.TakeFrame()) {
        for (const Router::Delivery& delivery : router.FromBrowser(*frame)) {
          send_to(delivery.connection, delivery.json);
        }
        // The user had these tabs: refs from before are stale.
        for (const std::string& target : router.TakeResumed()) {
          refs.Resumed(target);
        }
      }
    }
    if (fds[1].revents & POLLIN) {
      base::ScopedFD fd(HANDLE_EINTR(accept(listener.get(), nullptr, nullptr)));
      std::optional<agent::ResolvedPeer> peer;
      LocalClientVerdict verdict = LocalClientVerdict::kUnresolved;
      if (fd.is_valid()) {
        std::optional<agent::PeerToken> token =
            resolver->TokenForSocket(fd.get());
        peer = token ? resolver->Resolve(*token) : std::nullopt;
        verdict = DecideLocalClient(
            self->process, agents, geteuid(),
            peer ? std::optional<agent::PeerIdentity>(peer->identity)
                 : std::nullopt);
        if (verdict == LocalClientVerdict::kAllowed &&
            commands.size() >= kMaxCommands) {
          log("refused busy");
          RefuseCommand(std::move(fd), "Hold.busy",
                        "too many koan commands at once; retry");
        } else if (verdict != LocalClientVerdict::kAllowed) {
          // Not the paired agent's koan: it never reaches the connection,
          // and is told why (another user's process is told nothing).
          log("refused " + std::string(VerdictName(verdict)) + " " +
              (peer ? peer->identity.process.path : std::string("?")));
          if (verdict == LocalClientVerdict::kOtherUser) {
            fd.reset();
          } else {
            RefuseCommand(std::move(fd), "Hold.refused",
                          RefusalText(verdict, log_path));
          }
        }
      }
      if (fd.is_valid()) {
        fcntl(fd.get(), F_SETFD, FD_CLOEXEC);
        // A command that stops reading cannot stall the holder for long.
        timeval send_timeout = {2, 0};
        setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &send_timeout,
                   sizeof(send_timeout));
        peers[next_connection] = std::move(*peer);
        commands[next_connection++] =
            std::make_unique<FrameChannel>(std::move(fd), agent::kMaxPayload);
      }
    }
    for (size_t i = 0; i < ids.size(); ++i) {
      if (!fds[i + 2].revents) {
        continue;
      }
      const Router::ConnectionId id = ids[i];
      auto it = commands.find(id);
      if (it == commands.end()) {
        continue;
      }
      FrameChannel& channel = *it->second;
      bool open = channel.ReadAvailable();
      // Still the process that was let in.
      if (auto peer = peers.find(id);
          open && (peer == peers.end() || !resolver->Unchanged(peer->second))) {
        log("refused changed");
        open = false;
        while (channel.TakeFrame()) {
        }
      }
      while (std::optional<std::string> frame = channel.TakeFrame()) {
        last_command = base::TimeTicks::Now();
        std::optional<Message> message = ParseMessage(*frame);
        if (!message || !message->id) {
          continue;
        }
        base::DictValue reply;
        reply.Set("id", *message->id);
        if (message->method == "Hold.status") {
          base::DictValue result;
          result.Set("clientId", *client_id);
          base::ListValue tasks;
          for (const std::string& task : router.tasks()) {
            tasks.Append(task);
          }
          result.Set("tasks", std::move(tasks));
          base::ListValue events;
          for (const std::string& event : router.events()) {
            events.Append(event);
          }
          result.Set("events", std::move(events));
          reply.Set("result", std::move(result));
          send_to(id, base::WriteJson(reply).value_or("{}"));
        } else if (message->method == "Hold.watch") {
          router.AddWatcher(id);
          reply.Set("result", base::DictValue());
          send_to(id, base::WriteJson(reply).value_or("{}"));
        } else if (message->method == "Hold.follow") {
          // Only a session of this client's: the router has it from an
          // attach through this holder; any other id matches no event.
          const std::string* session = message->params.FindString("sessionId");
          router.Follow(id, session ? *session : std::string());
          reply.Set("result", base::DictValue());
          send_to(id, base::WriteJson(reply).value_or("{}"));
        } else if (message->method == "Hold.rememberRefs" ||
                   message->method == "Hold.checkRef") {
          const std::string* target = message->params.FindString("targetId");
          const std::string* document = message->params.FindString("document");
          const std::optional<int> ref = message->params.FindInt("ref");
          const base::ListValue* seen = message->params.FindList("refs");
          const bool remember = message->method == "Hold.rememberRefs";
          if (!target || !document || (remember ? !seen : !ref)) {
            reply.Set("error", base::DictValue()
                                   .Set("code", -32602)
                                   .Set("message",
                                        "targetId, document and "
                                        "refs or ref are needed")
                                   .Set("data", "Hold.invalidParams"));
          } else if (remember) {
            std::vector<int> seen_ids;
            for (const base::Value& seen_id : *seen) {
              if (seen_id.is_int()) {
                seen_ids.push_back(seen_id.GetInt());
              }
            }
            refs.Remember(*target, *document, seen_ids);
            // The snapshot's lines, for the next one's --diff; the one they
            // replace goes back.
            base::DictValue result;
            if (const base::DictValue* kept =
                    message->params.FindDict("baseline")) {
              if (std::optional<Baseline> next = BaselineFromValue(*kept)) {
                next->document = *document;
                if (std::optional<Baseline> previous =
                        baselines.Swap(*target, std::move(*next))) {
                  result.Set("previous", BaselineToValue(*previous));
                }
              }
              result.Set("baselines", true);
            }
            reply.Set("result", std::move(result));
          } else {
            reply.Set("result", base::DictValue().Set(
                                    "verdict", RefVerdictName(refs.Check(
                                                   *target, *document, *ref))));
          }
          send_to(id, base::WriteJson(reply).value_or("{}"));
        } else if (message->method == "Hold.exit") {
          reply.Set("result", base::DictValue());
          send_to(id, base::WriteJson(reply).value_or("{}"));
          running = false;
        } else {
          Router::Outgoing out = router.FromCommand(id, *frame);
          if (out.to_command) {
            send_to(id, *out.to_command);
          } else if (out.to_browser && !to_browser.Write(*out.to_browser)) {
            running = false;
          }
        }
      }
      if (!open) {
        router.RemoveConnection(id);
        commands.erase(id);
        peers.erase(id);
      }
    }
  }
  // Closing the connection ends every task of the client (the browser closes
  // their tabs and clears their throwaway containers) and gives the user's
  // tabs back.
  unlink(HolderPath(*client_id).c_str());
  return kOk;
}

// Starts the holder and waits for it to be ready. The holder authenticates
// before this process exits, so the browser sees the agent that ran `koan`
// among its ancestors, as at pairing.
bool StartHolder(const Command& command, std::string* error) {
  base::FilePath self;
  if (!base::PathService::Get(base::FILE_EXE, &self)) {
    *error = "cannot find the koan executable";
    return false;
  }
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    *error = "pipe failed";
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    *error = "fork failed";
    return false;
  }
  if (pid == 0) {
    close(pipe_fds[0]);
    setsid();
    const int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
      dup2(null_fd, STDIN_FILENO);
      dup2(null_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
    }
    const std::string ready = base::NumberToString(pipe_fds[1]);
    std::vector<std::string> args = {self.value(), "__hold", "--ready-fd",
                                     ready};
    if (!command.user_data_dir.empty()) {
      args.insert(args.end(), {"--user-data-dir", command.user_data_dir});
    }
    if (!command.client.empty()) {
      args.insert(args.end(), {"--client", command.client});
    }
    std::vector<char*> argv;
    for (std::string& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    execv(argv[0], argv.data());
    _exit(127);
  }
  close(pipe_fds[1]);
  base::ScopedFD read_end(pipe_fds[0]);
  const base::TimeTicks deadline =
      base::TimeTicks::Now() + kApprovalWait + base::Seconds(10);
  auto read_line = [&] {
    std::string line;
    char c;
    while (line.size() < 4096) {
      pollfd poll_fd = {read_end.get(), POLLIN, 0};
      const base::TimeDelta left = deadline - base::TimeTicks::Now();
      if (left <= base::TimeDelta() ||
          HANDLE_EINTR(poll(&poll_fd, 1,
                            static_cast<int>(left.InMilliseconds()))) <= 0 ||
          HANDLE_EINTR(read(read_end.get(), &c, 1)) != 1 || c == '\n') {
        break;
      }
      line += c;
    }
    return line;
  };
  std::string line = read_line();
  // Koan asks the user before it lets the connection in: say why, and wait.
  while (line.starts_with("wait ")) {
    const std::string reason = line.substr(5);
    fprintf(stderr, "koan: %s; approve it in Koan...\n", reason.c_str());
    line = read_line();
  }
  if (line == "ok") {
    return true;
  }
  *error = line.starts_with("error ") ? line.substr(6)
                                      : "the connection to Koan did not start";
  return false;
}

std::unique_ptr<Link> ConnectHolder(const std::string& client_id) {
  std::string ignored;
  base::ScopedFD fd = ConnectUnix(HolderPath(client_id), &ignored);
  if (!fd.is_valid()) {
    return nullptr;
  }
  return std::make_unique<Link>(
      std::make_unique<FrameChannel>(std::move(fd), agent::kMaxPayload));
}

// The holder of the client, started if `start` and none runs.
std::unique_ptr<Link> Holder(const Command& command,
                             bool start,
                             std::string* error) {
  const base::FilePath user_data_dir = UserDataDir(command.user_data_dir);
  const std::optional<agent::ProcessInfo> agent = ThisAgent();
  std::string forgotten;
  // A pairing Koan has forgotten is removed when its holder cannot prove it
  // (Authenticate()); then the next one is tried, so that a stale record
  // never stands in the way of a good one.
  while (true) {
    std::optional<std::string> client_id = ChooseClient(
        PairedClients(KoanDir()), command.client, user_data_dir, agent, error);
    if (!client_id) {
      if (!forgotten.empty()) {
        *error = forgotten;
      }
      return nullptr;
    }
    // Report a forgotten pairing only when another one can be tried. If
    // none remains, ConnectionFor() prints it as the command's failure.
    if (!forgotten.empty()) {
      fprintf(stderr, "koan: %s\n", forgotten.c_str());
      forgotten.clear();
    }
    if (std::unique_ptr<Link> link = ConnectHolder(*client_id)) {
      return link;
    }
    if (!start) {
      return nullptr;
    }
    Command held = command;
    held.client = *client_id;
    if (StartHolder(held, error)) {
      std::unique_ptr<Link> link = ConnectHolder(*client_id);
      if (!link) {
        *error = "the connection to Koan started but cannot be reached";
      }
      return link;
    }
    if (!command.client.empty() || LoadClient(KoanDir(), *client_id)) {
      return nullptr;
    }
    forgotten = *error;
  }
}

// --- Output
// -------------------------------------------------------------------

void PrintJson(const base::DictValue& value) {
  printf("%s\n", base::WriteJson(value).value_or("{}").c_str());
}

// The result of a call, or the error printed and an exit code.
std::optional<base::DictValue> Expect(const std::optional<Message>& answer,
                                      int* exit_code) {
  if (!answer || answer->is_error() || !answer->result) {
    *exit_code = Fail(Describe(answer));
    return std::nullopt;
  }
  return answer->result->Clone();
}

// The full target id for what the user typed.
std::optional<std::string> ResolveTabId(Link& link,
                                        const std::string& given,
                                        int* exit_code) {
  std::optional<base::DictValue> tabs =
      Expect(link.Call("Koan.listTabs", base::DictValue()), exit_code);
  if (!tabs) {
    return std::nullopt;
  }
  std::vector<std::string> ids;
  if (const base::ListValue* list = tabs->FindList("tabs")) {
    for (const base::Value& tab : *list) {
      if (const base::DictValue* dict = tab.GetIfDict()) {
        if (const std::string* id = dict->FindString("targetId")) {
          ids.push_back(*id);
        }
      }
    }
  }
  std::optional<std::string> id = ResolveTab(given, ids);
  if (!id) {
    *exit_code = Fail("no single tab of this agent matches \"" + given +
                      "\" (koan tabs lists them)");
  }
  return id;
}

std::optional<std::string> SessionFor(Link& link,
                                      const std::string& target_id,
                                      int* exit_code) {
  base::DictValue params;
  params.Set("targetId", target_id);
  params.Set("flatten", true);
  std::optional<base::DictValue> result =
      Expect(link.Call("Target.attachToTarget", std::move(params)), exit_code);
  const std::string* session =
      result ? result->FindString("sessionId") : nullptr;
  if (!session) {
    if (result) {
      *exit_code = Fail("the tab gave no session");
    }
    return std::nullopt;
  }
  return *session;
}

// --- Commands
// -----------------------------------------------------------------

#if BUILDFLAG(IS_LINUX)
// `koan setup [--remove]`: what Settings > Agent Control > Set up agents does
// on the Mac, run by the user in a terminal on the host, because Koan's
// Flatpak cannot write to the home folder (koan_agent_setup.h). The link goes
// to this koan by the path that follows the Flatpak's updates.
int Setup(const Command& command) {
  base::FilePath exe;
  if (!base::PathService::Get(base::FILE_EXE, &exe)) {
    return Fail("cannot tell where this koan is");
  }
  const base::FilePath tool = agent::ToolToLink(exe);
  const base::FilePath home = HomeDir();
  const agent::AgentSetup setup = command.remove_setup
                                      ? agent::RemoveAgentSetup(home, tool)
                                      : agent::SetUpAgents(home, tool);
  if (command.json) {
    printf("%s\n", base::WriteJson(agent::AgentSetupToValue(setup, home))
                       .value_or("{}")
                       .c_str());
  } else {
    printf("%s ~/.local/bin/koan%s\n", setup.linked ? "linked" : "not linked:",
           setup.linked ? (" -> " + tool.value()).c_str() : "");
    for (const agent::AgentSetup::Agent& agent : setup.agents) {
      if (!agent.installed && !agent.set_up) {
        continue;
      }
      printf("%s %s\n",
             agent.set_up ? "set up:" : "not set up:", agent.name.c_str());
    }
    if (setup.linked && !setup.on_path) {
      printf(
          "~/.local/bin does not seem to be on your shell's PATH: agents use "
          "the full path, or add export PATH=\"$HOME/.local/bin:$PATH\" to "
          "~/.bashrc or ~/.zshrc\n");
    }
  }
  for (const std::string& problem : setup.problems) {
    fprintf(stderr, "koan: %s\n", problem.c_str());
  }
  return setup.problems.empty() ? kOk : kFailed;
}
#endif

int Pair(const Command& command) {
  std::string error;
  std::unique_ptr<FrameChannel> browser =
      DialBrowser(UserDataDir(command.user_data_dir), &error);
  if (!browser) {
    return Fail(error, kNotConnected);
  }
  Link link(std::move(browser));
  const std::string label = command.label.empty() ? "koan" : command.label;
  fprintf(stderr, "koan: approve \"%s\" in Koan...\n", label.c_str());
  std::optional<base::DictValue> result =
      PairNewClient(link, KoanDir(), UserDataDir(command.user_data_dir), label,
                    PairCapabilities(command.capabilities), &error);
  if (!result) {
    return Fail(error);
  }
  if (command.json) {
    PrintJson(*result);
    return kOk;
  }
  std::vector<std::string> granted;
  if (const base::ListValue* list = result->FindList("capabilities")) {
    for (const base::Value& name : *list) {
      if (name.is_string()) {
        granted.push_back(name.GetString());
      }
    }
  }
  printf("paired as %s (%s); allowed: %s\n",
         result->FindString("clientId")->c_str(), label.c_str(),
         base::JoinString(granted, ", ").c_str());
  return kOk;
}

// The "paired:" line of `koan status`.
std::string PairingLine(const std::string& pairing, const std::string& why) {
  if (pairing == "valid") {
    return "yes";
  }
  if (pairing == "approvalNeeded") {
    return "yes (Koan will ask you to approve it again" +
           (why.empty() ? std::string() : ": " + why) + ")";
  }
  if (pairing == "unchecked") {
    return "yes (not checked: " + why + ")";
  }
  if (pairing == "forgotten") {
    return "no (Koan forgot this pairing; run koan pair)";
  }
  if (pairing == "unknown") {
    return "no (this Koan does not know the pairing; run koan pair)";
  }
  return "no (koan pair)";
}

int Status(const Command& command) {
  base::DictValue status;
  std::string error;
  const base::FilePath user_data_dir = UserDataDir(command.user_data_dir);
  std::unique_ptr<FrameChannel> probe = DialBrowser(user_data_dir, &error);
  status.Set("koanRunning", static_cast<bool>(probe));
  std::unique_ptr<Link> holder = Holder(command, /*start=*/false, &error);
  // Whether Koan still knows the pairing: an open connection says so;
  // otherwise Koan is asked (a probe proof, which neither connects nor asks
  // the user), and a pairing it forgot is removed and the next one asked
  // about.
  std::string pairing = holder ? "valid" : "none";
  std::string why;
  if (!holder) {
    const std::optional<agent::ProcessInfo> agent = ThisAgent();
    std::unique_ptr<Link> link =
        probe ? std::make_unique<Link>(std::move(probe)) : nullptr;
    while (std::optional<std::string> client_id =
               ChooseClient(PairedClients(KoanDir()), command.client,
                            user_data_dir, agent, &error)) {
      if (!link) {
        pairing = "unchecked";
        why = "Koan is not reachable";
        break;
      }
      const PairingCheck check =
          CheckPairing(*link, KoanDir(), *client_id, user_data_dir, &why);
      if (check == PairingCheck::kForgotten) {
        fprintf(stderr, "koan: Koan forgot pairing %s; koan removed it\n",
                client_id->substr(0, 8).c_str());
        pairing = "forgotten";
        continue;
      }
      pairing = check == PairingCheck::kValid            ? "valid"
                : check == PairingCheck::kApprovalNeeded ? "approvalNeeded"
                : check == PairingCheck::kUnknown        ? "unknown"
                                                         : "unchecked";
      break;
    }
  }
  probe.reset();
  const bool paired = pairing == "valid" || pairing == "approvalNeeded" ||
                      pairing == "unchecked";
  status.Set("paired", paired);
  status.Set("pairing", pairing);
  status.Set("connected", static_cast<bool>(holder));
  int exit_code = kOk;
  if (holder) {
    if (std::optional<base::DictValue> held = Expect(
            holder->Call("Hold.status", base::DictValue()), &exit_code)) {
      status.Merge(std::move(*held));
    }
    if (std::optional<base::DictValue> tabs = Expect(
            holder->Call("Koan.listTabs", base::DictValue()), &exit_code)) {
      status.Set("tabs", tabs->Extract("tabs").value_or(base::Value()));
    }
  }
  if (command.json) {
    PrintJson(status);
    return exit_code;
  }
  printf("Koan: %s\n", status.FindBool("koanRunning").value_or(false)
                           ? "running, Agent ready"
                           : "not reachable (not running, or Agent ready off)");
  printf("paired: %s\n", PairingLine(pairing, why).c_str());
  printf("%s", VersionNotice(ReadDiscovery(user_data_dir),
                             agent::kAgentInterfaceVersion)
                   .c_str());
  printf("connection: %s\n",
         holder ? "open" : "none (the next koan open starts one)");
  if (const base::ListValue* tasks = status.FindList("tasks")) {
    for (const base::Value& task : *tasks) {
      printf("task: %s\n", task.GetString().c_str());
    }
  }
  if (const base::ListValue* tabs = status.FindList("tabs")) {
    for (const base::Value& tab : *tabs) {
      const base::DictValue& t = tab.GetDict();
      printf("tab %s  %s  %s  %s\n",
             t.FindString("targetId") ? t.FindString("targetId")->c_str() : "",
             t.FindString("state") ? t.FindString("state")->c_str() : "",
             t.FindString("url") ? t.FindString("url")->c_str() : "",
             t.FindString("title") ? t.FindString("title")->c_str() : "");
    }
  }
  if (const base::ListValue* events = status.FindList("events")) {
    for (const base::Value& event : *events) {
      if (std::optional<Message> m = ParseMessage(event.GetString())) {
        const std::string* reason = m->params.FindString("reason");
        const std::string* target = m->params.FindString("targetId");
        printf("event %s %s %s\n", m->method.c_str(),
               target ? target->c_str() : "", reason ? reason->c_str() : "");
      }
    }
  }
  return exit_code;
}

// A holder of an older koan, which knows no refs.
constexpr char kOtherHolder[] =
    "the connection to Koan was started by another version of koan; run koan "
    "end, then open the tab again";

// The tab's main frame: its id, and the document it shows (its loader id,
// which is new for every document). nullopt (printed) if the tab does not
// say.
struct MainFrame {
  std::string id;
  std::string document;
};
std::optional<MainFrame> MainFrameOf(Link& link,
                                     const std::string& session,
                                     int* exit_code) {
  std::optional<base::DictValue> frames = Expect(
      link.Call("Page.getFrameTree", base::DictValue(), session), exit_code);
  const std::string* id =
      frames ? frames->FindStringByDottedPath("frameTree.frame.id") : nullptr;
  const std::string* loader =
      frames ? frames->FindStringByDottedPath("frameTree.frame.loaderId")
             : nullptr;
  if (!id || !loader) {
    if (frames) {
      *exit_code = Fail(
          "Koan.pageError: the tab did not say which page it shows; retry");
    }
    return std::nullopt;
  }
  return MainFrame{*id, *loader};
}

std::optional<std::string> DocumentOf(Link& link,
                                      const std::string& session,
                                      int* exit_code) {
  std::optional<MainFrame> frame = MainFrameOf(link, session, exit_code);
  if (!frame) {
    return std::nullopt;
  }
  return std::move(frame->document);
}

// Sends one `method` for each of `params` at once, on the tab's session,
// and waits for all their answers (events meanwhile go to the link's sink):
// a few round trips' time, not one each. An answer missing (no answer in
// time) is nullopt.
std::vector<std::optional<Message>> CallEach(
    Link& link,
    std::string_view method,
    std::vector<base::DictValue> params,
    const std::string& session) {
  std::vector<std::optional<Message>> answers(params.size());
  std::map<int, size_t> pending;
  for (size_t i = 0; i < params.size(); ++i) {
    if (const int id = link.Send(method, std::move(params[i]), session)) {
      pending.emplace(id, i);
    }
  }
  const base::TimeTicks deadline = base::TimeTicks::Now() + kAnswerWait;
  while (!pending.empty()) {
    std::optional<Message> message = link.Next(deadline);
    if (!message) {
      break;
    }
    if (!message->id) {
      link.PassEvent(*message);
      continue;
    }
    if (auto it = pending.find(*message->id); it != pending.end()) {
      answers[it->second] = std::move(message);
      pending.erase(it);
    }
  }
  return answers;
}

// The outline cut to what the viewport shows (ViewportSearch: a few rounds
// of DOM.getContentQuads on some of its lines, not all). nullopt (printed)
// if the viewport's size is not known.
std::optional<std::string> ViewportText(Link& link,
                                        const Snapshot& snapshot,
                                        const std::string& session,
                                        int* exit_code) {
  std::optional<base::DictValue> metrics =
      Expect(link.Call("Page.getLayoutMetrics", base::DictValue(), session),
             exit_code);
  if (!metrics) {
    return std::nullopt;
  }
  const base::DictValue* layout = metrics->FindDict("cssLayoutViewport");
  if (!layout) {
    layout = metrics->FindDict("cssVisualViewport");
  }
  const double height =
      layout ? layout->FindDouble("clientHeight").value_or(0) : 0;
  if (height <= 0) {
    *exit_code = Fail(
        "Koan.pageError: the tab did not say how big its viewport is; use "
        "--scope full");
    return std::nullopt;
  }
  // The lines the scope cuts (not the page's own line, not the popups).
  std::vector<size_t> lines;
  for (size_t i = 0; i < snapshot.lines.size(); ++i) {
    if (!snapshot.lines[i].pinned) {
      lines.push_back(i);
    }
  }
  ViewportSearch search(lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!snapshot.lines[lines[i]].node) {
      search.Report(i, ViewportSearch::Place::kUnknown);
    }
  }
  for (int round = 0; round < 10 && !search.done(); ++round) {
    const std::vector<size_t> probes = search.NextProbes();
    std::vector<base::DictValue> params;
    for (size_t probe : probes) {
      params.push_back(base::DictValue().Set(
          "backendNodeId", snapshot.lines[lines[probe]].node));
    }
    std::vector<std::optional<Message>> answers =
        CallEach(link, "DOM.getContentQuads", std::move(params), session);
    for (size_t i = 0; i < probes.size(); ++i) {
      const base::ListValue* quads = answers[i] && answers[i]->result
                                         ? answers[i]->result->FindList("quads")
                                         : nullptr;
      const base::ListValue* quad =
          quads && !quads->empty() ? (*quads)[0].GetIfList() : nullptr;
      ViewportSearch::Place place = ViewportSearch::Place::kUnknown;
      if (quad && quad->size() == 8) {
        double top = height;
        for (size_t y = 1; y < 8; y += 2) {
          if ((*quad)[y].is_double() || (*quad)[y].is_int()) {
            top = std::min(top, (*quad)[y].GetDouble());
          }
        }
        place = top < 0         ? ViewportSearch::Place::kAbove
                : top >= height ? ViewportSearch::Place::kBelow
                                : ViewportSearch::Place::kIn;
      }
      search.Report(probes[i], place);
    }
  }
  if (search.empty()) {
    // Nothing of the outline is in view: everything above (scrolled past
    // it) or below.
    const size_t at = search.first() < lines.size() ? lines[search.first()]
                                                    : snapshot.lines.size();
    return RenderWindow(snapshot, at, at);
  }
  return RenderWindow(snapshot, lines[search.first()],
                      lines[std::min(search.last(), lines.size() - 1)] + 1);
}

// What a snapshot shows (koan snapshot's options, and koan act's for the
// snapshot after its steps).
struct SnapshotRequest {
  bool full = false;
  bool interactive = false;
  bool diff = false;
  bool locators = false;
  // viewport, full, auto ("" too), @ref.
  std::string scope;
};
SnapshotRequest SnapshotRequestOf(const Command& command) {
  return {command.full, command.interactive, command.diff, command.locators,
          command.scope};
}

// The outline of the tab, as `request` says, its refs (and its lines, for
// the next --diff) remembered by the holder; `tree` gets the raw tree.
// nullopt (printed) if it could not be read.
std::optional<std::string> TakeSnapshot(Link& link,
                                        const SnapshotRequest& request,
                                        const std::string& target,
                                        const std::string& session,
                                        int* exit_code,
                                        base::DictValue* tree_out = nullptr) {
  // Read between two looks at the document, so that the refs are known to
  // belong to the page that was read.
  std::optional<base::DictValue> tree;
  std::optional<std::string> document;
  for (int attempt = 0; attempt < 2 && !document; ++attempt) {
    std::optional<std::string> before = DocumentOf(link, session, exit_code);
    if (!before) {
      return std::nullopt;
    }
    tree = Expect(
        link.Call("Accessibility.getFullAXTree", base::DictValue(), session),
        exit_code);
    if (!tree) {
      return std::nullopt;
    }
    std::optional<std::string> after = DocumentOf(link, session, exit_code);
    if (!after) {
      return std::nullopt;
    }
    if (*after == *before) {
      document = std::move(after);
    }
  }
  if (!document) {
    *exit_code = Fail(
        "Koan.pageChanged: the page kept changing while it was read (it is "
        "loading or navigating); retry in a moment");
    return std::nullopt;
  }
  const bool viewport = request.scope == "viewport";
  const bool automatic = request.scope.empty() || request.scope == "auto";
  SnapshotOptions options;
  options.full = request.full;
  options.interactive = request.interactive;
  options.locators = request.locators;
  options.root = ParseRef(request.scope).value_or(0);
  options.max_lines = viewport || (automatic && !request.full)
                          ? kScopedSnapshotLines
                      : request.full ? kFullSnapshotLines
                                     : kSnapshotLines;
  Snapshot snapshot = BuildSnapshot(*tree, options);
  if (snapshot.root_missing) {
    *exit_code = Fail("Koan.staleRef: " + request.scope +
                      " is not on the page now; take a new snapshot and use "
                      "its refs");
    return std::nullopt;
  }
  base::ListValue ids;
  for (int id : snapshot.refs) {
    ids.Append(id);
  }
  Baseline now;
  now.document = *document;
  now.kind = BaselineKind(options);
  now.lines = snapshot.lines;
  std::optional<Message> kept = link.Call(
      "Hold.rememberRefs", base::DictValue()
                               .Set("targetId", target)
                               .Set("document", *document)
                               .Set("refs", std::move(ids))
                               .Set("baseline", BaselineToValue(now)));
  if (!kept || !kept->result) {
    *exit_code = Fail(kOtherHolder);
    return std::nullopt;
  }
  std::string text = std::move(snapshot.text);
  if (viewport ||
      (automatic && !request.full && snapshot.lines.size() > kAutoScopeLines)) {
    std::optional<std::string> cut =
        ViewportText(link, snapshot, session, exit_code);
    if (!cut) {
      return std::nullopt;
    }
    text = std::move(*cut);
  }
  if (request.diff) {
    std::optional<Baseline> previous;
    if (const base::DictValue* was = kept->result->FindDict("previous")) {
      previous = BaselineFromValue(*was);
    }
    text = kept->result->FindBool("baselines").value_or(false)
               ? DiffText(previous, now, text)
               : "baseline reset (this connection was started by an older "
                 "koan, which keeps no snapshots; koan end, then open the tab "
                 "again); the whole snapshot:\n" +
                     text;
  }
  if (tree_out) {
    *tree_out = std::move(*tree);
  }
  return text;
}

int PrintSnapshot(Link& link,
                  const Command& command,
                  const std::string& target,
                  const std::string& session) {
  int exit_code = kOk;
  base::DictValue tree;
  std::optional<std::string> text = TakeSnapshot(
      link, SnapshotRequestOf(command), target, session, &exit_code, &tree);
  if (!text) {
    return exit_code;
  }
  if (command.json) {
    PrintJson(tree);
  } else {
    printf("%s", text->c_str());
  }
  return kOk;
}

// Whether `command.ref` belongs to the page the tab shows now (refused,
// printed, if not: Koan.staleRef).
bool RefIsCurrent(Link& link,
                  const Command& command,
                  const std::string& target,
                  const std::string& session,
                  int* exit_code) {
  std::optional<std::string> document = DocumentOf(link, session, exit_code);
  if (!document) {
    return false;
  }
  std::optional<Message> answer = link.Call(
      "Hold.checkRef", base::DictValue()
                           .Set("targetId", target)
                           .Set("document", *document)
                           .Set("ref", ParseRef(command.ref).value_or(0)));
  const std::string* name = answer && answer->result
                                ? answer->result->FindString("verdict")
                                : nullptr;
  const std::optional<RefVerdict> verdict =
      name ? RefVerdictFromName(*name) : std::nullopt;
  if (!verdict) {
    *exit_code = Fail(kOtherHolder);
    return false;
  }
  if (*verdict != RefVerdict::kOk) {
    *exit_code = Fail(RefRefusal(*verdict, command.ref, command.tab));
    return false;
  }
  return true;
}

// A ref's node as a remote object. nullopt and `error` if not.
std::optional<std::string> ResolveRef(Link& link,
                                      const std::string& session,
                                      int node,
                                      std::string* error) {
  std::optional<Message> answer = link.Call(
      "DOM.resolveNode", base::DictValue().Set("backendNodeId", node), session);
  const std::string* object_id =
      answer && answer->result
          ? answer->result->FindStringByDottedPath("object.objectId")
          : nullptr;
  if (!object_id) {
    *error = answer && answer->is_error()
                 ? Describe(answer)
                 : "Koan.staleRef: that element is gone from the page; take "
                   "a new snapshot and use its refs";
    return std::nullopt;
  }
  return *object_id;
}

// Runs one of koan_cli_actions.h's functions on a remote object. Its
// Runtime.callFunctionOn result, or nullopt and `error`. `by_value`: the
// result as JSON (else a remote object); `await`: the function's promise.
std::optional<base::DictValue> RunOn(Link& link,
                                     const std::string& session,
                                     const std::string& object_id,
                                     const char* function,
                                     base::ListValue arguments,
                                     std::string* error,
                                     bool by_value = true,
                                     bool await = false) {
  base::DictValue params;
  params.Set("objectId", object_id);
  params.Set("functionDeclaration", function);
  params.Set("returnByValue", by_value);
  if (await) {
    params.Set("awaitPromise", true);
  }
  if (!arguments.empty()) {
    params.Set("arguments", std::move(arguments));
  }
  std::optional<Message> answer =
      link.Call("Runtime.callFunctionOn", std::move(params), session);
  if (!answer || answer->is_error() || !answer->result) {
    *error = Describe(answer);
    return std::nullopt;
  }
  return answer->result->Clone();
}

base::ListValue Args(std::initializer_list<base::Value> values) {
  base::ListValue list;
  for (const base::Value& value : values) {
    list.Append(base::DictValue().Set("value", value.Clone()));
  }
  return list;
}

// What an action did, before the page settles: a failure (printed, its exit
// code), or what "ok" says and the JSON with it.
struct Acted {
  int exit_code = kOk;
  std::string line = "ok";
  base::DictValue json;
  // choose on a custom dropdown: the option it clicked, checked once the
  // page has settled.
  std::string chosen;
  std::string control;
  // navigate: the server answered an error status with no page of its own,
  // so the tab shows Chromium's error page.
  bool http_error = false;
};

Acted Failed(const std::string& text, int code = kFailed) {
  Acted acted;
  acted.exit_code = Fail(text, code);
  return acted;
}

// Chooses an option of the ref's <select> by `wanted` (its label or value),
// or, with no `wanted`, the ref's own <option>: set in the page, with the
// events a user's choice fires, so no popup opens (a background tab has
// none to show). With `quiet`, nullopt and nothing printed if the node is
// neither, so that the caller reports its own error.
std::optional<Acted> ChooseOption(Link& link,
                                  const Command& command,
                                  const std::string& session,
                                  const std::string* wanted,
                                  bool quiet) {
  std::string error;
  std::optional<std::string> object_id =
      ResolveRef(link, session, ParseRef(command.ref).value_or(0), &error);
  base::ListValue arguments;
  if (wanted) {
    arguments.Append(base::DictValue().Set("value", *wanted));
  }
  std::optional<base::DictValue> result =
      object_id ? RunOn(link, session, *object_id, kSelectOptionFunction,
                        std::move(arguments), &error)
                : std::nullopt;
  if (!result || (quiet && IsNotSelect(ReturnedValue(*result)))) {
    if (quiet) {
      return std::nullopt;
    }
    return Failed(error);
  }
  const SelectOutcome outcome =
      ReadSelectOutcome(ReturnedValue(*result), command.ref);
  if (!outcome.ok) {
    return Failed(outcome.text);
  }
  Acted acted;
  acted.line = outcome.text;
  acted.json.Set("selected", outcome.label);
  acted.json.Set("value", outcome.value);
  return acted;
}

// The node `spec` names, as koan act finds it: a locator in the page as it is
// now (no snapshot needed, nothing guessed), or a snapshot's @ref that
// belongs to the page the tab shows now. nullopt (printed) if neither.
std::optional<int> FindNode(Link& link,
                            const Command& command,
                            std::string_view spec,
                            const std::string& target,
                            const std::string& session,
                            int* exit_code) {
  if (spec.starts_with("loc:")) {
    std::string error;
    std::optional<Locator> locator = ParseLocator(spec, &error);
    if (!locator) {
      *exit_code = Fail(error, kUsage);
      return std::nullopt;
    }
    std::optional<base::DictValue> tree = Expect(
        link.Call("Accessibility.getFullAXTree", base::DictValue(), session),
        exit_code);
    if (!tree) {
      return std::nullopt;
    }
    const LocatorMatch match = MatchLocator(*tree, *locator, spec);
    if (!match.node) {
      *exit_code = Fail(match.error);
      return std::nullopt;
    }
    return match.node;
  }
  const std::optional<int> ref = ParseRef(spec);
  if (!ref) {
    *exit_code = Fail(
        std::string(spec) + " is neither a ref (@12) nor a locator (loc:...)",
        kUsage);
    return std::nullopt;
  }
  Command probe = command;
  probe.ref = std::string(spec);
  if (!RefIsCurrent(link, probe, target, session, exit_code)) {
    return std::nullopt;
  }
  return ref;
}

// `select @ref <option>` on a <select>, which, not quiet, always answers.
Acted Selected(Link& link, const Command& command, const std::string& session) {
  return std::move(*ChooseOption(link, command, session, &command.text,
                                 /*quiet=*/false));
}

// A click as a user's: the node scrolled into view, its box still (an
// opening popup's animation, a list that repositions itself after the
// scroll), then the pointer moved to its centre and pressed and released
// there with the left button down (buttons: 1, so that pointerdown and
// mousedown say a button is pressed, as the pages that check it expect).
// `node`: {backendNodeId} or {objectId}. The failed answer, if any (an
// <option> of a closed <select> has no box), in `failed`; `note` says what
// covered the node, if something did.
bool Click(Link& link,
           const std::string& session,
           const base::DictValue& node,
           std::optional<Message>* failed,
           std::string* note) {
  std::optional<Message> answer =
      link.Call("DOM.scrollIntoViewIfNeeded", node.Clone(), session);
  if (!answer || answer->is_error()) {
    *failed = std::move(answer);
    return false;
  }
  std::string ignored;
  std::optional<std::string> object_id;
  if (const std::string* given = node.FindString("objectId")) {
    object_id = *given;
  } else if (std::optional<int> backend = node.FindInt("backendNodeId")) {
    object_id = ResolveRef(link, session, *backend, &ignored);
  }
  ClickReadiness readiness;
  if (object_id) {
    if (std::optional<base::DictValue> ready =
            RunOn(link, session, *object_id, kClickReadyFunction,
                  base::ListValue(), &ignored, /*by_value=*/true,
                  /*await=*/true)) {
      readiness = ReadClickReadiness(ReturnedValue(*ready));
    }
  }
  answer = link.Call("DOM.getContentQuads", node.Clone(), session);
  if (!answer || answer->is_error() || !answer->result) {
    *failed = std::move(answer);
    return false;
  }
  std::optional<std::pair<double, double>> center = QuadCenter(*answer->result);
  if (!center) {
    *failed = Message();
    (*failed)->error_code = "Koan.notVisible";
    (*failed)->error_message =
        "that element is not on the screen; scroll to it, or take a new "
        "snapshot";
    return false;
  }
  if (!readiness.covered.empty()) {
    *note =
        "the click landed on " + readiness.covered + ", which covers it there";
  }
  auto mouse = [&](std::string_view type, int buttons) {
    base::DictValue params;
    params.Set("type", type);
    params.Set("x", center->first);
    params.Set("y", center->second);
    if (type != "mouseMoved") {
      params.Set("button", "left");
      params.Set("clickCount", 1);
    }
    params.Set("buttons", buttons);
    std::optional<Message> sent =
        link.Call("Input.dispatchMouseEvent", std::move(params), session);
    if (!sent || sent->is_error()) {
      *failed = std::move(sent);
      return false;
    }
    return true;
  };
  return mouse("mouseMoved", 0) && mouse("mousePressed", 1) &&
         mouse("mouseReleased", 0);
}

// The options of the list the ref's control opened (koan_cli_actions.h).
OptionList ListOptions(Link& link,
                       const std::string& session,
                       const std::string& control) {
  std::string ignored;
  std::optional<base::DictValue> listed =
      RunOn(link, session, control, kListOptionsFunction,
            Args({base::Value(-1), base::Value("")}), &ignored);
  return listed ? ReadOptionList(ReturnedValue(*listed)) : OptionList();
}

// Polls the control's list until `done` says it is what was waited for, or
// `wait` ends.
OptionList WaitForOptions(
    Link& link,
    const std::string& session,
    const std::string& control,
    base::TimeDelta wait,
    base::RepeatingCallback<bool(const OptionList&)> done) {
  const base::TimeTicks until = base::TimeTicks::Now() + wait;
  OptionList list = ListOptions(link, session, control);
  while (!done.Run(list) && base::TimeTicks::Now() < until) {
    usleep(100 * 1000);
    list = ListOptions(link, session, control);
  }
  return list;
}

// Clicks the option at `index` (still labelled `label`) of the control's
// list.
Acted ClickOption(Link& link,
                  const std::string& session,
                  const std::string& control,
                  size_t index,
                  const std::string& label) {
  std::string error;
  std::optional<base::DictValue> picked =
      RunOn(link, session, control, kListOptionsFunction,
            Args({base::Value(static_cast<int>(index)), base::Value(label)}),
            &error, /*by_value=*/false);
  const std::string* option =
      picked ? picked->FindStringByDottedPath("result.objectId") : nullptr;
  if (!option) {
    return Failed(picked ? "Koan.pageChanged: the list changed while it was "
                           "read; retry"
                         : error);
  }
  std::optional<Message> failed;
  std::string note;
  if (!Click(link, session, base::DictValue().Set("objectId", *option), &failed,
             &note)) {
    return Failed(Describe(failed));
  }
  Acted acted;
  acted.line = "ok: chose \"" + label + "\"";
  if (!note.empty()) {
    acted.line += " (" + note + ")";
  }
  acted.json.Set("chosen", label);
  acted.chosen = label;
  acted.control = control;
  return acted;
}

// `act choose`: a <select> as select does; a custom dropdown opened (a
// click on it, unless its list is open), its list waited for, the option
// that `command.text` names clicked (typed into first if the control is a
// text field and the option is not shown). Checked once the page settles
// (CheckChosen()).
Acted Choose(Link& link, const Command& command, const std::string& session) {
  const int node = ParseRef(command.ref).value_or(0);
  std::string error;
  std::optional<std::string> control = ResolveRef(link, session, node, &error);
  std::optional<base::DictValue> probe =
      control ? RunOn(link, session, *control, kChooseKindFunction,
                      base::ListValue(), &error)
              : std::nullopt;
  if (!probe) {
    return Failed(error);
  }
  const ChooseTarget kind =
      ReadChooseTarget(ReturnedValue(*probe), command.ref);
  if (kind.kind == ChooseKind::kSelect) {
    return Selected(link, command, session);
  }
  if (kind.kind == ChooseKind::kRefused) {
    return Failed(kind.refusal);
  }
  const auto shown = base::BindRepeating([](const OptionList& list) {
    return list.found && !list.labels.empty();
  });
  OptionList list = ListOptions(link, session, *control);
  // Opened unless its own list shows already: another list on the page
  // says nothing about this control.
  if (!shown.Run(list) || !(list.mine || kind.expanded)) {
    std::optional<Message> failed;
    std::string note;
    if (!Click(link, session, base::DictValue().Set("backendNodeId", node),
               &failed, &note)) {
      return Failed(Describe(failed));
    }
    list = WaitForOptions(link, session, *control, base::Seconds(2), shown);
  }
  OptionMatch match = MatchOption(list, command.text, command.ref);
  if (!match.index && kind.editable) {
    // An autocomplete shows the options that match what is typed.
    std::optional<Message> focused = link.Call(
        "DOM.focus", base::DictValue().Set("backendNodeId", node), session);
    std::optional<Message> typed;
    if (focused && !focused->is_error() &&
        RunOn(link, session, *control, kSelectContentsFunction,
              base::ListValue(), &error)) {
      typed = link.Call("Input.insertText",
                        base::DictValue().Set("text", command.text), session);
    }
    if (typed && !typed->is_error()) {
      list = WaitForOptions(
          link, session, *control, base::Seconds(2),
          base::BindRepeating(
              [](const std::string& wanted, const std::string& ref,
                 const OptionList& list) {
                return list.found &&
                       MatchOption(list, wanted, ref).index.has_value();
              },
              command.text, command.ref));
      match = MatchOption(list, command.text, command.ref);
    }
  }
  if (!match.index) {
    return Failed(list.found ? match.error : NoListText(command.ref));
  }
  return ClickOption(link, session, *control, *match.index,
                     list.labels[*match.index]);
}

// Whether the control shows the chosen option now.
bool CheckChosen(Link& link,
                 const std::string& session,
                 const Acted& acted,
                 std::string* now) {
  std::string ignored;
  std::optional<base::DictValue> read =
      RunOn(link, session, acted.control, kChosenValueFunction,
            Args({base::Value(acted.chosen)}), &ignored);
  const base::Value* value = read ? ReturnedValue(*read) : nullptr;
  const base::DictValue* dict = value ? value->GetIfDict() : nullptr;
  if (!dict) {
    // The page went (the choice navigated it): nothing to check.
    return true;
  }
  if (const std::string* shows = dict->FindString("now")) {
    *now = *shows;
  }
  return dict->FindBool("has").value_or(false);
}

// How long cleanup and a stop wait for their answers.
constexpr base::TimeDelta kCleanupWait = base::Seconds(2);

// While an action runs and its page settles: the tab's Page and Network
// events come to this koan (Hold.follow); afterwards they stop. With a
// holder that does not know Hold.follow, no events come, and the settle
// waits on the DOM and its cap alone.
class SettleWatch {
 public:
  SettleWatch(Link& link, const std::string& session)
      : link_(link), session_(session) {
    followed_ = Ok(link_->Call("Hold.follow",
                               base::DictValue().Set("sessionId", session_)));
    if (followed_) {
      page_ = Ok(link_->Call("Page.enable", base::DictValue(), session_));
      network_ = Ok(link_->Call("Network.enable", base::DictValue(), session_));
    }
  }
  SettleWatch(const SettleWatch&) = delete;
  SettleWatch& operator=(const SettleWatch&) = delete;
  ~SettleWatch() {
    // A moment at most: while the tab navigates Chromium holds its
    // renderer's commands (these two among them) until the navigation
    // commits or fails.
    if (network_) {
      link_->Call("Network.disable", base::DictValue(), session_, kCleanupWait);
    }
    if (page_) {
      link_->Call("Page.disable", base::DictValue(), session_, kCleanupWait);
    }
    if (followed_) {
      link_->Call("Hold.follow", base::DictValue(), {}, kCleanupWait);
    }
  }

 private:
  static bool Ok(const std::optional<Message>& answer) {
    return answer && !answer->is_error();
  }

  const raw_ref<Link> link_;
  const std::string session_;
  bool followed_ = false;
  bool page_ = false;
  bool network_ = false;
};

// Stops the tab's navigation (Page.stopLoading, which the browser answers
// at once). Chromium answers Page.navigate only when the navigation commits
// or fails, and holds the tab's renderer commands until then: a navigation
// to a server that never answers would keep the tab (and its lease) from the
// agent for good.
void StopNavigation(Link& link, const std::string& session) {
  link.Call("Page.stopLoading", base::DictValue(), session, kCleanupWait);
}

// Reads the tab's events into `settle` and probes the DOM as it asks
// (koan_cli_settle.h), until the page settles, or, with `answer_id`, until
// that request's answer comes (in `answer`; kWaiting is returned then).
SettleState Pump(Link& link,
                 const std::string& session,
                 Settle& settle,
                 const SettleTimings& timings,
                 int answer_id = 0,
                 std::optional<Message>* answer = nullptr) {
  int probe = 0;
  while (true) {
    const base::TimeTicks now = base::TimeTicks::Now();
    const SettleState state = settle.Check(now);
    if (answer_id ? (state == SettleState::kTimedOut ||
                     state == SettleState::kBlocked)
                  : state != SettleState::kWaiting) {
      return state;
    }
    if (!answer_id && !probe && settle.WantsDomProbe(now)) {
      probe = link.Send(
          "Runtime.evaluate",
          base::DictValue()
              .Set("expression", DomQuietExpression(timings.dom_quiet,
                                                    settle.ProbeBudget(now)))
              .Set("awaitPromise", true)
              .Set("returnByValue", true),
          session);
      if (probe) {
        settle.OnDomProbeSent(now);
      } else {
        settle.OnDomProbe(Settle::DomProbe::kFailed, now);
      }
    }
    std::optional<Message> message = link.Next(
        std::min(settle.NextCheck(now), now + base::Milliseconds(500)));
    if (!message) {
      if (link.closed()) {
        settle.Block("Koan.notConnected: the connection to Koan closed");
      }
      continue;
    }
    if (message->id) {
      if (answer_id && *message->id == answer_id) {
        *answer = std::move(message);
        return SettleState::kWaiting;
      }
      if (probe && *message->id == probe) {
        probe = 0;
        Settle::DomProbe result = Settle::DomProbe::kFailed;
        if (message->is_error()) {
          const AgentError error =
              MapError(message->error_code, message->error_message);
          if (error.code == "Koan.pageChanged") {
            result = Settle::DomProbe::kPageChanged;
          } else if (error.code == "Koan.paused" ||
                     error.code == "Koan.userNeeded" ||
                     error.code == "Koan.interstitial") {
            settle.Block(error.code + ": " + error.message);
          }
        } else if (message->result) {
          const base::Value* value = ReturnedValue(*message->result);
          const base::DictValue* dict = value ? value->GetIfDict() : nullptr;
          if (dict) {
            result = dict->FindBool("quiet").value_or(false)
                         ? Settle::DomProbe::kQuiet
                         : Settle::DomProbe::kBusy;
          }
        }
        settle.OnDomProbe(result, base::TimeTicks::Now());
      }
      continue;
    }
    if (message->is_error()) {
      // Closing, and saying why.
      settle.Block(Describe(message));
      continue;
    }
    // To the step's settle, and to the batch's gate (EventTap).
    link.PassEvent(*message);
  }
}

// The action itself, the page not yet settled (Act() waits for that).
Acted DoAction(Link& link,
               const Command& command,
               const std::string& session,
               Settle& settle,
               const SettleTimings& timings) {
  int exit_code = kOk;
  auto call = [&](std::string_view method,
                  base::DictValue params) -> std::optional<base::DictValue> {
    return Expect(link.Call(method, std::move(params), session), &exit_code);
  };
  auto failed = [&] {
    Acted acted;
    acted.exit_code = exit_code;
    return acted;
  };
  const int node = ParseRef(command.ref).value_or(0);
  switch (command.act) {
    case ActKind::kNavigate: {
      const int id = link.Send(
          "Page.navigate", base::DictValue().Set("url", command.text), session);
      std::optional<Message> answer;
      const SettleState state =
          id ? Pump(link, session, settle, timings, id, &answer)
             : SettleState::kBlocked;
      if (!answer) {
        if (state == SettleState::kTimedOut) {
          StopNavigation(link, session);
          return Failed(
              NavigationTimeoutText(command.text, timings.navigation_timeout));
        }
        return Failed(!settle.blocked_reason().empty()
                          ? settle.blocked_reason()
                          : "Koan.notConnected: the connection to Koan "
                            "closed");
      }
      if (answer->is_error() || !answer->result) {
        return Failed(Describe(answer));
      }
      // A 4xx or 5xx with no page of its own shows Chromium's error page:
      // the server answered, and the status says what.
      const std::string* failed_text = answer->result->FindString("errorText");
      if (failed_text &&
          *failed_text != "net::ERR_HTTP_RESPONSE_CODE_FAILURE") {
        return Failed("Koan.loadFailed: the page did not load (" +
                      *failed_text + "); check the URL, or retry");
      }
      settle.OnNavigateAnswered(base::TimeTicks::Now());
      Acted acted;
      acted.http_error = failed_text != nullptr;
      return acted;
    }
    case ActKind::kPress: {
      std::optional<base::DictValue> down = KeyDownFor(command.text);
      if (!down) {
        return Failed("unknown key \"" + command.text +
                          "\" (Enter, Tab, Escape, Backspace, Arrow..., or "
                          "one character)",
                      kUsage);
      }
      base::DictValue up = down->Clone();
      up.Set("type", "keyUp");
      up.Remove("text");
      if (!call("Input.dispatchKeyEvent", std::move(*down)) ||
          !call("Input.dispatchKeyEvent", std::move(up))) {
        return failed();
      }
      return Acted();
    }
    case ActKind::kScroll: {
      std::optional<base::DictValue> metrics =
          call("Page.getLayoutMetrics", base::DictValue());
      if (!metrics) {
        return failed();
      }
      const base::DictValue* viewport = metrics->FindDict("cssVisualViewport");
      const double x =
          viewport ? viewport->FindDouble("clientWidth").value_or(200) / 2
                   : 200;
      const double y =
          viewport ? viewport->FindDouble("clientHeight").value_or(200) / 2
                   : 200;
      base::DictValue wheel;
      wheel.Set("type", "mouseWheel");
      wheel.Set("x", x);
      wheel.Set("y", y);
      wheel.Set("deltaX", 0);
      wheel.Set("deltaY", command.scroll_by);
      if (!call("Input.dispatchMouseEvent", std::move(wheel))) {
        return failed();
      }
      return Acted();
    }
    case ActKind::kClick: {
      std::optional<Message> answer;
      std::string note;
      if (!Click(link, session, base::DictValue().Set("backendNodeId", node),
                 &answer, &note)) {
        // An <option> of a closed dropdown has no box of its own; a click on
        // it means choosing it.
        if (answer && answer->is_error()) {
          if (std::optional<Acted> chosen = ChooseOption(
                  link, command, session, nullptr, /*quiet=*/true)) {
            return std::move(*chosen);
          }
        }
        return Failed(Describe(answer));
      }
      Acted acted;
      if (!note.empty()) {
        acted.line = "ok (" + note + ")";
      }
      return acted;
    }
    case ActKind::kSelect:
      return Selected(link, command, session);
    case ActKind::kChoose:
      return Choose(link, command, session);
    case ActKind::kAutofill: {
      // Chrome's own fill, as the user's pick of the row (Koan.autofill).
      std::optional<Message> answer = link.Call(
          "Koan.autofill", AutofillParams(node, /*list=*/false, command.text),
          session, kAutofillWait);
      if (!answer || answer->is_error() || !answer->result) {
        return Failed(Describe(answer));
      }
      const AutofillOutcome outcome =
          ReadAutofillPick(*answer->result, command.tab);
      if (!outcome.filled) {
        // The batch stops: the user verifies, or nothing was filled.
        return Failed(outcome.line);
      }
      Acted acted;
      acted.line = outcome.line;
      acted.json.Set("filled", true);
      if (const std::string* kind = answer->result->FindString("kind")) {
        acted.json.Set("kind", *kind);
      }
      if (const std::string* label = answer->result->FindString("label")) {
        acted.json.Set("label", *label);
      }
      return acted;
    }
    case ActKind::kUpload: {
      // The files first: nothing is clicked for one that isn't there.
      base::FilePath cwd;
      base::GetCurrentDirectory(&cwd);
      std::vector<UploadPath> paths;
      std::string error;
      if (!ResolveUploadPaths(command.files, cwd, &paths, &error)) {
        return Failed(error);
      }
      // The click opens the page's chooser, which Koan holds for the agent
      // (Koan.userNeeded file-picker): this step answers it.
      settle.ExpectFileChooser();
      std::optional<Message> answer;
      std::string note;
      if (!Click(link, session, base::DictValue().Set("backendNodeId", node),
                 &answer, &note)) {
        return Failed(Describe(answer));
      }
      std::optional<Message> uploaded =
          link.Call("Koan.upload", UploadParams(paths, kUploadWait), session,
                    kUploadAnswerWait);
      if (!uploaded || uploaded->is_error() || !uploaded->result) {
        return Failed(Describe(uploaded));
      }
      Acted acted;
      acted.line = UploadedLine(*uploaded->result);
      if (const base::ListValue* files = uploaded->result->FindList("files")) {
        acted.json.Set("files", files->Clone());
      }
      return acted;
    }
    case ActKind::kFill: {
      std::string error;
      std::optional<std::string> object_id =
          ResolveRef(link, session, node, &error);
      std::optional<base::DictValue> probe =
          object_id ? RunOn(link, session, *object_id, kFillTargetFunction,
                            base::ListValue(), &error)
                    : std::nullopt;
      if (!probe && error.starts_with("Koan.secretField")) {
        // A secret Koan filled is in the page, so its script waits: the
        // text is typed at the field without reading it first or after.
        if (!call("DOM.focus", base::DictValue().Set("backendNodeId", node)) ||
            !call("Input.insertText",
                  base::DictValue().Set("text", command.text))) {
          return failed();
        }
        Acted acted;
        acted.line =
            "ok: typed (a secret Koan filled is in this page, so the field "
            "was not cleared first or read back)";
        return acted;
      }
      if (!probe) {
        return Failed(error);
      }
      const FillTarget fill =
          ReadFillTarget(ReturnedValue(*probe), command.ref);
      if (fill.kind == FillKind::kSelect) {
        return Selected(link, command, session);
      }
      if (fill.kind == FillKind::kRefused) {
        return Failed(fill.refusal);
      }
      // What is typed replaces what the field holds.
      if (!call("DOM.focus", base::DictValue().Set("backendNodeId", node))) {
        return failed();
      }
      if (!RunOn(link, session, *object_id, kSelectContentsFunction,
                 base::ListValue(), &error)) {
        return Failed(error);
      }
      if (!call("Input.insertText",
                base::DictValue().Set("text", command.text))) {
        return failed();
      }
      std::optional<base::DictValue> read =
          RunOn(link, session, *object_id, kReadTextFunction, base::ListValue(),
                &error);
      if (!read) {
        return Failed(error);
      }
      const base::Value* held = ReturnedValue(*read);
      const std::string now =
          held && held->is_string() ? held->GetString() : std::string();
      const FillOutcome outcome = CheckFill(fill.before, now, command.text);
      const std::string line =
          FillOutcomeText(outcome, command.ref, now, fill.secret);
      if (outcome == FillOutcome::kUnchanged) {
        return Failed(line);
      }
      Acted acted;
      acted.line = line;
      return acted;
    }
  }
  return Failed("not an action", kUsage);
}

const char* SettleStateName(SettleState state) {
  switch (state) {
    case SettleState::kSettled:
      return "quiet";
    case SettleState::kCapped:
      return "capped";
    case SettleState::kTimedOut:
      return "timedOut";
    case SettleState::kBlocked:
      return "blocked";
    case SettleState::kWaiting:
      break;
  }
  return "waiting";
}

// Where the tab's events go while `koan act` runs: the batch's gate, and
// the settle of the step that runs.
struct EventTap {
  raw_ptr<BatchGate> gate = nullptr;
  raw_ptr<Settle> settle = nullptr;
};

// What one step did.
struct StepDone {
  int exit_code = kOk;
  // Done: what "ok" says (SettledLine()); else the error.
  std::string line;
  base::DictValue json;
  // Done, and then the page waited for the user: why.
  std::string blocked;
};

// The command one step runs as.
Command StepCommand(const Command& command, const Step& step) {
  Command one = command;
  one.act = step.act;
  one.ref = step.target;
  one.text = step.text;
  one.scroll_by = step.scroll_by;
  one.files = step.files;
  return one;
}

// One step on a tab, through the CDP the browser allows an agent (the
// in-browser Koan.act of phase 3 replaces this). A ref must be from a
// snapshot of the page the tab shows now; a locator must name one element
// of it. Once the action is done, waits for the page to settle
// (koan_cli_settle.h). Prints nothing: the failure is kept for the receipt.
StepDone RunStep(Link& link,
                 const Command& given,
                 const std::string& target,
                 const std::string& session,
                 EventTap& tap) {
  StepDone done;
  std::string failure;
  ScopedFailure keep(&failure);
  auto fail = [&](int code) {
    StepDone failed;
    failed.exit_code = code;
    failed.line = failure.empty() ? "failed" : failure;
    return failed;
  };
  int exit_code = kOk;
  Command command = given;
  const bool on_ref =
      command.act == ActKind::kClick || command.act == ActKind::kFill ||
      command.act == ActKind::kSelect || command.act == ActKind::kChoose ||
      command.act == ActKind::kAutofill || command.act == ActKind::kUpload;
  if (on_ref) {
    std::optional<int> node =
        FindNode(link, command, command.ref, target, session, &exit_code);
    if (!node) {
      return fail(exit_code);
    }
    command.ref = "@" + base::NumberToString(*node);
  }
  std::optional<MainFrame> frame = MainFrameOf(link, session, &exit_code);
  if (!frame) {
    return fail(exit_code);
  }
  SettleTimings timings;
  timings.navigation_timeout = base::Seconds(command.timeout_seconds);
  SettleState state = SettleState::kSettled;
  Acted acted;
  std::optional<Settle> settled;
  auto settle_from = [&](bool navigating) -> Settle& {
    tap.settle = nullptr;
    settled.emplace(timings, base::TimeTicks::Now(), navigating, target,
                    frame->id);
    tap.settle = &*settled;
    return *settled;
  };
  Settle& settle = settle_from(command.act == ActKind::kNavigate);
  acted = DoAction(link, command, session, settle, timings);
  if (acted.exit_code == kOk) {
    state = Pump(link, session, settle, timings);
  }
  // A custom dropdown's option that did not take is clicked once more (a
  // list that was still moving, a page that wanted a second click).
  std::string shows;
  if (acted.exit_code == kOk && !acted.chosen.empty() &&
      state != SettleState::kBlocked &&
      !CheckChosen(link, session, acted, &shows)) {
    OptionList list = ListOptions(link, session, acted.control);
    OptionMatch match = MatchOption(list, acted.chosen, command.ref);
    if (match.index) {
      Settle& again = settle_from(false);
      Acted retried = ClickOption(link, session, acted.control, *match.index,
                                  list.labels[*match.index]);
      if (retried.exit_code == kOk) {
        state = Pump(link, session, again, timings);
      }
    }
    if (!CheckChosen(link, session, acted, &shows)) {
      acted = Failed(NotChosenText(command.ref, acted.chosen, shows));
    }
  }
  tap.settle = nullptr;
  if (acted.exit_code != kOk) {
    return fail(acted.exit_code);
  }
  done.line = SettledLine(*settled, state, acted.line);
  if (acted.http_error && !settled->http_status()) {
    done.line += " (an HTTP error, and no page from the server)";
  }
  acted.json.Set("settled", SettleStateName(state));
  if (std::optional<int> status = settled->http_status()) {
    acted.json.Set("status", *status);
  }
  if (state == SettleState::kBlocked) {
    done.blocked = settled->blocked_reason();
  }
  done.json = std::move(acted.json);
  return done;
}

// Whether --until holds now; `shows` says what the page shows instead.
// nullopt if the page could not be asked (it is checked again).
std::optional<bool> UntilHolds(Link& link,
                               const Until& until,
                               const Command& command,
                               const std::string& target,
                               const std::string& session,
                               std::string* shows) {
  std::string failure;
  ScopedFailure keep(&failure);
  int exit_code = kOk;
  switch (until.kind) {
    case Until::kUrl: {
      std::optional<base::DictValue> frames =
          Expect(link.Call("Page.getFrameTree", base::DictValue(), session),
                 &exit_code);
      const std::string* url =
          frames ? frames->FindStringByDottedPath("frameTree.frame.url")
                 : nullptr;
      if (!url) {
        return std::nullopt;
      }
      const std::string* fragment =
          frames->FindStringByDottedPath("frameTree.frame.urlFragment");
      const std::string full = *url + (fragment ? *fragment : "");
      *shows = "the URL is " + full;
      return UrlMatches(full, until);
    }
    case Until::kText: {
      std::optional<base::DictValue> result = Expect(
          link.Call("Runtime.evaluate",
                    base::DictValue()
                        .Set("expression", PageTextExpression(until.needle))
                        .Set("returnByValue", true),
                    session),
          &exit_code);
      const base::Value* value = result ? ReturnedValue(*result) : nullptr;
      if (!value || !value->is_bool()) {
        return std::nullopt;
      }
      *shows = "the page's text does not have it";
      return value->GetBool();
    }
    case Until::kRefText:
    case Until::kRefGone: {
      Command probe = command;
      probe.ref = until.ref;
      // A ref of a page the tab no longer shows is gone.
      bool gone = !RefIsCurrent(link, probe, target, session, &exit_code);
      std::string error;
      std::optional<std::string> object_id =
          gone ? std::nullopt
               : ResolveRef(link, session, ParseRef(until.ref).value_or(0),
                            &error);
      gone = gone || !object_id;
      std::optional<base::DictValue> result =
          gone ? std::nullopt
               : RunOn(link, session, *object_id, kElementTextFunction,
                       Args({base::Value(until.needle)}), &error);
      *shows = until.kind == Until::kRefGone ? until.ref + " is still shown"
               : gone ? until.ref + " is gone"
                      : until.ref + "'s text does not have it";
      return ElementMeets(until, result ? ReturnedValue(*result) : nullptr,
                          gone);
    }
  }
  return std::nullopt;
}

// Waits until --until holds, the gate says stop, or --timeout. Whether it
// held; `line` says what happened.
bool WaitUntil(Link& link,
               const Until& until,
               const Command& command,
               const std::string& target,
               const std::string& session,
               const BatchGate& gate,
               std::string* line) {
  const base::TimeDelta wait = base::Seconds(command.timeout_seconds);
  const base::TimeTicks deadline = base::TimeTicks::Now() + wait;
  // The tab keeps rendering while it is waited on, even between polls and
  // with the user's window covered (design section 8).
  TabKeepAlive keep_alive(link, target);
  std::string shows;
  while (true) {
    keep_alive.RenewIfDue();
    if (std::optional<std::string> stop = gate.StopReason()) {
      *line = *stop;
      return false;
    }
    if (UntilHolds(link, until, command, target, session, &shows)
            .value_or(false)) {
      *line = "met";
      return true;
    }
    const base::TimeTicks now = base::TimeTicks::Now();
    if (now >= deadline) {
      break;
    }
    // What comes meanwhile (Koan.paused) reaches the gate.
    const base::TimeTicks next = std::min(deadline, now + kUntilPoll);
    while (std::optional<Message> message = link.Next(next)) {
      if (!message->id) {
        link.PassEvent(*message);
      }
    }
    if (link.closed()) {
      *line = "Koan.notConnected: the connection to Koan closed";
      return false;
    }
  }
  *line = "Koan.untilTimeout: " + UntilText(until) + " did not hold within " +
          base::NumberToString(wait.InSeconds()) + " s (" + shows +
          "); look at the page before going on";
  return false;
}

// --- Screenshots (koan_cli_screenshot.h) -----------------------------------

// A screenshot saved: where, and how big.
struct Shot {
  std::string path;
  int width = 0;
  int height = 0;
  double css_per_pixel = 1;
  // The card fields Koan filled that are painted over (design section 4.5).
  int masked = 0;
};

// How a screenshot prints: ShotLine() or ShotJson(), and whether the card
// fields Koan filled are hidden in it.
std::string ShotText(const Shot& shot) {
  std::string line =
      ShotLine(shot.path, shot.width, shot.height, shot.css_per_pixel);
  if (shot.masked > 0) {
    line += "; the card fields Koan filled are hidden in it";
  }
  return line;
}
base::DictValue ShotDict(const Shot& shot) {
  base::DictValue json =
      ShotJson(shot.path, shot.width, shot.height, shot.css_per_pixel);
  if (shot.masked > 0) {
    json.Set("masked", shot.masked);
  }
  return json;
}

// The element --clip names, as koan act finds it (FindNode()), scrolled into
// view: its box in viewport CSS pixels, of no size if it has none. nullopt
// (printed) if it is not on the page.
std::optional<CssRect> ClipBox(Link& link,
                               const Command& command,
                               const std::string& target,
                               const std::string& session,
                               int* exit_code) {
  std::optional<int> node =
      FindNode(link, command, command.clip, target, session, exit_code);
  if (!node) {
    return std::nullopt;
  }
  const base::DictValue params = base::DictValue().Set("backendNodeId", *node);
  std::optional<Message> scrolled =
      link.Call("DOM.scrollIntoViewIfNeeded", params.Clone(), session);
  if (!scrolled) {
    *exit_code = Fail(Describe(scrolled));
    return std::nullopt;
  }
  // An element with no layout box (display: none) has no quads: no size.
  std::optional<Message> quads =
      link.Call("DOM.getContentQuads", params.Clone(), session);
  if (!quads) {
    *exit_code = Fail(Describe(quads));
    return std::nullopt;
  }
  std::optional<CssRect> box = quads->is_error() || !quads->result
                                   ? std::nullopt
                                   : QuadsBounds(*quads->result);
  return box.value_or(CssRect());
}

// Takes the screenshot `command` asks for (koan screenshot's --size and
// --clip, or act's --screenshot) and saves it: Koan.captureScaled, the same
// way on the agent's own tabs and the user's. nullopt (printed) if not.
std::optional<Shot> TakeScreenshot(Link& link,
                                   const Command& command,
                                   const std::string& target,
                                   const std::string& session,
                                   int* exit_code) {
  std::optional<ClipSpec> clip;
  if (!command.clip.empty()) {
    std::string error;
    clip = ParseClip(command.clip, &error);
    if (!clip) {
      *exit_code = Fail(error, kUsage);
      return std::nullopt;
    }
  }
  std::optional<CssRect> region;
  std::string what = "the rectangle";
  double margin = 0;
  if (clip && !clip->target.empty()) {
    region = ClipBox(link, command, target, session, exit_code);
    if (!region) {
      return std::nullopt;
    }
    what = clip->target;
    if (!(region->width > 0 && region->height > 0)) {
      *exit_code = Fail(ClipRefusal("Koan.emptyElement", what));
      return std::nullopt;
    }
    margin = kClipMargin;
  } else if (clip) {
    region = clip->rect;
  }
  std::optional<Message> answer =
      link.Call("Koan.captureScaled",
                CaptureParams(target, command.shot_size, region, margin));
  if (answer && answer->is_error()) {
    const std::string refusal = ClipRefusal(answer->error_code, what);
    if (!refusal.empty()) {
      *exit_code = Fail(refusal);
      return std::nullopt;
    }
  }
  std::optional<base::DictValue> taken = Expect(answer, exit_code);
  const std::string* data = taken ? taken->FindString("data") : nullptr;
  std::optional<std::vector<uint8_t>> png =
      data ? base::Base64Decode(*data) : std::nullopt;
  if (!png) {
    if (taken) {
      *exit_code = Fail("the screenshot was empty");
    }
    return std::nullopt;
  }
  Shot shot;
  shot.path = command.output.empty() ? "koan-" + target.substr(0, 8) + ".png"
                                     : command.output;
  if (!base::WriteFile(base::FilePath(shot.path), *png)) {
    *exit_code = Fail("cannot write " + shot.path);
    return std::nullopt;
  }
  shot.width = taken->FindInt("width").value_or(0);
  shot.height = taken->FindInt("height").value_or(0);
  shot.css_per_pixel = taken->FindDouble("cssPerPixel").value_or(1);
  if (std::optional<std::pair<int, int>> size = PngSize(*png)) {
    // As encoded (a CDP fallback rounds its sides).
    if (shot.width > 0 && size->first != shot.width) {
      shot.css_per_pixel = shot.css_per_pixel * shot.width / size->first;
    }
    shot.width = size->first;
    shot.height = size->second;
  }
  shot.masked = taken->FindInt("masked").value_or(0);
  return shot;
}

// `koan act`: its steps in order over this connection (koan_cli_batch.h),
// each after the page settled from the one before, stopping at the first
// failure and before any step once the user has the tab or the page waits
// for them; then --until, then --snapshot and --screenshot. One step
// prints as it always did ("ok", or the error); several print one receipt
// each.
int Act(Link& link,
        const Command& command,
        const std::string& target,
        const std::string& session) {
  std::vector<Step> steps = command.steps;
  if (!command.steps_file.empty()) {
    std::string text;
    const bool read =
        command.steps_file == "-"
            ? base::ReadStreamToString(stdin, &text)
            : base::ReadFileToString(base::FilePath(command.steps_file), &text);
    if (!read) {
      return Fail("cannot read " + command.steps_file, kUsage);
    }
    std::string error;
    if (!ParseStepLines(text, &steps, &error)) {
      return Fail(error, kUsage);
    }
  }
  const bool numbered = steps.size() > 1;
  int exit_code = kOk;
  BatchGate gate(target);
  // What already waits for the user, from before this command.
  if (std::optional<Message> held =
          link.Call("Hold.status", base::DictValue())) {
    if (const base::ListValue* events =
            held->result ? held->result->FindList("events") : nullptr) {
      gate.Replay(*events);
    }
  }
  // --until on a ref: one of the page as it is now.
  if (command.until && (command.until->kind == Until::kRefText ||
                        command.until->kind == Until::kRefGone)) {
    Command probe = command;
    probe.ref = command.until->ref;
    if (!RefIsCurrent(link, probe, target, session, &exit_code)) {
      return exit_code;
    }
  }
  EventTap tap;
  tap.gate = &gate;
  link.SetEventSink(base::BindRepeating(
      [](EventTap* tap, const Message& event) {
        tap->gate->OnEvent(event);
        if (tap->settle) {
          tap->settle->OnEvent(event, base::TimeTicks::Now());
        }
      },
      base::Unretained(&tap)));
  std::vector<StepDone> done;
  // The step the batch stopped at (not done, or failed), and why.
  std::optional<size_t> stopped;
  std::string why;
  // The last step was done, and then the page waited for the user.
  std::string blocked_after;
  bool tab_gone = false;
  {
    SettleWatch watch(link, session);
    for (size_t i = 0; i < steps.size(); ++i) {
      // The user may have taken the tab since the last step.
      std::optional<base::DictValue> tabs =
          Expect(link.Call("Koan.listTabs", base::DictValue()), &exit_code);
      if (!tabs) {
        link.SetEventSink({});
        return exit_code;
      }
      const base::DictValue* entry = nullptr;
      if (const base::ListValue* list = tabs->FindList("tabs")) {
        for (const base::Value& tab : *list) {
          const base::DictValue* dict = tab.GetIfDict();
          const std::string* id = dict ? dict->FindString("targetId") : nullptr;
          if (id && *id == target) {
            entry = dict;
          }
        }
      }
      gate.OnTab(entry);
      tab_gone = !entry;
      // An upload step answers the file chooser the page may hold already.
      if (std::optional<std::string> stop =
              gate.StopReason(steps[i].act == ActKind::kUpload)) {
        stopped = i;
        why = *stop;
        break;
      }
      StepDone result =
          RunStep(link, StepCommand(command, steps[i]), target, session, tap);
      const bool ok = result.exit_code == kOk;
      std::string blocked = result.blocked;
      done.push_back(std::move(result));
      if (!ok) {
        stopped = i;
        why = done.back().line;
        break;
      }
      if (!blocked.empty()) {
        if (i + 1 < steps.size()) {
          stopped = i + 1;
          why = blocked;
        } else {
          blocked_after = blocked;
        }
        break;
      }
    }
  }
  std::string until_line;
  bool until_met = true;
  if (command.until && !stopped && blocked_after.empty()) {
    until_met = WaitUntil(link, *command.until, command, target, session, gate,
                          &until_line);
  }
  tab_gone =
      tab_gone || gate.StopReason().value_or("").starts_with("Koan.tabGone");
  link.SetEventSink({});

  const bool all_done = !stopped && blocked_after.empty() && until_met;
  int code = kOk;
  if (stopped && *stopped < done.size()) {
    code = done[*stopped].exit_code;
  } else if (!all_done) {
    code = kFailed;
  }
  std::optional<std::string> snapshot;
  int snapshot_code = kOk;
  std::optional<Shot> shot;
  int shot_code = kOk;
  // What to show of the page after the steps: --snapshot, --screenshot.
  const bool after = (command.snapshot_after || command.screenshot_after) &&
                     !tab_gone && code != kUsage;
  if (after) {
    // "ok" first: it was done, even if the snapshot then fails.
    if (!command.json && !numbered && !stopped) {
      printf("%s\n", done[0].line.c_str());
      fflush(stdout);
    }
    if (command.snapshot_after) {
      snapshot = TakeSnapshot(link, SnapshotRequestOf(command), target, session,
                              &snapshot_code);
    }
    if (command.screenshot_after) {
      shot = TakeScreenshot(link, command, target, session, &shot_code);
    }
  }

  if (command.json) {
    base::DictValue json;
    if (!numbered && !done.empty()) {
      json = done[0].json.Clone();
    }
    base::ListValue receipts;
    for (size_t i = 0; i < done.size(); ++i) {
      base::DictValue receipt = done[i].json.Clone();
      receipt.Set("step", StepLabel(steps[i]));
      receipt.Set("ok", done[i].exit_code == kOk);
      receipt.Set("line", done[i].line);
      receipts.Append(std::move(receipt));
    }
    if (numbered) {
      json.Set("steps", std::move(receipts));
    }
    if (stopped) {
      json.Set("stopped", base::DictValue()
                              .Set("at", static_cast<int>(*stopped) + 1)
                              .Set("step", StepLabel(steps[*stopped]))
                              .Set("reason", why));
    }
    if (!blocked_after.empty()) {
      json.Set("blocked", blocked_after);
    }
    if (command.until && !stopped && blocked_after.empty()) {
      json.Set("until", base::DictValue()
                            .Set("condition", UntilText(*command.until))
                            .Set("met", until_met)
                            .Set("line", until_line));
    }
    if (snapshot) {
      json.Set("snapshot", *snapshot);
    }
    if (shot) {
      json.Set("screenshot", ShotDict(*shot));
    }
    json.Set("ok", all_done);
    PrintJson(json);
    return code != kOk            ? code
           : snapshot_code != kOk ? snapshot_code
                                  : shot_code;
  }

  if (numbered) {
    for (size_t i = 0; i < done.size(); ++i) {
      if (stopped && *stopped == i) {
        break;
      }
      printf("%s\n",
             ReceiptLine(i, steps.size(), steps[i], done[i].line).c_str());
    }
    if (stopped) {
      printf("%s\n",
             StoppedLine(*stopped, steps.size(), steps[*stopped], why).c_str());
      if (const std::string left = NotRunLine(steps, *stopped); !left.empty()) {
        printf("%s\n", left.c_str());
      }
    }
  } else if (!done.empty() && !stopped && !after) {
    printf("%s\n", done[0].line.c_str());
  }
  if (!blocked_after.empty()) {
    Fail(blocked_after);
  }
  if (!numbered && stopped) {
    Fail(why);
  }
  if (command.until && !stopped && blocked_after.empty()) {
    printf("until %s: %s\n", UntilText(*command.until).c_str(),
           until_line.c_str());
  }
  if (snapshot) {
    printf("%s", snapshot->c_str());
  }
  if (shot) {
    printf("%s\n", ShotText(*shot).c_str());
  }
  fflush(stdout);
  return code != kOk ? code : snapshot_code != kOk ? snapshot_code : shot_code;
}

// `koan wait <tab> --resumed` (design section 9.2, auto-resume): blocks until
// the paused tab comes back to the agent, then prints a fresh interactive
// snapshot, so that the agent goes on from the page as the user left it in
// the same turn. Sends nothing to the tab while it waits (no keep-alive, no
// lease): it only listens to the client's events.
int WaitResumed(Link& link,
                const Command& command,
                const std::string& target,
                const std::string& session) {
  int exit_code = kOk;
  ResumeWaiter waiter(target);
  // Events first, then the tab's state: a resume between the two is not
  // missed.
  std::optional<Message> followed =
      link.Call("Hold.follow", base::DictValue().Set("sessionId", session));
  const bool following = followed && !followed->is_error();
  std::optional<base::DictValue> tabs =
      Expect(link.Call("Koan.listTabs", base::DictValue(), {}, kAnswerWait,
                       base::BindRepeating(&ResumeWaiter::OnEvent,
                                           base::Unretained(&waiter))),
             &exit_code);
  if (!tabs) {
    if (following) {
      link.Call("Hold.follow", base::DictValue());
    }
    return exit_code;
  }
  const base::DictValue* entry = nullptr;
  if (const base::ListValue* list = tabs->FindList("tabs")) {
    for (const base::Value& tab : *list) {
      const base::DictValue* dict = tab.GetIfDict();
      const std::string* id = dict ? dict->FindString("targetId") : nullptr;
      if (id && *id == target) {
        entry = dict;
      }
    }
  }
  waiter.OnTab(entry);
  const base::TimeTicks deadline =
      base::TimeTicks::Now() + base::Seconds(command.wait_seconds);
  while (!waiter.done()) {
    std::optional<Message> message = link.Next(deadline);
    if (!message) {
      if (link.closed()) {
        waiter.OnClosed();
      } else if (base::TimeTicks::Now() >= deadline) {
        waiter.OnTimeout();
      }
      continue;
    }
    if (!message->id) {
      waiter.OnEvent(*message);
    }
  }
  if (following && !link.closed()) {
    link.Call("Hold.follow", base::DictValue());
  }
  const std::string line = waiter.Line(command.tab, command.wait_seconds);
  if (!waiter.ok()) {
    if (command.json) {
      PrintJson(base::DictValue().Set("ok", false).Set("error", line));
    }
    return command.json ? kFailed : Fail(line);
  }
  SnapshotRequest request;
  request.interactive = true;
  int snapshot_code = kOk;
  std::optional<std::string> snapshot =
      TakeSnapshot(link, request, target, session, &snapshot_code);
  if (command.json) {
    base::DictValue json;
    json.Set("ok", snapshot.has_value());
    json.Set("line", line);
    if (waiter.outcome() == ResumeWaiter::Outcome::kResumed) {
      json.Set("resumed", waiter.resumed().Clone());
    }
    if (snapshot) {
      json.Set("snapshot", *snapshot);
    }
    PrintJson(json);
    return snapshot_code;
  }
  printf("%s\n", line.c_str());
  if (snapshot) {
    printf("%s", snapshot->c_str());
  }
  fflush(stdout);
  return snapshot_code;
}

// --- agent-browser (design section 11) --------------------------------------

// Runs `koan-relay <args>` (next to this executable) with this terminal, and
// waits for it. Its exit code, or -1.
int RunRelay(std::vector<std::string> args, const Command& command) {
  base::FilePath self;
  if (!base::PathService::Get(base::FILE_EXE, &self)) {
    return -1;
  }
  args.insert(args.begin(), self.DirName().Append("koan-relay").value());
  if (!command.user_data_dir.empty()) {
    args.insert(args.end(), {"--user-data-dir", command.user_data_dir});
  }
  const pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    std::vector<char*> argv;
    for (std::string& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    execv(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  if (HANDLE_EINTR(waitpid(pid, &status, 0)) != pid || !WIFEXITED(status)) {
    return -1;
  }
  return WEXITSTATUS(status);
}

bool RelayRuns(const std::string& session) {
  std::string ignored;
  return ConnectUnix(RelayControlSocket(RelayHome(KoanDir()), session).value(),
                     &ignored)
      .is_valid();
}

// `koan agent-browser <args>`: the relay of the args' agent-browser session,
// paired once (its own approval, naming koan-relay and the agent) and started
// if it does not run, then agent-browser with AGENT_BROWSER_CDP in its
// environment: never on its command line and never printed.
int AgentBrowser(const Command& command) {
  std::string error;
  std::optional<std::string> session = SessionFromArgs(
      command.passthrough, getenv("AGENT_BROWSER_SESSION"), &error);
  if (!session) {
    return Fail(error, kUsage);
  }
  const base::FilePath relay_home = RelayHome(KoanDir());
  if (PairedClients(relay_home).empty()) {
    fprintf(stderr,
            "koan: agent-browser needs koan-relay; approve it in Koan...\n");
    if (RunRelay({"pair"}, command) != 0) {
      return Fail("koan-relay was not paired", kNotConnected);
    }
  }
  if (!RelayRuns(*session) &&
      RunRelay({"serve", "--session", *session}, command) != 0) {
    return Fail("the relay did not start (see above)", kNotConnected);
  }
  std::string env;
  std::optional<std::string> url;
  if (base::ReadFileToString(RelayEnvFile(relay_home, *session), &env)) {
    url = ParseRelayEnv(env);
  }
  if (!url) {
    return Fail(
        "the relay's env file is missing; try koan relay stop --session " +
        *session);
  }
  setenv("AGENT_BROWSER_CDP", url->c_str(), /*overwrite=*/1);
  std::vector<std::string> args = command.passthrough;
  args.insert(args.begin(), "agent-browser");
  std::vector<char*> argv;
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  execvp("agent-browser", argv.data());
  return Fail("agent-browser is not installed (or not on PATH)");
}

int Relay(const Command& command) {
  if (!IsValidSessionName(command.session)) {
    return Fail("--session must be 1 to 64 letters, digits, . _ -", kUsage);
  }
  const base::FilePath relay_home = RelayHome(KoanDir());
  if (command.kind == CommandKind::kRelayEnv) {
    const base::FilePath env = RelayEnvFile(relay_home, command.session);
    if (!RelayRuns(command.session) || !base::PathExists(env)) {
      return Fail("no relay runs for session " + command.session);
    }
    // The file's path, never what it holds.
    printf("%s\n", env.value().c_str());
    return kOk;
  }
  std::string error;
  base::ScopedFD fd = ConnectUnix(
      RelayControlSocket(relay_home, command.session).value(), &error);
  if (!fd.is_valid()) {
    if (!command.json) {
      printf("no relay runs for session %s\n", command.session.c_str());
    }
    return kOk;
  }
  Link link(
      std::make_unique<FrameChannel>(std::move(fd), agent::kMaxPreAuthPayload));
  std::optional<Message> answer =
      link.Call("Relay.stop", base::DictValue(), {}, base::Seconds(5));
  if (!answer || answer->is_error()) {
    return Fail("the relay did not stop: " + Describe(answer));
  }
  return kOk;
}

std::unique_ptr<Link> ConnectionFor(const Command& command, int* exit_code);
// `koan autofill <tab> @field`: --list prints Chrome's suggestions for the
// field (labels only); --pick runs as act's one `autofill` step, with its
// checks (the tab paused, the page waiting for the user) and its settle.
int Autofill(Link& link,
             const Command& command,
             const std::string& target,
             const std::string& session) {
  if (!command.autofill_list) {
    Command act = command;
    act.kind = CommandKind::kAct;
    Step step;
    step.act = ActKind::kAutofill;
    step.target = command.ref;
    step.text = command.pick;
    act.steps = {step};
    act.act = step.act;
    act.text = step.text;
    return Act(link, act, target, session);
  }
  int exit_code = kOk;
  std::optional<int> node =
      FindNode(link, command, command.ref, target, session, &exit_code);
  if (!node) {
    return exit_code;
  }
  std::optional<base::DictValue> result = Expect(
      link.Call("Koan.autofill", AutofillParams(*node, /*list=*/true, ""),
                session, kAutofillWait),
      &exit_code);
  if (!result) {
    return exit_code;
  }
  if (command.json) {
    PrintJson(*result);
    return kOk;
  }
  printf("%s", SuggestionLines(*result).c_str());
  fflush(stdout);
  return kOk;
}

// `koan upload <tab> <path...>`: answers the file chooser the tab's page
// opened (waiting a little for one), as the user's pick would.
int Upload(Link& link,
           const Command& command,
           const std::string& target,
           const std::string& session) {
  base::FilePath cwd;
  base::GetCurrentDirectory(&cwd);
  std::vector<UploadPath> paths;
  std::string error;
  if (!ResolveUploadPaths(command.files, cwd, &paths, &error)) {
    return Fail(error);
  }
  // The page's change handler runs at full speed meanwhile.
  TabKeepAlive keep_alive(link, target);
  int exit_code = kOk;
  std::optional<base::DictValue> result =
      Expect(link.Call("Koan.upload", UploadParams(paths, kUploadWait), session,
                       kUploadAnswerWait),
             &exit_code);
  if (!result) {
    return exit_code;
  }
  if (command.json) {
    PrintJson(*result);
    return kOk;
  }
  printf("%s\n", UploadedLine(*result).c_str());
  fflush(stdout);
  return kOk;
}

int RunOnHolder(Link& link, const Command& command);
int Shell(const Command& command);

int Run(const Command& command) {
  switch (command.kind) {
    case CommandKind::kHelp: {
      if (command.help_topic.empty()) {
        printf("%s", UsageText().c_str());
        return kOk;
      }
      std::optional<std::string> help = HelpText(command.help_topic);
      if (!help) {
        return Fail("no help for \"" + command.help_topic + "\"; try koan help",
                    kUsage);
      }
      printf("%s", help->c_str());
      return kOk;
    }
    case CommandKind::kVersion:
      printf("koan %d\n", agent::kAgentInterfaceVersion);
      return kOk;
    case CommandKind::kSkill:
      printf("%s",
             SkillGuide(VersionNotice(
                            ReadDiscovery(UserDataDir(command.user_data_dir)),
                            agent::kAgentInterfaceVersion))
                 .c_str());
      return kOk;
    case CommandKind::kPair:
      return Pair(command);
    case CommandKind::kStatus:
      return Status(command);
    case CommandKind::kHold:
      return RunHolder(command);
    case CommandKind::kAgentBrowser:
      return AgentBrowser(command);
    case CommandKind::kRelayEnv:
    case CommandKind::kRelayStop:
      return Relay(command);
    case CommandKind::kShell:
      return Shell(command);
#if BUILDFLAG(IS_LINUX)
    case CommandKind::kSetup:
      return Setup(command);
#endif
    default:
      break;
  }

  int exit_code = kOk;
  std::unique_ptr<Link> link = ConnectionFor(command, &exit_code);
  return link ? RunOnHolder(*link, command) : exit_code;
}

// Everything but the commands Run() answers itself runs over the holder's
// connection. `end` and `release` need no new one: with no holder there is
// nothing to end or give back. nullptr, and the exit code (printed), if
// there is none.
std::unique_ptr<Link> ConnectionFor(const Command& command, int* exit_code) {
  const bool start = command.kind != CommandKind::kEnd &&
                     command.kind != CommandKind::kRelease;
  std::string error;
  std::unique_ptr<Link> link = Holder(command, start, &error);
  if (!link) {
    if (!start && error.empty()) {
      if (!command.json) {
        printf("no connection: nothing to %s\n",
               command.kind == CommandKind::kEnd ? "end" : "release");
      }
      *exit_code = kOk;
    } else {
      *exit_code = Fail(error, kNotConnected);
    }
  }
  return link;
}

int RunOnHolder(Link& link, const Command& command) {
  int exit_code = kOk;
  switch (command.kind) {
    case CommandKind::kTabs: {
      std::optional<base::DictValue> result =
          Expect(link.Call("Koan.listTabs", base::DictValue()), &exit_code);
      if (!result) {
        return exit_code;
      }
      if (command.json) {
        PrintJson(*result);
        return kOk;
      }
      if (const base::ListValue* tabs = result->FindList("tabs")) {
        for (const base::Value& tab : *tabs) {
          const base::DictValue& t = tab.GetDict();
          const std::string* id = t.FindString("targetId");
          const std::string* state = t.FindString("state");
          const std::string* url = t.FindString("url");
          const std::string* title = t.FindString("title");
          printf("%s  %s  %s  %s\n", id ? id->c_str() : "",
                 state ? state->c_str() : "", url ? url->c_str() : "",
                 title ? title->c_str() : "");
        }
      }
      return kOk;
    }
    case CommandKind::kOpen: {
      base::DictValue params;
      params.Set("url", command.url);
      if (!command.task.empty()) {
        params.Set("task", command.task);
      }
      if (!command.saved_container.empty()) {
        params.Set("savedContainer", command.saved_container);
      }
      std::optional<base::DictValue> result =
          Expect(link.Call("Koan.openTab", std::move(params)), &exit_code);
      if (!result) {
        return exit_code;
      }
      if (command.json) {
        PrintJson(*result);
      } else if (const std::string* id = result->FindString("targetId")) {
        printf("%s\n", id->c_str());
      }
      return kOk;
    }
    case CommandKind::kSnapshot:
    case CommandKind::kScreenshot:
    case CommandKind::kWait:
    case CommandKind::kAutofill:
    case CommandKind::kUpload:
    case CommandKind::kAct: {
      std::optional<std::string> target =
          ResolveTabId(link, command.tab, &exit_code);
      if (!target) {
        return exit_code;
      }
      std::optional<std::string> session =
          SessionFor(link, *target, &exit_code);
      if (!session) {
        return exit_code;
      }
      if (command.kind == CommandKind::kSnapshot) {
        return PrintSnapshot(link, command, *target, *session);
      }
      if (command.kind == CommandKind::kWait) {
        return WaitResumed(link, command, *target, *session);
      }
      if (command.kind == CommandKind::kAutofill) {
        return Autofill(link, command, *target, *session);
      }
      if (command.kind == CommandKind::kUpload) {
        return Upload(link, command, *target, *session);
      }
      if (command.kind == CommandKind::kScreenshot) {
        std::optional<Shot> shot =
            TakeScreenshot(link, command, *target, *session, &exit_code);
        if (!shot) {
          return exit_code;
        }
        if (command.json) {
          PrintJson(ShotDict(*shot));
          return kOk;
        }
        printf("%s\n", ShotText(*shot).c_str());
        return kOk;
      }
      return Act(link, command, *target, *session);
    }
    case CommandKind::kTakeOver: {
      base::DictValue params;
      if (!command.tab.empty()) {
        params.Set("targetId", command.tab);
      }
      fprintf(stderr, "koan: the user is asked in Koan...\n");
      std::optional<base::DictValue> result = Expect(
          link.Call("Koan.takeOver", std::move(params), {}, kApprovalWait),
          &exit_code);
      if (!result) {
        return exit_code;
      }
      if (command.json) {
        PrintJson(*result);
      } else if (const std::string* id = result->FindString("targetId")) {
        printf("%s\n", id->c_str());
      }
      return kOk;
    }
    case CommandKind::kRelease: {
      std::optional<std::string> target =
          ResolveTabId(link, command.tab, &exit_code);
      if (!target) {
        return exit_code;
      }
      base::DictValue params;
      params.Set("targetId", *target);
      return Expect(link.Call("Koan.release", std::move(params)), &exit_code)
                 ? kOk
                 : exit_code;
    }
    case CommandKind::kEnd: {
      if (!command.task.empty()) {
        base::DictValue params;
        params.Set("task", command.task);
        return Expect(link.Call("Koan.endTask", std::move(params)), &exit_code)
                   ? kOk
                   : exit_code;
      }
      return Expect(link.Call("Hold.exit", base::DictValue()), &exit_code)
                 ? kOk
                 : exit_code;
    }
    case CommandKind::kWatch: {
      std::optional<std::string> target;
      if (!command.tab.empty()) {
        target = ResolveTabId(link, command.tab, &exit_code);
        if (!target) {
          return exit_code;
        }
      }
      if (!Expect(link.Call("Hold.watch", base::DictValue()), &exit_code)) {
        return exit_code;
      }
      auto print = [](std::string_view json) {
        printf("%.*s\n", static_cast<int>(json.size()), json.data());
        fflush(stdout);
      };
      // `koan watch <tab>`: the tab keeps rendering while it is watched, and
      // the events that come while a renewal waits are printed too.
      std::optional<TabKeepAlive> keep_alive;
      if (target) {
        keep_alive.emplace(link, *target,
                           base::BindRepeating(
                               [](decltype(print) print, const Message& event) {
                                 base::DictValue json;
                                 json.Set("method", event.method);
                                 json.Set("params", event.params.Clone());
                                 if (!event.session_id.empty()) {
                                   json.Set("sessionId", event.session_id);
                                 }
                                 print(base::WriteJson(json).value_or("{}"));
                               },
                               print));
      }
      while (true) {
        const base::TimeDelta timeout =
            keep_alive
                ? std::max(keep_alive->next_renewal() - base::TimeTicks::Now(),
                           base::TimeDelta())
                : base::TimeDelta::Max();
        std::optional<std::string> frame = link.channel().Read(timeout);
        if (frame) {
          print(*frame);
        } else if (link.closed()) {
          break;
        }
        if (keep_alive) {
          keep_alive->RenewIfDue();
        }
      }
      return kOk;
    }
    default:
      return Fail("not a command", kUsage);
  }
}

// --- koan shell -------------------------------------------------------------

// What one command of `koan shell` prints, gathered for its answer. A plain
// line's stderr goes into its stdout, so that errors come in order and before
// the end line; a JSON line's two go into files of their own (unnamed,
// this user's), read back into one JSON answer.
class ShellAnswer {
 public:
  explicit ShellAnswer(bool json) : json_(json) {
    fflush(stdout);
    fflush(stderr);
    // Not inherited by a holder this command starts: the ssh session that
    // runs `koan shell` must end when it does.
    saved_out_ = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    saved_err_ = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    if (json_) {
      out_.reset(tmpfile());
      err_.reset(tmpfile());
      if (out_ && err_) {
        fcntl(fileno(out_.get()), F_SETFD, FD_CLOEXEC);
        fcntl(fileno(err_.get()), F_SETFD, FD_CLOEXEC);
        dup2(fileno(out_.get()), STDOUT_FILENO);
        dup2(fileno(err_.get()), STDERR_FILENO);
      }
    } else {
      dup2(STDOUT_FILENO, STDERR_FILENO);
    }
  }
  ShellAnswer(const ShellAnswer&) = delete;
  ShellAnswer& operator=(const ShellAnswer&) = delete;
  ~ShellAnswer() = default;

  // Puts stdout and stderr back and writes the answer: the output, then
  // "--- exit N"; or {"id", "exit", "out", "err"}.
  void Finish(int code, const std::string& id) {
    fflush(stdout);
    fflush(stderr);
    if (saved_out_ >= 0) {
      dup2(saved_out_, STDOUT_FILENO);
      close(saved_out_);
    }
    if (saved_err_ >= 0) {
      dup2(saved_err_, STDERR_FILENO);
      close(saved_err_);
    }
    if (json_) {
      const std::string answer =
          base::WriteJson(base::DictValue()
                              .Set("exit", code)
                              .Set("out", ReadBack(out_.get()))
                              .Set("err", ReadBack(err_.get())))
              .value_or("{}");
      // The id as the line gave it (any JSON value), first.
      // Splice it in for the answer's opening brace.
      const std::string line =
          "{\"id\":" + id + "," + answer.substr(answer.empty() ? 0 : 1);
      printf("%s\n", line.c_str());
    } else {
      printf("--- exit %d\n", code);
    }
    fflush(stdout);
  }

 private:
  static std::string ReadBack(FILE* file) {
    std::string text;
    if (!file) {
      return text;
    }
    rewind(file);
    base::ReadStreamToString(file, &text);
    // One line of JSON: the text is what the command printed, without its
    // last newline.
    if (!text.empty() && text.back() == '\n') {
      text.pop_back();
    }
    return text;
  }

  const bool json_;
  int saved_out_ = -1;
  int saved_err_ = -1;
  base::ScopedFILE out_;
  base::ScopedFILE err_;
};

// One command of the shell, over its connection (made, or made again after
// `koan end`, when a command needs it).
int RunInShell(const Command& command, std::unique_ptr<Link>& link) {
  switch (command.kind) {
    case CommandKind::kShell:
    case CommandKind::kHold:
    case CommandKind::kAgentBrowser:
      return Fail("not in koan shell: run it on its own", kUsage);
    case CommandKind::kWatch:
      return Fail(
          "not in koan shell: koan watch streams events until it is stopped; "
          "run it on its own",
          kUsage);
    case CommandKind::kHelp:
    case CommandKind::kVersion:
    case CommandKind::kSkill:
    case CommandKind::kPair:
    case CommandKind::kStatus:
    case CommandKind::kRelayEnv:
    case CommandKind::kRelayStop:
    case CommandKind::kSetup:
      return Run(command);
    default:
      break;
  }
  if (command.steps_file == "-") {
    return Fail(
        "--steps - reads stdin, which is koan shell's: put the steps on the "
        "act line, or in a file",
        kUsage);
  }
  if (link && link->closed()) {
    link.reset();
  }
  int exit_code = kOk;
  if (!link) {
    link = ConnectionFor(command, &exit_code);
    if (!link) {
      return exit_code;
    }
  }
  exit_code = RunOnHolder(*link, command);
  // `koan end` (of every task) ends the holder, and with it the connection.
  if (command.kind == CommandKind::kEnd && command.task.empty()) {
    link.reset();
  }
  return exit_code;
}

// `koan shell`: commands from stdin, one per line (ParseShellLine()), over
// one connection to the holder, which is admitted once (as every koan is:
// koan_cli_admission.h) and checked again before each request. An agent
// pays for starting koan and connecting once, not per command; over ssh,
// for one ssh session.
int Shell(const Command& shell) {
  std::unique_ptr<Link> link;
  int last = kOk;
  char* buffer = nullptr;
  size_t size = 0;
  ssize_t got;
  // A command's output and its errors in the order they were written.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  while ((got = getline(&buffer, &size, stdin)) >= 0) {
    // SAFETY: getline() wrote `got` bytes (and a NUL) at `buffer`.
    const ShellLine line = ParseShellLine(
        UNSAFE_BUFFERS(std::string_view(buffer, static_cast<size_t>(got))));
    if (line.empty) {
      continue;
    }
    ShellAnswer answer(line.json);
    int code = kOk;
    if (!line.error.empty()) {
      code = Fail(line.error, kUsage);
    } else if (ParseResult parsed = ParseArgs(line.args); !parsed.command) {
      code = Fail(parsed.error, kUsage);
    } else if (!parsed.command->client.empty() ||
               !parsed.command->user_data_dir.empty()) {
      code = Fail(
          "--client and --user-data-dir are koan shell's own: give them to "
          "koan shell",
          kUsage);
    } else {
      Command command = std::move(*parsed.command);
      command.client = shell.client;
      command.user_data_dir = shell.user_data_dir;
      code = RunInShell(command, link);
    }
    answer.Finish(code, line.id);
    last = code;
    if (code != kOk && shell.stop_on_error) {
      break;
    }
  }
  free(buffer);
  return last;
}

}  // namespace
}  // namespace koan::cli

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  // A write to a closed socket is an error to handle, not a signal.
  signal(SIGPIPE, SIG_IGN);
  std::vector<std::string> args(argv + 1, argv + argc);
  koan::cli::ParseResult parsed = koan::cli::ParseArgs(args);
  if (!parsed.command) {
    return koan::cli::Fail(parsed.error, koan::cli::kUsage);
  }
  return koan::cli::Run(*parsed.command);
}
