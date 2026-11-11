// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_io.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/posix/eintr_wrapper.h"
#include "build/build_config.h"

namespace koan::cli {

namespace {

bool FillAddress(const std::string& path,
                 sockaddr_un* address,
                 std::string* error) {
  *address = sockaddr_un{};
  address->sun_family = AF_UNIX;
  if (path.empty() || path.size() >= sizeof(address->sun_path)) {
    *error = "the socket path is too long: " + path;
    return false;
  }
  // (The rest stays zero: the path is NUL-terminated.)
  base::span(address->sun_path).copy_prefix_from(base::span(path));
  return true;
}

void SetCloseOnExec(int fd) {
  fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

}  // namespace

base::ScopedFD ConnectUnix(const std::string& path, std::string* error) {
  sockaddr_un address;
  if (!FillAddress(path, &address, error)) {
    return base::ScopedFD();
  }
  base::ScopedFD fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd.is_valid()) {
    *error = std::string("socket: ") + strerror(errno);
    return base::ScopedFD();
  }
  SetCloseOnExec(fd.get());
#if BUILDFLAG(IS_APPLE)
  // A write to a socket the browser closed is an error, not SIGPIPE.
  const int on = 1;
  setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
  if (HANDLE_EINTR(connect(fd.get(), reinterpret_cast<sockaddr*>(&address),
                           sizeof(address))) != 0) {
    *error = std::string("cannot connect to ") + path + ": " + strerror(errno);
    return base::ScopedFD();
  }
  return fd;
}

base::ScopedFD ListenUnix(const std::string& path, std::string* error) {
  sockaddr_un address;
  if (!FillAddress(path, &address, error)) {
    return base::ScopedFD();
  }
  // Only a socket is replaced, never another kind of file.
  struct stat info;
  if (lstat(path.c_str(), &info) == 0) {
    if (!S_ISSOCK(info.st_mode) || info.st_uid != geteuid()) {
      *error = path + " is in the way";
      return base::ScopedFD();
    }
    unlink(path.c_str());
  }
  base::ScopedFD fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd.is_valid()) {
    *error = std::string("socket: ") + strerror(errno);
    return base::ScopedFD();
  }
  SetCloseOnExec(fd.get());
  const mode_t old_mask = umask(0177);
  const int bound =
      bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address));
  umask(old_mask);
  if (bound != 0 || chmod(path.c_str(), 0600) != 0 ||
      listen(fd.get(), 8) != 0) {
    *error = std::string("cannot listen on ") + path + ": " + strerror(errno);
    return base::ScopedFD();
  }
  return fd;
}

FrameChannel::FrameChannel(base::ScopedFD fd, size_t max_payload)
    : fd_(std::move(fd)), decoder_(max_payload) {}

FrameChannel::~FrameChannel() = default;

bool FrameChannel::Write(std::string_view payload) {
  std::optional<std::vector<uint8_t>> frame =
      agent::EncodeFrame(base::as_byte_span(payload), decoder_.max_payload());
  if (!frame || closed_ || write_failed_) {
    return false;
  }
  size_t written = 0;
  while (written < frame->size()) {
    const base::span<const uint8_t> rest = base::span(*frame).subspan(written);
#if BUILDFLAG(IS_APPLE)
    const ssize_t n = HANDLE_EINTR(write(fd_.get(), rest.data(), rest.size()));
#else
    const ssize_t n =
        HANDLE_EINTR(send(fd_.get(), rest.data(), rest.size(), MSG_NOSIGNAL));
#endif
    if (n <= 0) {
      write_failed_ = true;
      return false;
    }
    written += static_cast<size_t>(n);
  }
  return true;
}

bool FrameChannel::ReadAvailable() {
  std::array<uint8_t, 64 * 1024> buffer;
  const ssize_t n = HANDLE_EINTR(read(fd_.get(), buffer.data(), buffer.size()));
  if (n <= 0 ||
      !decoder_.Append(base::span(buffer).first(static_cast<size_t>(n)))) {
    closed_ = true;
    return false;
  }
  return true;
}

std::optional<std::string> FrameChannel::TakeFrame() {
  std::optional<std::vector<uint8_t>> frame = decoder_.TakeFrame();
  if (!frame) {
    return std::nullopt;
  }
  return std::string(frame->begin(), frame->end());
}

std::optional<std::string> FrameChannel::Read(base::TimeDelta timeout) {
  const base::TimeTicks deadline = timeout.is_max()
                                       ? base::TimeTicks::Max()
                                       : base::TimeTicks::Now() + timeout;
  while (true) {
    if (std::optional<std::string> frame = TakeFrame()) {
      return frame;
    }
    if (closed_ || decoder_.failed()) {
      closed_ = true;
      return std::nullopt;
    }
    int wait_ms = -1;
    if (!deadline.is_max()) {
      const base::TimeDelta left = deadline - base::TimeTicks::Now();
      if (left <= base::TimeDelta()) {
        return std::nullopt;
      }
      wait_ms = static_cast<int>(left.InMillisecondsRoundedUp());
    }
    pollfd poll_fd = {fd_.get(), POLLIN, 0};
    const int ready = HANDLE_EINTR(poll(&poll_fd, 1, wait_ms));
    if (ready < 0) {
      closed_ = true;
      return std::nullopt;
    }
    if (ready == 0) {
      return std::nullopt;
    }
    if (!ReadAvailable()) {
      return std::nullopt;
    }
  }
}

}  // namespace koan::cli
