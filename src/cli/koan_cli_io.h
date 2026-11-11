// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_IO_H_
#define KOAN_CLI_KOAN_CLI_IO_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/scoped_file.h"
#include "base/time/time.h"
#include "koan/browser/agent/koan_agent_frame.h"

// Frames on a unix socket for the `koan` tool: the browser's socket and the
// holder's. Blocking, with timeouts; POSIX only. No //net, so that the tool
// starts in a few milliseconds.
namespace koan::cli {

// Connects to the unix socket at `path`; an invalid fd (and `error`) if not.
base::ScopedFD ConnectUnix(const std::string& path, std::string* error);

// Listens on `path` (its directory must exist and be the user's own, 0700):
// a socket left by a process that is gone is replaced; the socket is 0600.
base::ScopedFD ListenUnix(const std::string& path, std::string* error);

// A framed connection over one socket.
class FrameChannel {
 public:
  // `max_payload` bounds what it reads (the browser's frames may be large:
  // screenshots).
  FrameChannel(base::ScopedFD fd, size_t max_payload);
  FrameChannel(const FrameChannel&) = delete;
  FrameChannel& operator=(const FrameChannel&) = delete;
  ~FrameChannel();

  int fd() const { return fd_.get(); }

  // Writes one frame (blocking). False if the frame is too large or the
  // socket failed; what the other end sent before it closed can still be
  // read.
  bool Write(std::string_view payload);

  // The next frame, waiting at most `timeout` (TimeDelta::Max(): forever).
  // nullopt on timeout, close or a bad frame; `closed()` then says which.
  std::optional<std::string> Read(base::TimeDelta timeout);

  // For a poll loop: reads what the socket has now (never blocks long) into
  // the decoder. False if it closed or sent a bad frame.
  bool ReadAvailable();
  // A frame already read, if there is one.
  std::optional<std::string> TakeFrame();

  bool closed() const { return closed_; }

 private:
  base::ScopedFD fd_;
  agent::FrameDecoder decoder_;
  bool closed_ = false;
  bool write_failed_ = false;
};

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_IO_H_
