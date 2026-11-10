// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// `koan-relay` (.agent/notes/agent-control.md section 11): a WebSocket CDP
// endpoint on loopback for agent-browser, which cannot dial agent control's
// unix socket. It is a paired client of its own, with its own key in
// ~/.koan/relay/clients and its own approval naming `koan-relay` and the
// agent that started it; it never goes through the `koan` holder. One relay
// per agent-browser session, started by `koan agent-browser`:
//
//   koan-relay pair [--label L] [--capabilities ...] [--user-data-dir D]
//   koan-relay serve --session S [--client ID] [--user-data-dir D]
//
// `serve` returns once the relay runs in the background: it has proved itself
// to the browser, listens on 127.0.0.1:<random port> for one path with a
// 128-bit secret (koan_relay_gate.h), and has written the URL to a 0600 env
// file. It never prints the secret. It ends when the browser closes its
// connection (Stop, revoke, Agent ready off), on `koan relay stop`, when
// nothing connected in its first 30 s, or after 15 minutes without a frame;
// the browser then ends its tasks.

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/posix/eintr_wrapper.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_executor.h"
#include "base/timer/timer.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/cli/koan_cli_client.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_relay_gate.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace koan::cli {
namespace {

constexpr net::NetworkTrafficAnnotationTag kRelayTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("koan_agent_relay", R"(
      semantics {
        sender: "Koan agent relay"
        description:
          "A loopback WebSocket that carries the DevTools protocol between "
          "agent-browser and Koan's agent control, for an agent the user "
          "paired and approved."
        trigger: "The agent runs `koan agent-browser`."
        data: "Agent control's filtered DevTools protocol for the agent's tabs."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings > Agent Control: Agent ready, and the paired agents."
        policy_exception_justification:
          "Agent control follows RemoteDebuggingAllowed."
      })");

enum Exit {
  kOk = 0,
  kFailed = 1,
  kUsage = 2,
  kNotConnected = 3,
};

int Fail(const std::string& text, int code = kFailed) {
  fprintf(stderr, "koan-relay: %s\n", text.c_str());
  return code;
}

base::FilePath Home() {
  return RelayHome(KoanDir());
}

struct Options {
  std::string mode;
  std::string session;
  std::string client;
  std::string label;
  std::string user_data_dir;
  std::vector<std::string> capabilities;
};

std::optional<Options> Parse(const std::vector<std::string>& args,
                             std::string* error) {
  Options options;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    auto value = [&](std::string* out) {
      if (i + 1 >= args.size() || args[i + 1].empty()) {
        *error = arg + " needs a value";
        return false;
      }
      *out = args[++i];
      return true;
    };
    std::string capabilities;
    if (arg == "--session") {
      if (!value(&options.session)) {
        return std::nullopt;
      }
    } else if (arg == "--client") {
      if (!value(&options.client)) {
        return std::nullopt;
      }
    } else if (arg == "--label") {
      if (!value(&options.label)) {
        return std::nullopt;
      }
    } else if (arg == "--user-data-dir") {
      if (!value(&options.user_data_dir)) {
        return std::nullopt;
      }
    } else if (arg == "--capabilities") {
      if (!value(&capabilities)) {
        return std::nullopt;
      }
      for (const std::string& name :
           base::SplitString(capabilities, ",", base::TRIM_WHITESPACE,
                             base::SPLIT_WANT_NONEMPTY)) {
        options.capabilities.push_back(name);
      }
    } else if (options.mode.empty() && !arg.starts_with("-")) {
      options.mode = arg;
    } else {
      *error = "unexpected " + arg;
      return std::nullopt;
    }
  }
  if (options.mode != "pair" && options.mode != "serve") {
    *error =
        "usage: koan-relay pair | serve --session S (started by koan "
        "agent-browser)";
    return std::nullopt;
  }
  if (options.mode == "serve" && !IsValidSessionName(options.session)) {
    *error = "serve needs --session with 1 to 64 letters, digits, . _ -";
    return std::nullopt;
  }
  return options;
}

int Pair(const Options& options) {
  std::string error;
  std::unique_ptr<FrameChannel> browser =
      DialBrowser(UserDataDir(options.user_data_dir), &error);
  if (!browser) {
    return Fail(error, kNotConnected);
  }
  Link link(std::move(browser));
  const std::string label =
      options.label.empty() ? "koan-relay (agent-browser)" : options.label;
  fprintf(stderr, "koan-relay: approve \"%s\" in Koan...\n", label.c_str());
  std::optional<base::DictValue> result =
      PairNewClient(link, Home(), label, options.capabilities, &error);
  if (!result) {
    return Fail(error);
  }
  fprintf(stderr, "koan-relay: paired\n");
  return kOk;
}

void SayReady(base::ScopedFD fd, const std::string& line) {
  if (fd.is_valid()) {
    base::WriteFileDescriptor(fd.get(), line + "\n");
  }
}

// The relay's loop: the browser's socket, the loopback WebSocket server and
// the control socket, on one IO thread.
class Relay : public net::HttpServer::Delegate {
 public:
  Relay(std::unique_ptr<Link> browser,
        std::unique_ptr<net::ServerSocket> server_socket,
        uint16_t port,
        std::string secret,
        base::ScopedFD control,
        std::unique_ptr<agent::PeerResolver> resolver,
        base::FilePath log_path,
        base::OnceClosure quit)
      : browser_(std::move(browser)),
        server_(std::move(server_socket), this),
        gate_(std::move(secret), port, base::TimeTicks::Now()),
        control_(std::move(control)),
        resolver_(std::move(resolver)),
        log_path_(std::move(log_path)),
        quit_(std::move(quit)),
        last_frame_(base::TimeTicks::Now()) {
    browser_watch_ = base::FileDescriptorWatcher::WatchReadable(
        browser_->channel().fd(),
        base::BindRepeating(&Relay::OnBrowserReadable, base::Unretained(this)));
    control_watch_ = base::FileDescriptorWatcher::WatchReadable(
        control_.get(),
        base::BindRepeating(&Relay::OnControl, base::Unretained(this)));
    tick_.Start(FROM_HERE, base::Seconds(5),
                base::BindRepeating(&Relay::OnTick, base::Unretained(this)));
  }

  std::string Url() const { return gate_.Url(); }

  // net::HttpServer::Delegate:
  void OnConnect(int connection_id) override {}

  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override {
    // No /json, no listing: only the WebSocket.
    Log("refused http " + info.path.substr(0, 5));
    server_.Send404(connection_id, kRelayTrafficAnnotation);
    server_.Close(connection_id);
  }

  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override {
    std::optional<std::string> origin;
    if (info.headers.contains("origin")) {
      origin = info.GetHeaderValue("origin");
    }
    const RelayGate::Decision decision = gate_.OnWebSocketRequest(
        connection_id, info.path, info.GetHeaderValue("host"), origin,
        base::TimeTicks::Now());
    if (decision != RelayGate::Decision::kAccept) {
      // The path is never logged: it holds the secret.
      Log("refused websocket " + std::string(DecisionName(decision)));
      server_.Send404(connection_id, kRelayTrafficAnnotation);
      server_.Close(connection_id);
      return;
    }
    server_.SetReceiveBufferSize(connection_id, agent::kMaxPayload);
    server_.SetSendBufferSize(connection_id, 2 * agent::kMaxPayload);
    server_.AcceptWebSocket(connection_id, info, kRelayTrafficAnnotation);
    last_frame_ = base::TimeTicks::Now();
  }

  void OnWebSocketMessage(int connection_id, std::string data) override {
    if (gate_.active() != connection_id) {
      return;
    }
    last_frame_ = base::TimeTicks::Now();
    // The browser checks everything; the relay only carries it.
    if (!browser_->channel().Write(data)) {
      Quit();
    }
  }

  void OnClose(int connection_id) override { gate_.OnClosed(connection_id); }

 private:
  void OnBrowserReadable() {
    FrameChannel& channel = browser_->channel();
    if (!channel.ReadAvailable()) {
      // Stop, a revoke, Agent ready off, or Koan quit.
      Quit();
      return;
    }
    while (std::optional<std::string> frame = channel.TakeFrame()) {
      last_frame_ = base::TimeTicks::Now();
      // With no agent-browser connected, what comes is for nobody.
      if (std::optional<int> active = gate_.active()) {
        server_.SendOverWebSocket(*active, *frame, kRelayTrafficAnnotation);
      }
    }
  }

  // `koan relay stop`: a process of this user asks the relay to end.
  void OnControl() {
    base::ScopedFD fd(HANDLE_EINTR(accept(control_.get(), nullptr, nullptr)));
    if (!fd.is_valid()) {
      return;
    }
    std::optional<agent::PeerToken> token =
        resolver_ ? resolver_->TokenForSocket(fd.get()) : std::nullopt;
    std::optional<agent::ResolvedPeer> peer =
        token ? resolver_->Resolve(*token) : std::nullopt;
    if (!peer || peer->identity.uid != geteuid()) {
      Log("refused control");
      return;
    }
    FrameChannel channel(std::move(fd), agent::kMaxPreAuthPayload);
    std::optional<std::string> frame = channel.Read(base::Seconds(1));
    std::optional<Message> message =
        frame ? ParseMessage(*frame) : std::nullopt;
    if (message && message->id && message->method == "Relay.stop") {
      base::DictValue answer;
      answer.Set("id", *message->id);
      answer.Set("result", base::DictValue());
      channel.Write(base::WriteJson(answer).value_or("{}"));
      Quit();
    }
  }

  void OnTick() {
    const base::TimeTicks now = base::TimeTicks::Now();
    if (gate_.FirstConnectionMissed(now)) {
      Log("ended: no connection in time");
      Quit();
    } else if (now - last_frame_ > kRelayIdle) {
      Log("ended: idle");
      Quit();
    }
  }

  void Log(const std::string& line) {
    base::ScopedFD fd(HANDLE_EINTR(
        open(log_path_.value().c_str(),
             O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600)));
    if (fd.is_valid()) {
      base::WriteFileDescriptor(
          fd.get(), base::NumberToString(
                        base::Time::Now().InMillisecondsSinceUnixEpoch()) +
                        " " + line + "\n");
    }
  }

  void Quit() {
    if (quit_) {
      std::move(quit_).Run();
    }
  }

  std::unique_ptr<Link> browser_;
  net::HttpServer server_;
  RelayGate gate_;
  base::ScopedFD control_;
  std::unique_ptr<agent::PeerResolver> resolver_;
  const base::FilePath log_path_;
  base::OnceClosure quit_;
  base::TimeTicks last_frame_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> browser_watch_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> control_watch_;
  base::RepeatingTimer tick_;
};

// The background half of `serve`. Says "ok" or "error ..." on `ready`.
int Serve(const Options& options, base::ScopedFD ready) {
  std::string error;
  std::optional<std::string> client_id =
      ChooseClient(PairedClients(Home()), options.client, &error);
  if (!client_id) {
    SayReady(std::move(ready), "error the relay is not paired: " + error);
    return kNotConnected;
  }
  const base::FilePath run = Home().Append("run");
  if (!PrivateDir(KoanDir()) || !PrivateDir(Home()) || !PrivateDir(run)) {
    SayReady(std::move(ready), "error ~/.koan/relay is not private");
    return kFailed;
  }
  const base::FilePath control_path =
      RelayControlSocket(Home(), options.session);
  {
    std::string ignored;
    if (ConnectUnix(control_path.value(), &ignored).is_valid()) {
      SayReady(std::move(ready), "error a relay already runs for this session");
      return kFailed;
    }
  }

  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(executor.task_runner());

  // Proved as the relay's own client, while `koan agent-browser` (and the
  // agent that ran it) are still its ancestors, as when it paired.
  std::unique_ptr<FrameChannel> channel =
      DialBrowser(UserDataDir(options.user_data_dir), &error);
  if (!channel) {
    SayReady(std::move(ready), "error " + error);
    return kNotConnected;
  }
  auto browser = std::make_unique<Link>(std::move(channel));
  if (!Authenticate(*browser, Home(), *client_id, &error)) {
    SayReady(std::move(ready), "error " + error);
    return kNotConnected;
  }

  auto server_socket =
      std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
  if (server_socket->Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0),
                            /*backlog=*/4,
                            /*ipv6_only=*/std::nullopt) != net::OK) {
    SayReady(std::move(ready), "error cannot listen on 127.0.0.1");
    return kFailed;
  }
  net::IPEndPoint local;
  if (server_socket->GetLocalAddress(&local) != net::OK) {
    SayReady(std::move(ready), "error cannot read the relay's port");
    return kFailed;
  }
  base::ScopedFD control = ListenUnix(control_path.value(), &error);
  if (!control.is_valid()) {
    SayReady(std::move(ready), "error " + error);
    return kFailed;
  }

  uint8_t secret_bytes[16];
  RAND_bytes(secret_bytes, sizeof(secret_bytes));
  base::RunLoop run_loop;
  Relay relay(std::move(browser), std::move(server_socket), local.port(),
              base::ToLowerASCII(base::HexEncode(secret_bytes)),
              std::move(control), agent::CreatePlatformPeerResolver(),
              RelayEnvFile(Home(), options.session).ReplaceExtension(".log"),
              run_loop.QuitClosure());
  const base::FilePath env_file = RelayEnvFile(Home(), options.session);
  if (!WritePrivateFile(env_file, RelayEnvContents(relay.Url()))) {
    SayReady(std::move(ready), "error cannot write " + env_file.value());
    return kFailed;
  }
  SayReady(std::move(ready), "ok");
  run_loop.Run();
  // The secret dies with the relay.
  unlink(env_file.value().c_str());
  unlink(control_path.value().c_str());
  return kOk;
}

// `serve`: starts the relay in the background and waits until it runs.
int StartServe(const Options& options) {
  int fds[2];
  if (pipe(fds) != 0) {
    return Fail("pipe failed");
  }
  const pid_t pid = fork();
  if (pid < 0) {
    return Fail("fork failed");
  }
  if (pid == 0) {
    close(fds[0]);
    setsid();
    const int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
      dup2(null_fd, STDIN_FILENO);
      dup2(null_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
    }
    _exit(Serve(options, base::ScopedFD(fds[1])));
  }
  close(fds[1]);
  base::ScopedFD read_end(fds[0]);
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
    return kOk;
  }
  return Fail(line.starts_with("error ") ? line.substr(6)
                                         : "the relay did not start");
}

}  // namespace
}  // namespace koan::cli

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  signal(SIGPIPE, SIG_IGN);
  std::string error;
  std::optional<koan::cli::Options> options =
      koan::cli::Parse(std::vector<std::string>(argv + 1, argv + argc), &error);
  if (!options) {
    return koan::cli::Fail(error, koan::cli::kUsage);
  }
  return options->mode == "pair" ? koan::cli::Pair(*options)
                                 : koan::cli::StartServe(*options);
}
