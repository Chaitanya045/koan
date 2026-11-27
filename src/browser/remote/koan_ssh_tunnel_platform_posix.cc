// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_tunnel_platform_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_view_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/sequence_bound.h"
#include "build/build_config.h"
#include "koan/browser/remote/koan_ssh_askpass_server.h"
#include "koan/browser/remote/koan_ssh_command.h"

namespace koan::remote {

namespace {

// After SIGTERM, how long the group has before SIGKILL.
constexpr base::TimeDelta kKillGrace = base::Seconds(kSshKillGraceSeconds);
// How often an exited supervisor is polled for until it can be reaped.
constexpr base::TimeDelta kReapPoll = base::Milliseconds(20);
// Longer stderr "lines" are passed on in pieces.
constexpr size_t kMaxStderrLine = 4096;
// The supervisor's end of its exit pipe (BuildSupervisedArgv()).
constexpr int kExitPipeFd = 4;

constexpr base::TaskTraits kBlockingTraits = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN};

bool IsTrustedSystemExecutable(const base::FilePath& path) {
  struct stat info;
  return stat(path.value().c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_uid == 0 && (info.st_mode & (S_IWGRP | S_IWOTH)) == 0 &&
         (info.st_mode & S_IXUSR) != 0 &&
         access(path.value().c_str(), X_OK) == 0;
}

base::FilePath ResolveTrusted(const char* path) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  const base::FilePath file(path);
  return IsTrustedSystemExecutable(file) ? file : base::FilePath();
}

bool SetBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  return flags != -1 && fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

// A pipe with close-on-exec on both ends; the end `child_end` (0 read,
// 1 write) is blocking, the other non-blocking.
bool CreateChildPipe(int child_end,
                     base::ScopedFD* read_end,
                     base::ScopedFD* write_end) {
  std::array<int, 2> fds;
  if (!base::CreateLocalNonBlockingPipe(fds)) {
    return false;
  }
  read_end->reset(fds[0]);
  write_end->reset(fds[1]);
  return SetBlocking(child_end == 0 ? read_end->get() : write_end->get());
}

// Whether the supervisor has exited (or is exiting): its exit pipe, of which
// it holds the only write end and never writes to, is closed. Non-blocking.
bool IsExitPipeClosed(int fd) {
  std::array<char, 1> byte;
  const ssize_t n = HANDLE_EINTR(read(fd, byte.data(), byte.size()));
  return n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
}

// Reaps `process` (the supervisor) once it has exited, polling, and runs
// `done` with its exit code. Its group gets SIGKILL first: at `kill_at`, or
// as soon as the supervisor has exited (`exit_pipe` closed), so nothing of
// the group outlives it. The supervisor isn't reaped before that, so its pid,
// the group's id, can't have been reused.
void ReapWhenExited(base::Process process,
                    base::ScopedFD exit_pipe,
                    base::TimeTicks kill_at,
                    base::OnceCallback<void(int)> done) {
  if (base::TimeTicks::Now() >= kill_at || IsExitPipeClosed(exit_pipe.get())) {
    kill(-process.Pid(), SIGKILL);
    int exit_code = -1;
    if (process.WaitForExitWithTimeout(base::TimeDelta(), &exit_code)) {
      if (done) {
        std::move(done).Run(exit_code);
      }
      return;
    }
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ReapWhenExited, std::move(process), std::move(exit_pipe),
                     kill_at, std::move(done)),
      kReapPoll);
}

// The ssh process, on a blocking sequence of its own.
class SshProcessIo {
 public:
  struct Callbacks {
    // Each runs on the tunnel's sequence (BindPostTask).
    base::RepeatingCallback<void(std::string)> on_stderr_line;
    base::RepeatingCallback<void(int id, SshPrompt)> on_prompt;
    base::OnceCallback<void(int)> on_exit;
  };

  // `ssh_argv_for_testing`, if not empty, runs in place of ssh.
  SshProcessIo(SshDestination destination,
               uint16_t port,
               std::vector<std::string> ssh_argv_for_testing,
               Callbacks callbacks)
      : destination_(std::move(destination)),
        port_(port),
        ssh_argv_for_testing_(std::move(ssh_argv_for_testing)),
        callbacks_(std::move(callbacks)) {}

  SshProcessIo(const SshProcessIo&) = delete;
  SshProcessIo& operator=(const SshProcessIo&) = delete;

  ~SshProcessIo() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (process_.IsValid()) {
      // First of all: ssh must be gone before one of its askpass helpers
      // could finish, so an open prompt is never answered (not even with a
      // refusal, which ssh would send as an empty password). Not reaped yet,
      // so the group id is still ours. The tunnel's end of the stdin pipe is
      // closed already, which makes the supervisor do the same.
      kill(-process_.Pid(), SIGTERM);
    }
    stderr_watch_.reset();
    exit_watch_.reset();
    askpass_.reset();
    if (process_.IsValid()) {
      ReapWhenExited(std::move(process_), std::move(exit_pipe_),
                     base::TimeTicks::Now() + kKillGrace,
                     base::OnceCallback<void(int)>());
    }
    // The askpass directory goes with dir_ (a blocking sequence).
  }

  // Launches the supervisor. Returns the write end of its stdin pipe, or an
  // invalid fd (and runs on_exit) on failure.
  base::ScopedFD Start() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    base::ScopedBlockingCall scoped_blocking_call(
        FROM_HERE, base::BlockingType::MAY_BLOCK);
    base::ScopedFD stdin_write = Launch();
    if (!stdin_write.is_valid()) {
      askpass_.reset();
      std::move(callbacks_.on_exit)
          .Run(SshTunnelPlatform::kLaunchFailedExitCode);
    }
    return stdin_write;
  }

  void Reply(int id, SshPromptReply reply) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (askpass_) {
      askpass_->Reply(id, reply);
    }
  }

 private:
  base::ScopedFD Launch() {
    const base::FilePath sh = ResolveSystemShell();
    std::vector<std::string> ssh_argv = ssh_argv_for_testing_;
    if (ssh_argv.empty()) {
      const base::FilePath ssh = ResolveSystemSsh();
      if (ssh.empty()) {
        return base::ScopedFD();
      }
      ssh_argv = BuildSshTunnelArgv(ssh, destination_, port_);
    }
    if (sh.empty() || !dir_.CreateUniqueTempDir() ||
        !base::SetPosixFilePermissions(dir_.GetPath(), 0700)) {
      return base::ScopedFD();
    }
    askpass_ = SshAskpassServer::Create(
        dir_.GetPath(),
        base::BindRepeating(&SshProcessIo::OnPrompt,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(&SshProcessIo::OnBadPromptRequest,
                            weak_factory_.GetWeakPtr()));
    if (!askpass_) {
      return base::ScopedFD();
    }
    base::ScopedFD stdin_read, stdin_write, stderr_read, stderr_write,
        exit_read, exit_write;
    if (!CreateChildPipe(0, &stdin_read, &stdin_write) ||
        !CreateChildPipe(1, &stderr_read, &stderr_write) ||
        !CreateChildPipe(1, &exit_read, &exit_write)) {
      return base::ScopedFD();
    }
    base::ScopedFD dev_null(
        HANDLE_EINTR(open("/dev/null", O_RDWR | O_CLOEXEC)));
    if (!dev_null.is_valid()) {
      return base::ScopedFD();
    }
    base::LaunchOptions options;
    options.fds_to_remap = {{stdin_read.get(), STDIN_FILENO},
                            {dev_null.get(), STDOUT_FILENO},
                            {stderr_write.get(), STDERR_FILENO},
                            {exit_write.get(), kExitPipeFd}};
    options.new_process_group = true;
    options.environment = BuildSshEnvironment(askpass_->helper_path(),
                                              *base::Environment::Create());
    process_ = base::LaunchProcess(BuildSupervisedArgv(sh, ssh_argv), options);
    if (!process_.IsValid()) {
      return base::ScopedFD();
    }
    // The supervisor leads its group: its pid is the group's id.
    askpass_->SetProcessGroup(process_.Pid());
    stderr_ = std::move(stderr_read);
    stderr_watch_ = base::FileDescriptorWatcher::WatchReadable(
        stderr_.get(), base::BindRepeating(&SshProcessIo::OnStderrReadable,
                                           base::Unretained(this)));
    exit_pipe_ = std::move(exit_read);
    exit_watch_ = base::FileDescriptorWatcher::WatchReadable(
        exit_pipe_.get(), base::BindRepeating(&SshProcessIo::OnExitPipeReadable,
                                              base::Unretained(this)));
    return stdin_write;
  }

  void OnPrompt(int id, SshPrompt prompt) {
    callbacks_.on_prompt.Run(id, std::move(prompt));
  }

  // A helper's request couldn't be read, and it gets no answer: stop ssh
  // instead (the end is reported as for any exit).
  void OnBadPromptRequest() {
    if (process_.IsValid()) {
      kill(-process_.Pid(), SIGTERM);
    }
  }

  enum class ReadResult { kData, kEmpty, kClosed };

  // Reads what the stderr pipe holds (up to a buffer) and passes on its
  // lines.
  ReadResult ReadStderr() {
    std::array<char, 4096> buffer;
    const ssize_t n =
        HANDLE_EINTR(read(stderr_.get(), buffer.data(), buffer.size()));
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return ReadResult::kEmpty;
    }
    if (n <= 0) {
      return ReadResult::kClosed;
    }
    partial_line_.append(
        base::as_string_view(base::span(buffer).first(static_cast<size_t>(n))));
    size_t newline;
    while ((newline = partial_line_.find('\n')) != std::string::npos) {
      callbacks_.on_stderr_line.Run(partial_line_.substr(0, newline));
      partial_line_.erase(0, newline + 1);
    }
    if (partial_line_.size() > kMaxStderrLine) {
      callbacks_.on_stderr_line.Run(std::move(partial_line_));
      partial_line_.clear();
    }
    return ReadResult::kData;
  }

  void OnStderrReadable() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (ReadStderr() == ReadResult::kClosed) {
      OnSupervisorExited();
    }
  }

  void OnExitPipeReadable() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (IsExitPipeClosed(exit_pipe_.get())) {
      OnSupervisorExited();
    }
  }

  // The supervisor has exited: its exit pipe closed, or stderr did (which
  // every member of the group holds, the supervisor until it exits). Stderr
  // can stay open after it, held by a member that ignores SIGTERM (a
  // ProxyCommand that traps it), so the exit pipe is what counts.
  void OnSupervisorExited() {
    if (!process_.IsValid()) {
      return;
    }
    // Whatever is left of the group goes now; until the supervisor is
    // reaped, the group id is still ours.
    kill(-process_.Pid(), SIGKILL);
    exit_watch_.reset();
    stderr_watch_.reset();
    // What ssh printed before it exited (its reason) is still in the pipe.
    while (ReadStderr() == ReadResult::kData) {
    }
    stderr_.reset();
    if (!partial_line_.empty()) {
      callbacks_.on_stderr_line.Run(std::move(partial_line_));
      partial_line_.clear();
    }
    askpass_.reset();
    ReapWhenExited(std::move(process_), std::move(exit_pipe_),
                   base::TimeTicks::Now(), std::move(callbacks_.on_exit));
  }

  const SshDestination destination_;
  const uint16_t port_;
  const std::vector<std::string> ssh_argv_for_testing_;
  Callbacks callbacks_;
  base::ScopedTempDir dir_;
  std::unique_ptr<SshAskpassServer> askpass_;
  base::Process process_;
  base::ScopedFD stderr_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stderr_watch_;
  base::ScopedFD exit_pipe_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> exit_watch_;
  std::string partial_line_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<SshProcessIo> weak_factory_{this};
};

// The tunnel's handle, on the tunnel's sequence.
class SshProcessHandle : public SshTunnelPlatform::Process {
 public:
  SshProcessHandle(const SshDestination& destination,
                   uint16_t port,
                   std::vector<std::string> ssh_argv_for_testing,
                   SshTunnelPlatform::ProcessCallbacks callbacks)
      : callbacks_(std::move(callbacks)) {
    scoped_refptr<base::SequencedTaskRunner> current =
        base::SequencedTaskRunner::GetCurrentDefault();
    SshProcessIo::Callbacks io_callbacks;
    io_callbacks.on_stderr_line = base::BindPostTask(
        current, base::BindRepeating(&SshProcessHandle::OnStderrLine,
                                     weak_factory_.GetWeakPtr()));
    io_callbacks.on_prompt = base::BindPostTask(
        current, base::BindRepeating(&SshProcessHandle::OnPrompt,
                                     weak_factory_.GetWeakPtr()));
    io_callbacks.on_exit = base::BindPostTask(
        current,
        base::BindOnce(&SshProcessHandle::OnExit, weak_factory_.GetWeakPtr()));
    io_ = base::SequenceBound<SshProcessIo>(
        base::ThreadPool::CreateSequencedTaskRunner(kBlockingTraits),
        destination, port, std::move(ssh_argv_for_testing),
        std::move(io_callbacks));
    // If this handle is gone by the reply, dropping the fd closes the pipe.
    io_.AsyncCall(&SshProcessIo::Start)
        .Then(base::BindOnce(&SshProcessHandle::OnStarted,
                             weak_factory_.GetWeakPtr()));
  }

  SshProcessHandle(const SshProcessHandle&) = delete;
  SshProcessHandle& operator=(const SshProcessHandle&) = delete;

  // Closing the pipe ends the tunnel (SshTunnelPlatformPosix); destroying
  // io_ kills and reaps the group.
  ~SshProcessHandle() override = default;

 private:
  void OnStarted(base::ScopedFD stdin_write) {
    stdin_write_ = std::move(stdin_write);
  }

  void OnStderrLine(std::string line) {
    callbacks_.on_stderr_line.Run(std::move(line));
  }

  void OnPrompt(int id, SshPrompt prompt) {
    SshPromptReplyCallback reply;
    if (prompt.kind != SshPromptKind::kNotice) {
      reply = base::BindOnce(&SshProcessHandle::Reply,
                             weak_factory_.GetWeakPtr(), id);
    }
    callbacks_.on_prompt.Run(std::move(prompt), std::move(reply));
  }

  void Reply(int id, SshPromptReply reply) {
    io_.AsyncCall(&SshProcessIo::Reply).WithArgs(id, std::move(reply));
  }

  void OnExit(int exit_code) {
    stdin_write_.reset();
    std::move(callbacks_.on_exit).Run(exit_code);
  }

  SshTunnelPlatform::ProcessCallbacks callbacks_;
  base::ScopedFD stdin_write_;
  base::SequenceBound<SshProcessIo> io_;
  base::WeakPtrFactory<SshProcessHandle> weak_factory_{this};
};

}  // namespace

base::FilePath ResolveSystemSsh() {
  return ResolveTrusted("/usr/bin/ssh");
}

base::FilePath ResolveSystemShell() {
  return ResolveTrusted("/bin/sh");
}

std::optional<uint16_t> PickFreeLoopbackPort(
    std::optional<uint16_t> preferred) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  auto try_bind = [](uint16_t port) -> std::optional<uint16_t> {
    base::ScopedFD fd(socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.is_valid()) {
      return std::nullopt;
    }
    sockaddr_in address = {};
#if BUILDFLAG(IS_APPLE)
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd.get(), reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0) {
      return std::nullopt;
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd.get(), reinterpret_cast<sockaddr*>(&address), &length) !=
        0) {
      return std::nullopt;
    }
    const uint16_t bound = ntohs(address.sin_port);
    return bound == 0 ? std::nullopt : std::optional<uint16_t>(bound);
  };
  if (preferred && *preferred != 0) {
    if (std::optional<uint16_t> port = try_bind(*preferred)) {
      return port;
    }
  }
  return try_bind(0);
}

bool ProbeSocks5(uint16_t port) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  base::ScopedFD fd(socket(AF_INET, SOCK_STREAM, 0));
  if (!fd.is_valid()) {
    return false;
  }
  fcntl(fd.get(), F_SETFD, FD_CLOEXEC);
#if BUILDFLAG(IS_APPLE)
  const int one = 1;
  setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  timeval timeout = {};
  timeout.tv_sec = 1;
  setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address = {};
#if BUILDFLAG(IS_APPLE)
  address.sin_len = sizeof(address);
#endif
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (HANDLE_EINTR(connect(fd.get(),
                           reinterpret_cast<const sockaddr*>(&address),
                           sizeof(address))) != 0) {
    return false;
  }
  // SOCKS5 greeting: version 5, one method, "no authentication" (RFC 1928).
  constexpr std::array<uint8_t, 3> kGreeting = {0x05, 0x01, 0x00};
  int flags = 0;
#if defined(MSG_NOSIGNAL)
  flags = MSG_NOSIGNAL;
#endif
  if (HANDLE_EINTR(send(fd.get(), kGreeting.data(), kGreeting.size(), flags)) !=
      static_cast<ssize_t>(kGreeting.size())) {
    return false;
  }
  std::array<uint8_t, 2> reply = {};
  size_t received = 0;
  while (received < reply.size()) {
    base::span<uint8_t> rest = base::span(reply).subspan(received);
    const ssize_t n = HANDLE_EINTR(recv(fd.get(), rest.data(), rest.size(), 0));
    if (n <= 0) {
      return false;
    }
    received += static_cast<size_t>(n);
  }
  return reply[0] == 0x05 && reply[1] == 0x00;
}

SshTunnelPlatformPosix::SshTunnelPlatformPosix() = default;
SshTunnelPlatformPosix::~SshTunnelPlatformPosix() = default;

std::unique_ptr<SshTunnelPlatform::Process> SshTunnelPlatformPosix::Launch(
    const SshDestination& destination,
    uint16_t port,
    ProcessCallbacks callbacks) {
  return std::make_unique<SshProcessHandle>(
      destination, port, ssh_argv_for_testing_, std::move(callbacks));
}

void SshTunnelPlatformPosix::PickPort(
    std::optional<uint16_t> preferred,
    base::OnceCallback<void(std::optional<uint16_t>)> callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kBlockingTraits,
      base::BindOnce(&PickFreeLoopbackPort, preferred), std::move(callback));
}

void SshTunnelPlatformPosix::ProbeSocks(
    uint16_t port,
    base::OnceCallback<void(bool)> callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kBlockingTraits, base::BindOnce(&ProbeSocks5, port),
      std::move(callback));
}

}  // namespace koan::remote
