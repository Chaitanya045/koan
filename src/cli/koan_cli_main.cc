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
#include "base/functional/callback.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/cli/koan_cli_admission.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_client.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
#include "koan/cli/koan_cli_skill.h"
#include "koan/cli/koan_relay_gate.h"

namespace koan::cli {
namespace {

constexpr char kVersion[] = "1";
// A holder with no command for this long ends its tasks and exits.
constexpr base::TimeDelta kHolderIdle = base::Minutes(30);
constexpr size_t kSnapshotLines = 1500;

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

int RunHolder(const Command& command) {
  std::string error;
  const std::vector<std::string> paired = PairedClients(KoanDir());
  std::optional<std::string> client_id =
      ChooseClient(paired, command.client, &error);
  int ready_fd = command.ready_fd;
  if (!client_id) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  std::unique_ptr<FrameChannel> browser =
      DialBrowser(UserDataDir(command.user_data_dir), &error);
  if (!browser) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  Link link(std::move(browser));
  if (!Authenticate(link, KoanDir(), *client_id, &error)) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  if (!PrivateDir(KoanDir()) || !PrivateDir(KoanDir().Append("run"))) {
    SayReady(ready_fd, "error ~/.koan/run is not a private directory");
    return kFailed;
  }
  // Every command that connects is checked as the browser checks `koan`
  // (koan_cli_admission.h): no resolver, or no identity of its own, and the
  // holder serves nobody.
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
  // The agent the pairing recorded; a pairing by an older koan has none, and
  // the agent that started this holder (which the browser just accepted)
  // stands in.
  const std::optional<ClientInfo> client = LoadClient(KoanDir(), *client_id);
  const agent::ProcessInfo paired_agent = client && client->pinned
                                              ? client->pinned->agent
                                              : agent::AgentAncestor(*self);
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
            self->process, paired_agent, geteuid(),
            peer ? std::optional<agent::PeerIdentity>(peer->identity)
                 : std::nullopt);
        if (verdict == LocalClientVerdict::kAllowed &&
            commands.size() >= kMaxCommands) {
          log("refused busy");
          fd.reset();
        } else if (verdict != LocalClientVerdict::kAllowed) {
          // Not the paired agent's koan: it never reaches the connection.
          log("refused " + std::string(VerdictName(verdict)) + " " +
              (peer ? peer->identity.process.path : std::string("?")));
          fd.reset();
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
  std::string line;
  char c;
  const base::TimeTicks deadline =
      base::TimeTicks::Now() + kApprovalWait + base::Seconds(10);
  while (line.size() < 4096) {
    pollfd poll_fd = {read_end.get(), POLLIN, 0};
    const base::TimeDelta left = deadline - base::TimeTicks::Now();
    if (left <= base::TimeDelta() ||
        HANDLE_EINTR(
            poll(&poll_fd, 1, static_cast<int>(left.InMilliseconds()))) <= 0 ||
        HANDLE_EINTR(read(read_end.get(), &c, 1)) != 1 || c == '\n') {
      break;
    }
    line += c;
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
  std::optional<std::string> client_id =
      ChooseClient(PairedClients(KoanDir()), command.client, error);
  if (!client_id) {
    return nullptr;
  }
  if (std::unique_ptr<Link> link = ConnectHolder(*client_id)) {
    return link;
  }
  if (!start) {
    return nullptr;
  }
  if (!StartHolder(command, error)) {
    return nullptr;
  }
  std::unique_ptr<Link> link = ConnectHolder(*client_id);
  if (!link) {
    *error = "the connection to Koan started but cannot be reached";
  }
  return link;
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
      PairNewClient(link, KoanDir(), label, command.capabilities, &error);
  if (!result) {
    return Fail(error);
  }
  if (command.json) {
    PrintJson(*result);
  } else {
    printf("paired as %s (%s)\n", result->FindString("clientId")->c_str(),
           label.c_str());
  }
  return kOk;
}

int Status(const Command& command) {
  base::DictValue status;
  std::string error;
  std::unique_ptr<FrameChannel> probe =
      DialBrowser(UserDataDir(command.user_data_dir), &error);
  status.Set("koanRunning", static_cast<bool>(probe));
  probe.reset();
  const std::vector<std::string> paired = PairedClients(KoanDir());
  status.Set("paired", !paired.empty());
  std::unique_ptr<Link> holder = Holder(command, /*start=*/false, &error);
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
  printf("paired: %s\n", paired.empty() ? "no (koan pair)" : "yes");
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

// One action on a tab, through the CDP the browser allows an agent (the
// in-browser Koan.act of phase 3 replaces this).
int Act(Link& link, const Command& command, const std::string& session) {
  int exit_code = kOk;
  auto call = [&](std::string_view method,
                  base::DictValue params) -> std::optional<base::DictValue> {
    return Expect(link.Call(method, std::move(params), session), &exit_code);
  };
  auto mouse = [&](std::string_view type, double x, double y,
                   base::DictValue extra) {
    extra.Set("type", type);
    extra.Set("x", x);
    extra.Set("y", y);
    return call("Input.dispatchMouseEvent", std::move(extra)).has_value();
  };
  const int node = ParseRef(command.ref).value_or(0);
  switch (command.act) {
    case ActKind::kNavigate: {
      base::DictValue params;
      params.Set("url", command.text);
      std::optional<base::DictValue> result =
          call("Page.navigate", std::move(params));
      if (!result) {
        return exit_code;
      }
      if (const std::string* failed = result->FindString("errorText")) {
        return Fail("the page did not load: " + *failed);
      }
      break;
    }
    case ActKind::kPress: {
      std::optional<base::DictValue> down = KeyDownFor(command.text);
      if (!down) {
        return Fail("unknown key \"" + command.text +
                        "\" (Enter, Tab, Escape, Backspace, Arrow..., or "
                        "one character)",
                    kUsage);
      }
      base::DictValue up = down->Clone();
      up.Set("type", "keyUp");
      up.Remove("text");
      if (!call("Input.dispatchKeyEvent", std::move(*down)) ||
          !call("Input.dispatchKeyEvent", std::move(up))) {
        return exit_code;
      }
      break;
    }
    case ActKind::kScroll: {
      std::optional<base::DictValue> metrics =
          call("Page.getLayoutMetrics", base::DictValue());
      if (!metrics) {
        return exit_code;
      }
      const base::DictValue* viewport = metrics->FindDict("cssVisualViewport");
      const double x =
          viewport ? viewport->FindDouble("clientWidth").value_or(200) / 2
                   : 200;
      const double y =
          viewport ? viewport->FindDouble("clientHeight").value_or(200) / 2
                   : 200;
      base::DictValue wheel;
      wheel.Set("deltaX", 0);
      wheel.Set("deltaY", command.scroll_by);
      if (!mouse("mouseWheel", x, y, std::move(wheel))) {
        return exit_code;
      }
      break;
    }
    case ActKind::kClick: {
      base::DictValue target;
      target.Set("backendNodeId", node);
      if (!call("DOM.scrollIntoViewIfNeeded", target.Clone())) {
        return exit_code;
      }
      std::optional<base::DictValue> quads =
          call("DOM.getContentQuads", std::move(target));
      if (!quads) {
        return exit_code;
      }
      std::optional<std::pair<double, double>> center = QuadCenter(*quads);
      if (!center) {
        return Fail(command.ref + " is not on the screen");
      }
      base::DictValue press;
      press.Set("button", "left");
      press.Set("clickCount", 1);
      if (!mouse("mouseMoved", center->first, center->second,
                 base::DictValue()) ||
          !mouse("mousePressed", center->first, center->second,
                 press.Clone()) ||
          !mouse("mouseReleased", center->first, center->second,
                 std::move(press))) {
        return exit_code;
      }
      break;
    }
    case ActKind::kFill: {
      base::DictValue target;
      target.Set("backendNodeId", node);
      if (!call("DOM.focus", target.Clone())) {
        return exit_code;
      }
      std::optional<base::DictValue> resolved =
          call("DOM.resolveNode", std::move(target));
      const base::DictValue* object =
          resolved ? resolved->FindDict("object") : nullptr;
      const std::string* object_id =
          object ? object->FindString("objectId") : nullptr;
      if (!object_id) {
        return resolved ? Fail(command.ref + " is gone") : exit_code;
      }
      // What is typed replaces what the field holds.
      base::DictValue select;
      select.Set("objectId", *object_id);
      select.Set("functionDeclaration",
                 "function() { if (this.select) { this.select(); return; } "
                 "const range = document.createRange(); "
                 "range.selectNodeContents(this); "
                 "const selection = getSelection(); "
                 "selection.removeAllRanges(); selection.addRange(range); }");
      base::DictValue text;
      text.Set("text", command.text);
      if (!call("Runtime.callFunctionOn", std::move(select)) ||
          !call("Input.insertText", std::move(text))) {
        return exit_code;
      }
      break;
    }
  }
  if (!command.json) {
    printf("ok\n");
  } else {
    PrintJson(base::DictValue().Set("ok", true));
  }
  return kOk;
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

int Run(const Command& command) {
  switch (command.kind) {
    case CommandKind::kHelp:
      printf("%s", UsageText().c_str());
      return kOk;
    case CommandKind::kVersion:
      printf("koan %s\n", kVersion);
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
    default:
      break;
  }

  // Everything else runs over the holder's connection. `end` and `release`
  // need no new one: with no holder there is nothing to end or give back.
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
      return kOk;
    }
    return Fail(error, kNotConnected);
  }
  int exit_code = kOk;
  switch (command.kind) {
    case CommandKind::kTabs: {
      std::optional<base::DictValue> result =
          Expect(link->Call("Koan.listTabs", base::DictValue()), &exit_code);
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
          Expect(link->Call("Koan.openTab", std::move(params)), &exit_code);
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
          ResolveTabId(*link, command.tab, &exit_code);
      if (!target) {
        return exit_code;
      }
      std::optional<std::string> session =
          SessionFor(*link, *target, &exit_code);
      if (!session) {
        return exit_code;
      }
      if (command.kind == CommandKind::kSnapshot) {
        std::optional<base::DictValue> tree =
            Expect(link->Call("Accessibility.getFullAXTree", base::DictValue(),
                              *session),
                   &exit_code);
        if (!tree) {
          return exit_code;
        }
        if (command.json) {
          PrintJson(*tree);
        } else {
          printf("%s", SnapshotText(*tree, kSnapshotLines).c_str());
        }
        return kOk;
      }
      if (command.kind == CommandKind::kScreenshot) {
        base::DictValue params;
        params.Set("format", "png");
        std::optional<base::DictValue> shot = Expect(
            link->Call("Page.captureScreenshot", std::move(params), *session),
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
      return Act(*link, command, *session);
    }
    case CommandKind::kTakeOver: {
      base::DictValue params;
      if (!command.tab.empty()) {
        params.Set("targetId", command.tab);
      }
      fprintf(stderr, "koan: the user is asked in Koan...\n");
      std::optional<base::DictValue> result = Expect(
          link->Call("Koan.takeOver", std::move(params), {}, kApprovalWait),
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
          ResolveTabId(*link, command.tab, &exit_code);
      if (!target) {
        return exit_code;
      }
      base::DictValue params;
      params.Set("targetId", *target);
      return Expect(link->Call("Koan.release", std::move(params)), &exit_code)
                 ? kOk
                 : exit_code;
    }
    case CommandKind::kEnd: {
      if (!command.task.empty()) {
        base::DictValue params;
        params.Set("task", command.task);
        return Expect(link->Call("Koan.endTask", std::move(params)), &exit_code)
                   ? kOk
                   : exit_code;
      }
      return Expect(link->Call("Hold.exit", base::DictValue()), &exit_code)
                 ? kOk
                 : exit_code;
    }
    case CommandKind::kWatch: {
      if (!Expect(link->Call("Hold.watch", base::DictValue()), &exit_code)) {
        return exit_code;
      }
      while (std::optional<std::string> frame =
                 link->channel().Read(base::TimeDelta::Max())) {
        printf("%s\n", frame->c_str());
        fflush(stdout);
      }
      return kOk;
    }
    default:
      return Fail("not a command", kUsage);
  }
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
