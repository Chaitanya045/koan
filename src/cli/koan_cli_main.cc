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
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ref.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/cli/koan_cli_actions.h"
#include "koan/cli/koan_cli_admission.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_client.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
#include "koan/cli/koan_cli_settle.h"
#include "koan/cli/koan_cli_skill.h"
#include "koan/cli/koan_cli_snapshot.h"
#include "koan/cli/koan_relay_gate.h"

namespace koan::cli {
namespace {

constexpr char kVersion[] = "1";
// A holder with no command for this long ends its tasks and exits.
constexpr base::TimeDelta kHolderIdle = base::Minutes(30);
// How many lines a snapshot prints at most: compact, and --full.
constexpr size_t kSnapshotLines = 1500;
constexpr size_t kFullSnapshotLines = 5000;

enum Exit {
  kOk = 0,
  kFailed = 1,
  kUsage = 2,
  kNotConnected = 3,
};

int Fail(const std::string& text, int code = kFailed) {
  fprintf(stderr, "koan: %s\n", text.c_str());
  return code;
}

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
  // Which page each tab's refs belong to (koan_cli_snapshot.h).
  RefBook refs;
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
            reply.Set("result", base::DictValue());
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
    fprintf(stderr, "koan: %s\n", error->c_str());
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

// The compact (or `full`) outline of the tab, its refs remembered by the
// holder; `tree` gets the raw tree. nullopt (printed) if it could not be
// read.
std::optional<std::string> TakeSnapshot(Link& link,
                                        bool full,
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
  SnapshotOptions options;
  options.full = full;
  options.max_lines = full ? kFullSnapshotLines : kSnapshotLines;
  Snapshot snapshot = BuildSnapshot(*tree, options);
  base::ListValue ids;
  for (int id : snapshot.refs) {
    ids.Append(id);
  }
  std::optional<Message> kept =
      link.Call("Hold.rememberRefs", base::DictValue()
                                         .Set("targetId", target)
                                         .Set("document", *document)
                                         .Set("refs", std::move(ids)));
  if (!kept || !kept->result) {
    *exit_code = Fail(kOtherHolder);
    return std::nullopt;
  }
  if (tree_out) {
    *tree_out = std::move(*tree);
  }
  return std::move(snapshot.text);
}

int PrintSnapshot(Link& link,
                  const Command& command,
                  const std::string& target,
                  const std::string& session) {
  int exit_code = kOk;
  base::DictValue tree;
  std::optional<std::string> text =
      TakeSnapshot(link, command.full, target, session, &exit_code, &tree);
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

int Done(const Command& command,
         const std::string& line,
         base::DictValue json) {
  if (command.json) {
    json.Set("ok", true);
    PrintJson(json);
  } else {
    printf("%s\n", line.c_str());
  }
  return kOk;
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
    if (network_) {
      link_->Call("Network.disable", base::DictValue(), session_);
    }
    if (page_) {
      link_->Call("Page.disable", base::DictValue(), session_);
    }
    if (followed_) {
      link_->Call("Hold.follow", base::DictValue());
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
    settle.OnEvent(*message, base::TimeTicks::Now());
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
    case ActKind::kFill: {
      std::string error;
      std::optional<std::string> object_id =
          ResolveRef(link, session, node, &error);
      std::optional<base::DictValue> probe =
          object_id ? RunOn(link, session, *object_id, kFillTargetFunction,
                            base::ListValue(), &error)
                    : std::nullopt;
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

// One action on a tab, through the CDP the browser allows an agent (the
// in-browser Koan.act of phase 3 replaces this). A ref must be from a
// snapshot of the page the tab shows now. Once the action is done, waits
// for the page to settle (koan_cli_settle.h), then prints "ok" (or what the
// action says), and with --snapshot the new snapshot.
int Act(Link& link,
        const Command& command,
        const std::string& target,
        const std::string& session) {
  int exit_code = kOk;
  const bool on_ref =
      command.act == ActKind::kClick || command.act == ActKind::kFill ||
      command.act == ActKind::kSelect || command.act == ActKind::kChoose;
  if (on_ref && !RefIsCurrent(link, command, target, session, &exit_code)) {
    return exit_code;
  }
  std::optional<MainFrame> frame = MainFrameOf(link, session, &exit_code);
  if (!frame) {
    return exit_code;
  }
  SettleTimings timings;
  timings.navigation_timeout = base::Seconds(command.timeout_seconds);
  SettleState state = SettleState::kSettled;
  Acted acted;
  std::optional<Settle> settled;
  {
    SettleWatch watch(link, session);
    auto settle_from = [&](bool navigating) -> Settle& {
      settled.emplace(timings, base::TimeTicks::Now(), navigating, target,
                      frame->id);
      link.SetEventSink(base::BindRepeating(
          [](Settle* settle, const Message& event) {
            settle->OnEvent(event, base::TimeTicks::Now());
          },
          base::Unretained(&*settled)));
      return *settled;
    };
    Settle& settle = settle_from(command.act == ActKind::kNavigate);
    acted = DoAction(link, command, session, settle, timings);
    if (acted.exit_code == kOk) {
      state = Pump(link, session, settle, timings);
    }
    // A custom dropdown's option that did not take is clicked once more
    // (a list that was still moving, a page that wanted a second click).
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
    link.SetEventSink({});
  }
  if (acted.exit_code != kOk) {
    return acted.exit_code;
  }
  const Settle& settle = *settled;
  std::string line = SettledLine(settle, state, acted.line);
  if (acted.http_error && !settle.http_status()) {
    line += " (an HTTP error, and no page from the server)";
  }
  acted.json.Set("settled", SettleStateName(state));
  if (std::optional<int> status = settle.http_status()) {
    acted.json.Set("status", *status);
  }
  if (state == SettleState::kBlocked) {
    Done(command, line, std::move(acted.json));
    return Fail(settle.blocked_reason());
  }
  if (!command.snapshot_after) {
    return Done(command, line, std::move(acted.json));
  }
  // "ok" first: it was done, even if the snapshot then fails.
  if (!command.json) {
    printf("%s\n", line.c_str());
    fflush(stdout);
  }
  std::optional<std::string> snapshot =
      TakeSnapshot(link, /*full=*/false, target, session, &exit_code);
  if (command.json) {
    if (snapshot) {
      acted.json.Set("snapshot", *snapshot);
    }
    Done(command, line, std::move(acted.json));
  } else if (snapshot) {
    printf("%s", snapshot->c_str());
  }
  return snapshot ? kOk : exit_code;
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
int RunOnHolder(Link& link, const Command& command);
int Shell(const Command& command);

int Run(const Command& command) {
  switch (command.kind) {
    case CommandKind::kHelp:
      printf("%s", UsageText().c_str());
      return kOk;
    case CommandKind::kVersion:
      printf("%s\n", (std::string("koan ") + kVersion).c_str());
      return kOk;
    case CommandKind::kSkill:
      printf("%s", SkillGuide().c_str());
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
      if (command.kind == CommandKind::kScreenshot) {
        base::DictValue params;
        params.Set("format", "png");
        std::optional<base::DictValue> shot = Expect(
            link.Call("Page.captureScreenshot", std::move(params), *session),
            &exit_code);
        const std::string* data = shot ? shot->FindString("data") : nullptr;
        std::optional<std::vector<uint8_t>> png =
            data ? base::Base64Decode(*data) : std::nullopt;
        if (!png) {
          return shot ? Fail("the screenshot was empty") : exit_code;
        }
        const base::FilePath path(command.output.empty()
                                      ? "koan-" + target->substr(0, 8) + ".png"
                                      : command.output);
        if (!base::WriteFile(path, *png)) {
          return Fail("cannot write " + path.value());
        }
        printf("%s\n", path.value().c_str());
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
      if (!Expect(link.Call("Hold.watch", base::DictValue()), &exit_code)) {
        return exit_code;
      }
      while (std::optional<std::string> frame =
                 link.channel().Read(base::TimeDelta::Max())) {
        printf("%s\n", frame->c_str());
        fflush(stdout);
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
      printf("{\"id\":%s,%s\n", id.c_str(), answer.c_str() + 1);
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
    char buffer[16 * 1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      text.append(buffer, n);
    }
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
      return Run(command);
    default:
      break;
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
    const ShellLine line =
        ParseShellLine(std::string_view(buffer, static_cast<size_t>(got)));
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
