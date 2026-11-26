// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_askpass_server.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/threading/scoped_blocking_call.h"

namespace koan::remote {

namespace {

constexpr char kHelperName[] = "askpass";
constexpr char kControlName[] = "ctl";
// "<pid>\n": a pid has at most 10 digits.
constexpr size_t kMaxControlLine = 16;

bool IsOwnedByUser(const struct stat& info) {
  return info.st_uid == geteuid();
}

base::FilePath RequestPath(const base::FilePath& dir, int id) {
  return dir.Append("q." + base::NumberToString(id));
}

base::FilePath AnswerPath(const base::FilePath& dir, int id) {
  return dir.Append("a." + base::NumberToString(id));
}

// The helper's request file, if it's a regular file of the user's (never
// following a link) and not too big.
std::optional<std::string> ReadRequest(const base::FilePath& path) {
  base::ScopedFD fd(HANDLE_EINTR(open(
      path.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)));
  if (!fd.is_valid()) {
    return std::nullopt;
  }
  struct stat info;
  if (fstat(fd.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      !IsOwnedByUser(info) || info.st_nlink != 1) {
    return std::nullopt;
  }
  std::string request;
  std::array<char, 4096> chunk;
  while (request.size() <= kMaxSshPromptBytes + 16) {
    const ssize_t n = HANDLE_EINTR(read(fd.get(), chunk.data(), chunk.size()));
    if (n < 0) {
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    request.append(
        base::as_string_view(base::span(chunk).first(static_cast<size_t>(n))));
  }
  return request;
}

}  // namespace

// static
std::unique_ptr<SshAskpassServer> SshAskpassServer::Create(
    const base::FilePath& dir,
    RequestCallback on_request) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  struct stat info;
  if (lstat(dir.value().c_str(), &info) != 0 || !S_ISDIR(info.st_mode) ||
      !IsOwnedByUser(info) || (info.st_mode & 0077) != 0) {
    return nullptr;
  }
  const base::FilePath helper = dir.Append(kHelperName);
  {
    base::ScopedFD fd(HANDLE_EINTR(
        open(helper.value().c_str(),
             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0700)));
    if (!fd.is_valid() ||
        !base::WriteFileDescriptor(fd.get(), AskpassHelperScript())) {
      return nullptr;
    }
  }
  const base::FilePath control_path = dir.Append(kControlName);
  if (mkfifo(control_path.value().c_str(), 0600) != 0) {
    return nullptr;
  }
  // Read-write: the FIFO always has a writer (us), so reads never see EOF
  // between helpers, and helpers' opens never block.
  base::ScopedFD control(
      HANDLE_EINTR(open(control_path.value().c_str(),
                        O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)));
  if (!control.is_valid()) {
    return nullptr;
  }
  auto server = base::WrapUnique(
      new SshAskpassServer(dir, std::move(control), std::move(on_request)));
  // Unretained: the controller is a member, so the watch ends with `server`.
  server->control_watch_ = base::FileDescriptorWatcher::WatchReadable(
      server->control_.get(),
      base::BindRepeating(&SshAskpassServer::OnControlReadable,
                          base::Unretained(server.get())));
  return server;
}

SshAskpassServer::SshAskpassServer(base::FilePath dir,
                                   base::ScopedFD control,
                                   RequestCallback on_request)
    : dir_(std::move(dir)),
      helper_path_(dir_.Append(kHelperName)),
      control_(std::move(control)),
      on_request_(std::move(on_request)) {}

SshAskpassServer::~SshAskpassServer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RejectAll();
}

void SshAskpassServer::SetProcessGroup(pid_t process_group) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  process_group_ = process_group;
}

void SshAskpassServer::Reply(int id, const SshPromptReply& reply) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = open_.find(id);
  if (it == open_.end()) {
    return;
  }
  const SshPromptKind kind = it->second;
  open_.erase(it);
  Answer(id, kind, reply);
}

void SshAskpassServer::RejectAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::flat_map<int, SshPromptKind> open = std::move(open_);
  open_.clear();
  for (const auto& [id, kind] : open) {
    // A rejection is "no\n" whatever the kind (EncodeAskpassAnswer()).
    Answer(id, SshPromptKind::kNotice, SshPromptReply::Cancel());
  }
}

void SshAskpassServer::OnControlReadable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::array<char, 512> buffer;
  const ssize_t n =
      HANDLE_EINTR(read(control_.get(), buffer.data(), buffer.size()));
  if (n <= 0) {
    return;  // EAGAIN; EOF can't happen (we're a writer).
  }
  partial_line_.append(
      base::as_string_view(base::span(buffer).first(static_cast<size_t>(n))));
  std::vector<int> ids;
  size_t newline;
  while ((newline = partial_line_.find('\n')) != std::string::npos) {
    const std::string line = partial_line_.substr(0, newline);
    partial_line_.erase(0, newline + 1);
    int id = 0;
    if (!line.empty() && line.size() <= 10 &&
        std::ranges::all_of(line, base::IsAsciiDigit<char>) &&
        base::StringToInt(line, &id) && id > 1) {
      ids.push_back(id);
    }
  }
  if (partial_line_.size() > kMaxControlLine) {
    partial_line_.clear();  // Garbage: not a helper.
  }
  for (int id : ids) {
    HandleRequest(id);
  }
}

void SshAskpassServer::HandleRequest(int id) {
  if (open_.contains(id)) {
    return;
  }
  // The helper must be ssh's child, in the supervisor's process group.
  if (process_group_ <= 0 || getpgid(id) != process_group_) {
    return;
  }
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  const base::FilePath path = RequestPath(dir_, id);
  std::optional<std::string> request = ReadRequest(path);
  base::DeleteFile(path);
  if (!request) {
    Answer(id, SshPromptKind::kNotice, SshPromptReply::Cancel());
    return;
  }
  SshPrompt prompt = ParseAskpassRequest(*request);
  if (prompt.kind != SshPromptKind::kNotice) {
    // A notice gets no answer: ssh kills its helper when it's done.
    open_[id] = prompt.kind;
  }
  on_request_.Run(id, std::move(prompt));
}

void SshAskpassServer::Answer(int id,
                              SshPromptKind kind,
                              const SshPromptReply& reply) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  const SshSecret answer = EncodeAskpassAnswer(kind, reply);
  // Non-blocking: if the helper is gone (no reader), this fails (ENXIO)
  // instead of waiting.
  base::ScopedFD fd(
      HANDLE_EINTR(open(AnswerPath(dir_, id).value().c_str(),
                        O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    return;
  }
  struct stat info;
  if (fstat(fd.get(), &info) != 0 || !S_ISFIFO(info.st_mode) ||
      !IsOwnedByUser(info)) {
    return;
  }
  // At most kMaxSshAnswerBytes + 4 bytes into an empty pipe: written whole.
  base::WriteFileDescriptor(fd.get(), answer.value());
}

}  // namespace koan::remote
