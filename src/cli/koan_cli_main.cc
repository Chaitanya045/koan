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
#include "base/files/file_enumerator.h"
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
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
#include "koan/cli/koan_cli_skill.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace koan::cli {
namespace {

constexpr char kVersion[] = "1";
// The user may take a while over the approval dialog (60 s in the browser).
constexpr base::TimeDelta kApprovalWait = base::Seconds(75);
constexpr base::TimeDelta kAnswerWait = base::Seconds(30);
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

base::FilePath Home() {
  const char* home = getenv("HOME");
  return base::FilePath(home && *home ? home : "/");
}

base::FilePath KoanDir() {
  return KoanHome(Home());
}

base::FilePath UserDataDir(const Command& command) {
  return command.user_data_dir.empty()
             ? DefaultUserDataDir(Home(), getenv("KOAN_USER_DATA_DIR"))
             : base::FilePath(command.user_data_dir);
}

// A directory of the user's alone: made 0700 if missing, refused if it is a
// link or another user's.
bool PrivateDir(const base::FilePath& dir) {
  if (mkdir(dir.value().c_str(), 0700) != 0 && errno != EEXIST) {
    return false;
  }
  struct stat info;
  return lstat(dir.value().c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
         info.st_uid == geteuid() && chmod(dir.value().c_str(), 0700) == 0;
}

bool WritePrivateFile(const base::FilePath& path, const std::string& data) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(path.value().c_str(),
           O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600)));
  return fd.is_valid() && base::WriteFileDescriptor(fd.get(), data) &&
         fchmod(fd.get(), 0600) == 0;
}

// --- Clients and keys ------------------------------------------------------

std::vector<std::string> PairedClients() {
  std::vector<std::string> ids;
  base::FileEnumerator dirs(KoanDir().Append("clients"), false,
                            base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dirs.Next(); !dir.empty(); dir = dirs.Next()) {
    std::string json;
    if (!base::ReadFileToString(dir.Append("client.json"), &json)) {
      continue;
    }
    if (std::optional<ClientInfo> client = ParseClient(json);
        client && client->client_id == dir.BaseName().value()) {
      ids.push_back(client->client_id);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

struct Key {
  uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
  uint8_t private_key[ED25519_PRIVATE_KEY_LEN];
};

std::optional<Key> LoadKey(const std::string& client_id) {
  std::string encoded;
  if (!base::ReadFileToString(ClientDir(KoanDir(), client_id).Append("key"),
                              &encoded)) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> seed = base::Base64Decode(
      std::string(base::TrimWhitespaceASCII(encoded, base::TRIM_ALL)));
  if (!seed || seed->size() != 32) {
    return std::nullopt;
  }
  Key key;
  ED25519_keypair_from_seed(key.public_key, key.private_key, seed->data());
  return key;
}

std::string Sign(const Key& key, base::span<const uint8_t> message) {
  uint8_t signature[ED25519_SIGNATURE_LEN];
  ED25519_sign(signature, message.data(), message.size(), key.private_key);
  return base::Base64Encode(signature);
}

// --- Talking to the browser or the holder -----------------------------------

class Link {
 public:
  explicit Link(std::unique_ptr<FrameChannel> channel)
      : channel_(std::move(channel)) {}

  FrameChannel& channel() { return *channel_; }

  // Sends a request and waits for its answer; events that come meanwhile are
  // passed to `on_event` if given.
  std::optional<Message> Call(
      std::string_view method,
      base::DictValue params,
      std::string_view session_id = {},
      base::TimeDelta wait = kAnswerWait,
      base::RepeatingCallback<void(const Message&)> on_event = {}) {
    const int id = next_id_++;
    if (!channel_->Write(Request(id, method, std::move(params), session_id))) {
      return std::nullopt;
    }
    const base::TimeTicks deadline = base::TimeTicks::Now() + wait;
    while (true) {
      const base::TimeDelta left = deadline - base::TimeTicks::Now();
      if (left <= base::TimeDelta()) {
        return std::nullopt;
      }
      std::optional<std::string> frame = channel_->Read(left);
      if (!frame) {
        return std::nullopt;
      }
      std::optional<Message> message = ParseMessage(*frame);
      if (!message) {
        continue;
      }
      if (message->id == id) {
        return message;
      }
      if (!message->id && on_event) {
        on_event.Run(*message);
      }
    }
  }

 private:
  std::unique_ptr<FrameChannel> channel_;
  int next_id_ = 1;
};

std::string Describe(const std::optional<Message>& answer) {
  if (!answer) {
    return "no answer (Koan quit, or it took too long)";
  }
  return answer->error_code + ": " + answer->error_message;
}

// Dials the browser's socket from its discovery file.
std::unique_ptr<FrameChannel> DialBrowser(const Command& command,
                                          std::string* error) {
  const base::FilePath file = DiscoveryFile(UserDataDir(command));
  std::string json;
  if (!base::ReadFileToString(file, &json)) {
    *error =
        "Koan is not running, or Agent ready is off (Settings > Agent "
        "Control); no " +
        file.value();
    return nullptr;
  }
  std::optional<Discovery> discovery = ParseDiscovery(json);
  if (!discovery) {
    *error = file.value() + " is not readable";
    return nullptr;
  }
  // A discovery file left by a browser that is gone.
  if (kill(discovery->pid, 0) != 0 && errno == ESRCH) {
    *error = "Koan is not running (its discovery file is stale)";
    return nullptr;
  }
  base::ScopedFD fd = ConnectUnix(discovery->socket, error);
  if (!fd.is_valid()) {
    return nullptr;
  }
  return std::make_unique<FrameChannel>(std::move(fd), agent::kMaxPayload);
}

// Proves the client to the browser (design section 5.2).
bool Authenticate(Link& link,
                  const std::string& client_id,
                  std::string* error) {
  std::optional<Key> key = LoadKey(client_id);
  if (!key) {
    *error = "the key of client " + client_id + " is missing or unreadable";
    return false;
  }
  base::DictValue ask;
  ask.Set("clientId", client_id);
  std::optional<Message> nonce_answer =
      link.Call("Koan.authenticate", std::move(ask));
  const std::string* nonce_text =
      nonce_answer && nonce_answer->result
          ? nonce_answer->result->FindString("nonce")
          : nullptr;
  std::optional<std::vector<uint8_t>> nonce =
      nonce_text ? base::Base64Decode(*nonce_text) : std::nullopt;
  if (!nonce || nonce->size() != agent::kNonceSize) {
    *error = "Koan did not start authentication: " + Describe(nonce_answer);
    return false;
  }
  base::DictValue proof;
  proof.Set("clientId", client_id);
  proof.Set("signature", Sign(*key, agent::ProofMessage(client_id, *nonce)));
  // A changed program or chain is asked about in Koan: wait for the user.
  std::optional<Message> answer =
      link.Call("Koan.authenticate", std::move(proof), {}, kApprovalWait);
  if (!answer || answer->is_error() || !answer->result) {
    *error = answer && answer->error_code == "Koan.authFailed"
                 ? "Koan does not know this client any more: pair again "
                   "(koan pair)"
                 : "authentication failed: " + Describe(answer);
    return false;
  }
  return true;
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
  const std::vector<std::string> paired = PairedClients();
  std::optional<std::string> client_id =
      ChooseClient(paired, command.client, &error);
  int ready_fd = command.ready_fd;
  if (!client_id) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  std::unique_ptr<FrameChannel> browser = DialBrowser(command, &error);
  if (!browser) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  Link link(std::move(browser));
  if (!Authenticate(link, *client_id, &error)) {
    SayReady(ready_fd, "error " + error);
    return kNotConnected;
  }
  if (!PrivateDir(KoanDir()) || !PrivateDir(KoanDir().Append("run"))) {
    SayReady(ready_fd, "error ~/.koan/run is not a private directory");
    return kFailed;
  }
  base::ScopedFD listener = ListenUnix(HolderPath(*client_id), &error);
  if (!listener.is_valid()) {
    SayReady(ready_fd, "error " + error);
    return kFailed;
  }
  SayReady(ready_fd, "ok");
  ready_fd = -1;

  Router router;
  std::map<Router::ConnectionId, std::unique_ptr<FrameChannel>> commands;
  Router::ConnectionId next_connection = 1;
  base::TimeTicks last_command = base::TimeTicks::Now();
  bool running = true;
  FrameChannel& to_browser = link.channel();

  auto send_to = [&](Router::ConnectionId id, const std::string& json) {
    auto it = commands.find(id);
    if (it != commands.end() && !it->second->Write(json)) {
      router.RemoveConnection(id);
      commands.erase(it);
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
      // Only this user's commands.
      if (fd.is_valid() && PeerIsSameUser(fd.get())) {
        fcntl(fd.get(), F_SETFD, FD_CLOEXEC);
        // A command that stops reading cannot stall the holder for long.
        timeval send_timeout = {2, 0};
        setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &send_timeout,
                   sizeof(send_timeout));
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
      const bool open = channel.ReadAvailable();
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
      ChooseClient(PairedClients(), command.client, error);
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
  if (!PrivateDir(KoanDir()) || !PrivateDir(KoanDir().Append("clients"))) {
    return Fail("~/.koan is not a private directory");
  }
  std::string error;
  std::unique_ptr<FrameChannel> browser = DialBrowser(command, &error);
  if (!browser) {
    return Fail(error, kNotConnected);
  }
  Link link(std::move(browser));
  uint8_t seed[32];
  RAND_bytes(seed, sizeof(seed));
  Key key;
  ED25519_keypair_from_seed(key.public_key, key.private_key, seed);
  const std::string label = command.label.empty() ? "koan" : command.label;
  base::DictValue params;
  params.Set("publicKey", base::Base64Encode(key.public_key));
  params.Set("label", label);
  base::ListValue capabilities;
  for (const std::string& name : command.capabilities) {
    capabilities.Append(name);
  }
  params.Set("capabilities", std::move(capabilities));
  params.Set("proof", Sign(key, agent::PairingMessage(key.public_key, label)));
  fprintf(stderr, "koan: approve \"%s\" in Koan...\n", label.c_str());
  std::optional<Message> answer =
      link.Call("Koan.pair", std::move(params), {}, kApprovalWait);
  int exit_code = kOk;
  std::optional<base::DictValue> result = Expect(answer, &exit_code);
  const std::string* client_id =
      result ? result->FindString("clientId") : nullptr;
  if (!client_id || !agent::IsValidClientId(*client_id)) {
    return result ? Fail("Koan answered without a client id") : exit_code;
  }
  const base::FilePath dir = ClientDir(KoanDir(), *client_id);
  if (!PrivateDir(dir) ||
      !WritePrivateFile(dir.Append("key"), base::Base64Encode(seed)) ||
      !WritePrivateFile(dir.Append("client.json"),
                        SerializeClient({*client_id, label}))) {
    return Fail("cannot write " + dir.value());
  }
  if (command.json) {
    PrintJson(*result);
  } else {
    printf("paired as %s (%s)\n", client_id->c_str(), label.c_str());
  }
  return kOk;
}

int Status(const Command& command) {
  base::DictValue status;
  std::string error;
  std::unique_ptr<FrameChannel> probe = DialBrowser(command, &error);
  status.Set("koanRunning", static_cast<bool>(probe));
  probe.reset();
  const std::vector<std::string> paired = PairedClients();
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
